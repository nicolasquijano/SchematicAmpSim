#include "PluginProcessor.h"

#include "FxPresets.h"

#include "CabData.h"

#include <algorithm>
#include <array>

#include "PluginEditor.h"
#include "CabinetModule.h"
#include "RackState.h"
#include "EqPresets.h"
#include "sas/embedded_amps.h"

namespace {

using Layout = juce::AudioProcessorValueTreeState::ParameterLayout;
constexpr int kVersion = 1;

using RackState::dbParam;
using RackState::knob;

constexpr std::size_t kMaxChunk = 4096;
constexpr std::size_t kMaxStoredIrBytes = 4 * 1024 * 1024;
constexpr double kMaxIrSeconds = 4.0;

}  // namespace

Layout SasProcessor::createLayout(const std::vector<sas::AmpDef>& defs) {
    Layout l;
    juce::StringArray ampNames;
    for (const auto& d : defs) ampNames.add(d.name);
    l.add(std::make_unique<juce::AudioParameterChoice>(juce::ParameterID{ids::amp, kVersion}, "Amp", ampNames, 0));
    l.add(knob(ids::volume, "Preamp Volume", 6.0f));
    l.add(knob(ids::volume2, "Preamp Volume II", 5.0f));
    l.add(knob(ids::bass, "Bass", 5.0f));
    l.add(knob(ids::middle, "Middle", 5.0f));
    l.add(knob(ids::treble, "Treble", 6.0f));
    l.add(knob(ids::presence, "Presence", 5.0f));
    l.add(knob(ids::master, "Master", 5.0f));
    l.add(knob(ids::power, "Power", 5.0f));
    l.add(knob(ids::vibSpeed, "Vibrato Speed", 5.0f));
    l.add(knob(ids::vibIntensity, "Vibrato Intensity", 5.0f));
    l.add(knob(ids::reverb, "Reverb", 3.0f));
    l.add(knob(ids::resonance, "Resonance", 5.0f));
    l.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID{ids::vibOn, kVersion}, "Vibrato", false));
    l.add(dbParam(ids::input, "Input"));
    // The amps are calibrated to a loud, full-scale speaker signal; a new instance opens 10 dB lower so it does not hit the host hot.
    l.add(dbParam(ids::output, "Output", -10.0f));
    // New qualities are only appended so saved projects keep theirs (same rule as the Rust build).
    l.add(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID{ids::quality, kVersion}, "Quality", juce::StringArray{"Normal (2x)", "High (4x)", "Lite (2x)", "Eco (2x)", "Efficient (2x/1x)"}, 4));  // a new instance opens in Efficient
    l.add(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID{ids::cab, kVersion}, "Cabinet", juce::StringArray{"Off", "Filter", "IR", "NAM"}, 1));
    l.add(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID{ids::jack, kVersion}, "Input jack", juce::StringArray{"Jack 1", "Jack 2", "Jack 3", "Jack 4", "Jack 5", "Jack 6", "Jack 7", "Jack 8"}, 0));
    l.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID{ids::levelMatch, kVersion}, "Level match", true));
    for (int i = 0; i < 4; ++i)
        l.add(std::make_unique<juce::AudioParameterChoice>(juce::ParameterID{ids::sel[i], kVersion}, "Amp switch " + juce::String(i + 1),
                                                           juce::StringArray{"Position 1", "Position 2", "Position 3", "Position 4", "Position 5"}, 0));
    RackState::addParams(l, sas::ModuleType::Overdrive, 0);
    l.add(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID{ids::pickup, kVersion}, "Guitar pickup", juce::StringArray{"Off", "Single coil", "Humbucker", "P-90"}, 0));
    l.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{ids::cable, kVersion}, "Guitar cable", juce::NormalisableRange<float>(0.0f, 14.0f, 2.0f), 4.0f,
                                                       juce::AudioParameterFloatAttributes().withLabel("m").withStringFromValueFunction([](float v, int) { return juce::String(v, 0); })));
    RackState::addParams(l, sas::ModuleType::Eq, 0);
    RackState::addParams(l, sas::ModuleType::Pitch, 0);
    RackState::addParams(l, sas::ModuleType::Gate, 0);
    RackState::addParams(l, sas::ModuleType::Delay, 0);
    RackState::addParams(l, sas::ModuleType::Reverb, 0);
    RackState::addParams(l, sas::ModuleType::Phaser, 0);
    l.add(std::make_unique<juce::AudioParameterChoice>(juce::ParameterID{ids::phPost, kVersion}, "Phaser position", juce::StringArray{"Pre amp", "Post amp"}, 0,
                                                       juce::AudioParameterChoiceAttributes().withAutomatable(false)));  // read once, to place a module of an old project
    RackState::addParams(l, sas::ModuleType::Chorus, 0);
    l.add(std::make_unique<juce::AudioParameterChoice>(juce::ParameterID{ids::chPost, kVersion}, "Chorus position", juce::StringArray{"Pre amp", "Post amp"}, 1,
                                                       juce::AudioParameterChoiceAttributes().withAutomatable(false)));  // read once, to place a module of an old project
    RackState::addParams(l, sas::ModuleType::Vibe, 0);
    l.add(std::make_unique<juce::AudioParameterChoice>(juce::ParameterID{ids::vbPost, kVersion}, "Vibe position", juce::StringArray{"Pre amp", "Post amp"}, 0,
                                                       juce::AudioParameterChoiceAttributes().withAutomatable(false)));  // read once, to place a module of an old project
    RackState::addParams(l, sas::ModuleType::Tremolo, 0);
    RackState::addParams(l, sas::ModuleType::Flanger, 0);
    RackState::addParams(l, sas::ModuleType::Wah, 0);
    RackState::addParams(l, sas::ModuleType::Fuzz, 0);
    RackState::addParams(l, sas::ModuleType::Compressor, 0);
    RackState::addParams(l, sas::ModuleType::NamPedal, 0);
    l.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID{ids::tunerOn, kVersion}, "Tuner", false));
    l.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID{ids::tunerMute, kVersion}, "Tuner mutes the output", true));
    l.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{ids::tunerRef, kVersion}, "Tuner reference", juce::NormalisableRange<float>(430.0f, 450.0f, 1.0f), 440.0f,
                                                       juce::AudioParameterFloatAttributes().withLabel("Hz").withStringFromValueFunction([](float v, int) { return juce::String(v, 0); })));
    l.add(knob(ids::irWidth, "IR stereo width", 0.0f));
    l.add(knob(ids::doubleAmount, "Double tracking", 0.0f));
    l.add(knob(ids::doubleTime, "Double tracking time", 5.0f));
    l.add(knob(ids::irBlend, "IR blend", 5.0f));
    l.add(knob(ids::speaker, "Speaker dynamics", 0.0f));
    l.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID{ids::speakerIntense, kVersion}, "Speaker dynamics intense", false));
    l.add(knob(ids::bias, "Bias", 5.0f));
    l.add(std::make_unique<juce::AudioParameterChoice>(juce::ParameterID{ids::tube, kVersion}, "Power tubes", juce::StringArray{ids::tubeTypes[0], ids::tubeTypes[1], ids::tubeTypes[2], ids::tubeTypes[3]}, 0));
    l.add(std::make_unique<juce::AudioParameterChoice>(juce::ParameterID{ids::noise, kVersion}, "Noise and hum", juce::StringArray{"Off", "Low", "Real"}, 0));
    l.add(std::make_unique<juce::AudioParameterInt>(juce::ParameterID{ids::wear, kVersion}, "Wear", 0, 10, 0,
                                                     juce::AudioParameterIntAttributes().withStringFromValueFunction([](int v, int) { return juce::String(v * 10) + " %"; })));
    l.add(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID{ids::unit, kVersion}, "Unit",
        juce::StringArray{"Ideal", "Unit 1", "Unit 2", "Unit 3", "Unit 4", "Unit 5", "Unit 6", "Unit 7", "Unit 8", "Unit 9"}, 0));
    RackState::addParams(l, sas::ModuleType::Cabinet, 0);
    RackState::addParams(l, sas::ModuleType::Mixer, 0);
    RackState::addExtraInstances(l, ampNames);
    return l;
}

// ------------------------------------------------------------------ amp switching

// Builds the circuits for another amp off the audio thread. At most one build runs at a time and
// a newer request replaces an older one that has not started yet.
class SasProcessor::ChainBuilder final : public juce::Thread {
public:
    ChainBuilder(SasProcessor& p, int instance) : juce::Thread("SAS chain builder"), owner_(p), runner_(p.amps_[instance]) { startThread(juce::Thread::Priority::low); }
    ~ChainBuilder() override { stopThread(5000); }

    void wake() { notify(); }

    void run() override {
        while (!threadShouldExit()) {
            wait(-1);
            if (threadShouldExit()) break;
            delete runner_.retired.exchange(nullptr);  // chains the audio thread has swapped out
            const int key = runner_.requestedKey.exchange(-1);
            const int amp = key & 31;
            if (key < 0 || amp >= int(owner_.defs_.size()) || owner_.sampleRate_ <= 0.0) continue;
            try {
                const sas::Condition cond{(key >> 5) & 7, (key >> 8) & 15, key >> 12};
                auto* built = new sas::Chain(sas::applyCondition(owner_.defs_[std::size_t(amp)], cond), owner_.sampleRate_);
                runner_.readyKey.store(key);
                delete runner_.ready.exchange(built);  // replaces a build nobody picked up
            } catch (const std::exception&) {
                // A broken amp definition leaves the current amp running.
            }
        }
    }

private:
    SasProcessor& owner_;
    AmpRunner& runner_;
};

double SasProcessor::cabValue(int cabinet, const char* id) const {
    return cabinet == 0 ? double(apvts.getRawParameterValue(id)->load()) : moduleParam({sas::ModuleType::Cabinet, cabinet}, id);
}

double SasProcessor::ampValue(int instance, const char* id) const {
    return instance == 0 ? double(apvts.getRawParameterValue(id)->load()) : moduleParam({sas::ModuleType::Amp, instance}, id);
}

int SasProcessor::selectedAmpIndex(int instance) const {
    return juce::jlimit(0, int(defs_.size()) - 1, int(ampValue(instance, ids::amp) + 0.5));
}

sas::Condition SasProcessor::wantedCondition(int instance) const {
    const auto& def = defs_[std::size_t(selectedAmpIndex(instance))];
    sas::Condition c;
    const int tubes = int(def.power.tubes.size());
// The parameter holds a tube TYPE, not a position: every amp lists its tubes in its own order (stock first), so a position picked on
    // one amp would mean another tube on the next. An amp that does not offer the type keeps its own tubes.
    c.tube = 0;
    if (const int type = juce::jlimit(0, 3, int(ampValue(instance, ids::tube) + 0.5)); type > 0 && tubes > 1)
        for (int k = 0; k < tubes; ++k)
            if (def.power.tubes[std::size_t(k)].name == ids::tubeTypes[type]) c.tube = k;
    c.wear = juce::jlimit(0, 10, int(ampValue(instance, ids::wear) + 0.5));
    c.unit = juce::jlimit(0, 9, int(ampValue(instance, ids::unit) + 0.5));
    return c;
}

int SasProcessor::wantedKey(int instance) const { return makeKey(selectedAmpIndex(instance), wantedCondition(instance)); }

void SasProcessor::swapInBuiltChain(int instance) {
    auto& runner = amps_[instance];
    sas::Chain* built = runner.ready.exchange(nullptr);
    if (!built) return;
    const int key = runner.readyKey.load();
    const int amp = key & 31;
    // Start the new circuits at the current knob positions instead of ramping from their defaults.
    runner.activeAmp = amp;
    runner.activeKey = key;
    if (auto* module = ampModule(instance)) {
        module->setSettings(readSettings(anyIrLoaded(), instance));
        module->snapChain(*built);
        module->setChain(built);
    } else {
        built->snap(readSettings(anyIrLoaded(), instance));
    }
    sas::Chain* old = runner.chain.release();
    runner.chain.reset(built);
    // The builder frees it; if it has not yet freed the previous one, do it here (rare).
    delete runner.retired.exchange(old);
    latencyReported_ = -1;
}

// ------------------------------------------------------------------ lifecycle

SasProcessor::SasProcessor()
    : AudioProcessor(BusesProperties()
                         .withInput("Input", juce::AudioChannelSet::stereo(), true)
                         .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      apvts(*this, nullptr, "SAS" /* state tag: part of saved projects */, createLayout([] {
                std::vector<sas::AmpDef> defs;
                for (const auto& id : sas::embeddedAmpIds(true)) defs.push_back(sas::loadEmbeddedAmp(id));
                return defs;
            }())) {
    for (const auto& id : sas::embeddedAmpIds(true)) defs_.push_back(sas::loadEmbeddedAmp(id));

    // Resolve, per instance of the amp and per amp, which parameter drives each of its knobs (via its "ui" block).
    for (int inst = 0; inst < 2; ++inst)
        for (const auto& def : defs_) {
            AmpParams ap;
            ap.knobs.assign(def.preamp.knobs.size(), nullptr);
            ap.powerKnobs.assign(def.power.knobs.size(), nullptr);
            auto find = [&](const std::string& base) { return apvts.getRawParameterValue(RackState::paramId(base.c_str(), inst)); };
            for (const auto& u : def.ui) {
                if (u.param == ids::presence) ap.presence = find(u.param);
                if (u.param == ids::vibSpeed) ap.vibSpeed = find(u.param);
                if (!u.knob.empty()) ap.knobs[def.knobIndex(u.knob)] = find(u.param);
                if (!u.powerKnob.empty()) ap.powerKnobs[def.powerKnobIndex(u.powerKnob)] = find(u.param);
            }
            if (!ap.presence) ap.presence = find(ids::presence);
            if (!ap.vibSpeed) ap.vibSpeed = find(ids::vibSpeed);
            ampParams_[std::size_t(inst)].push_back(std::move(ap));
        }
    for (auto* st : {&settings_, &settingsB_}) {
        st->knobs.reserve(16);  // never reallocate on the audio thread
        st->powerKnobs.reserve(16);
    }
    mono_.resize(kMaxChunk);
    mono2_.resize(kMaxChunk);
    monoR_.resize(kMaxChunk);
    amps_[1].activeKey = -1;  // the second amp is built when a parallel asks for it
    for (int i = 0; i < 2; ++i) amps_[i].builder = std::make_unique<ChainBuilder>(*this, i);
    // A fresh instance has "4x12 OS Mix 1" ready in slot A (a restored project replaces it); the first of the list if that one is not there.
    int firstCab = 0;
    for (int i = 0; i < builtInImpulseResponseCount(); ++i)
        if (builtInImpulseResponseName(i) == "4x12 OS Mix 1") firstCab = i;
    firstCab_ = firstCab;
    loadBuiltInImpulseResponse(firstCab, 0, 0);
    // The parameters of every module instance, found once (the audio thread reads them by pointer).
    for (int t = 0; t < sas::kModuleTypeCount; ++t)
        for (int k = 0; k < RackState::maxInstances(static_cast<sas::ModuleType>(t)); ++k)
            for (const char* base : RackState::paramIds(static_cast<sas::ModuleType>(t)))
                modParams_[std::size_t(t)][std::size_t(k)].push_back(apvts.getRawParameterValue(RackState::paramId(base, k)));
    rackOrder_ = RackState::defaultOrder();
    syncRack(sas::Transition::Immediate);
}

SasProcessor::~SasProcessor() {
    for (auto& runner : amps_) runner.builder.reset();  // joins the thread before the chains it may own go away
    for (auto& runner : amps_) {
        delete runner.ready.exchange(nullptr);
        delete runner.retired.exchange(nullptr);
    }
}

bool SasProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const {
    const auto& out = layouts.getMainOutputChannelSet();
    // Mono or stereo in, mono or stereo out: a mono guitar track can feed the stereo output (the amp is mono inside, the cabinet and the
    // double tracking make the stereo).
    const auto& in = layouts.getMainInputChannelSet();
    return (out == juce::AudioChannelSet::mono() || out == juce::AudioChannelSet::stereo())
           && (in == juce::AudioChannelSet::mono() || in == juce::AudioChannelSet::stereo());
}

void SasProcessor::prepareToPlay(double sampleRate, int samplesPerBlock) {
    const int wanted = wantedKey(0);
    const bool rateChanged = sampleRate != sampleRate_;
    if (!amps_[0].chain || rateChanged || wanted != amps_[0].activeKey) {
        sampleRate_ = sampleRate;
        delete amps_[0].ready.exchange(nullptr);
        amps_[0].chain = std::make_unique<sas::Chain>(sas::applyCondition(defs_[std::size_t(wanted & 31)], wantedCondition(0)), sampleRate);  // heavy: solves the DC points
        amps_[0].activeAmp = wanted & 31;
        amps_[0].activeKey = wanted;
    } else {
        amps_[0].chain->reset();
    }
    if (auto* amp = ampModule(0)) amp->setChain(amps_[0].chain.get());
    // The second amp's circuits are for another rate when it changed: they are built again, off this thread, when a parallel needs them.
    if (rateChanged || (amps_[1].chain && !ampModule(1))) {
        delete amps_[1].ready.exchange(nullptr);
        if (auto* amp = ampModule(1)) amp->setChain(nullptr);
        amps_[1].chain.reset();
        amps_[1].activeKey = -1;
    } else if (amps_[1].chain) {
        amps_[1].chain->reset();
    }
    for (int i = 0; i < kNamPedalSlots; ++i) {
        // The NAM pedals too.
        const juce::ScopedLock lock(namLock_);
        auto& slot = namPedals_[i];
        if (slot.model && slot.rate != sampleRate && slot.bytes.getSize() > 0) {
            const juce::MemoryBlock bytes = slot.bytes;
            const juce::String name = slot.name;
            const juce::ScopedUnlock unlock(namLock_);
            loadNamPedalFromMemory(bytes.getData(), bytes.getSize(), name, i);
        }
    }
    {
        // The NAM cabinet too.
        const juce::ScopedLock lock(namLock_);
        if (namCab_ && namCabRate_ != sampleRate && namCabBytes_.getSize() > 0) {
            const juce::MemoryBlock bytes = namCabBytes_;
            const juce::String name = namCabName_;
            const juce::ScopedUnlock unlock(namLock_);
            loadNamCabFromMemory(bytes.getData(), bytes.getSize(), name);
        }
    }
    {
        // A model is tied to the rate it was prepared for: load it again from its bytes when the host rate changed.
        const juce::ScopedLock lock(namLock_);
        if (nam_ && namRate_ != sampleRate && namBytes_.getSize() > 0) {
            const juce::MemoryBlock bytes = namBytes_;
            const juce::String name = namName_;
            const juce::ScopedUnlock unlock(namLock_);
            loadNamFromMemory(bytes.getData(), bytes.getSize(), name);
        }
    }
    // The convolution is told the largest block it can ever get: processBlock hands it chunks of up to kMaxChunk whatever block size the host
    // announced here (some hosts send bigger ones), and a block above the announced maximum crashes it.
    juce::dsp::ProcessSpec spec{sampleRate, static_cast<juce::uint32>(kMaxChunk), 1};
    (void)samplesPerBlock;
    for (auto& bank : cabs_) {
        for (auto& slot : bank.ir) slot.conv.prepare(spec);
        bank.irWasActive = false;
        bank.dbl.prepare(sampleRate);
    }
    latencyReported_ = -1;
    rack_.prepare(sampleRate, int(kMaxChunk));
    tuner_.prepare(sampleRate);
}

sas::ChainSettings SasProcessor::readSettings(bool irAvailable, int instance) {
    auto& s = instance == 0 ? settings_ : settingsB_;
    const int ampIndex = amps_[instance].activeAmp.load();
    const auto& def = defs_[std::size_t(ampIndex)];
    const auto& ap = ampParams_[std::size_t(instance)][std::size_t(ampIndex)];
    s.knobs.resize(def.preamp.knobs.size());
    for (std::size_t i = 0; i < s.knobs.size(); ++i)
        s.knobs[i] = ap.knobs[i] ? double(ap.knobs[i]->load()) / 10.0 : def.preamp.knobs[i].def;
    // The second amp reads its own copy of every parameter of the amp; what is not the amp's (the output level, the cabinet) is shared.
    auto raw = [this, instance](const char* id) {
        if (instance > 0) {
            const auto& names = RackState::paramIds(sas::ModuleType::Amp);
            for (std::size_t i = 0; i < names.size(); ++i)
                if (names[i] == id) return moduleParam({sas::ModuleType::Amp, instance}, id);
        }
        return double(apvts.getRawParameterValue(id)->load());
    };
    s.powerKnobs.resize(def.power.knobs.size());
    for (std::size_t i = 0; i < s.powerKnobs.size(); ++i)
        s.powerKnobs[i] = ap.powerKnobs[i] ? double(ap.powerKnobs[i]->load()) / 10.0 : def.power.knobs[i].def;
    s.presence = double(ap.presence->load()) / 10.0;
    s.inputDb = raw(ids::input);
    s.outputDb = raw(ids::output);
    s.quality = static_cast<sas::Quality>(juce::jlimit(0, 4, int(raw(ids::quality) + 0.5)));
    s.jack = def.preamp.jacks.empty() ? 0 : std::size_t(juce::jlimit(0, int(def.preamp.jacks.size()) - 1, int(raw(ids::jack) + 0.5)));
    const auto cab = static_cast<CabMode>(juce::jlimit(0, 3, int(raw(ids::cab) + 0.5)));
    // With no IR loaded the IR mode falls back to the cab filter instead of the raw amp.
    s.cabFilter = cab == CabMode::Filter || (cab == CabMode::Ir && !irAvailable) || (cab == CabMode::Nam && namCabActive_.load() == nullptr);  // (the cabinet module applies it)
    s.cabFilterInChain = false;  // the cabinet module applies it
    s.jackLevelMatch = true;  // the jacks always play at a similar level; the old "level_match" parameter stays only so saved projects load
    s.selectors.clear();
    for (std::size_t i = 0; i < def.preamp.selectors.size() && i < 4; ++i)
        s.selectors.push_back(std::size_t(juce::jlimit(0, int(def.preamp.selectors[i].positions.size()) - 1, int(raw(ids::sel[i]) + 0.5))));
    s.vibratoOn = raw(ids::vibOn) > 0.5;
    s.vibratoSpeed = double(ap.vibSpeed->load()) / 10.0;
    s.tsOn = false;  // the overdrive and the compressor are rack modules: the amp module takes the ones next to it
    s.compOn = false;
    s.pickup = int(raw(ids::pickup) + 0.5);
    s.cableMeters = int(raw(ids::cable) + 0.5);
    s.bias = 0.75 + 0.05 * raw(ids::bias);
    s.speaker = raw(ids::speaker) / 10.0;
    s.speakerIntense = raw(ids::speakerIntense) > 0.5;
    if (def.nam) {
        // The loader's two knobs: DRIVE into the model and LEVEL out of it, 0..10 with 5 as unity, 2.4 dB per step.
        s.nam = namActive_.load();
        s.namDriveDb = (raw(ids::volume) - 5.0) * 2.4;
        s.namLevelDb = (raw(ids::master) - 5.0) * 2.4;
        s.namBass = raw(ids::bass) / 10.0;
        s.namMid = raw(ids::middle) / 10.0;
        s.namTreble = raw(ids::treble) / 10.0;
    }
    s.noise = std::array<double, 3>{0.0, 0.5, 1.0}[std::size_t(juce::jlimit(0, 2, int(raw(ids::noise) + 0.5)))];
    return s;
}

double SasProcessor::moduleParam(sas::SlotRef slot, const char* baseId) const {
    const auto& names = RackState::paramIds(slot.type);
    const auto& cache = modParams_[std::size_t(slot.type)][std::size_t(slot.instance)];
    for (std::size_t i = 0; i < names.size() && i < cache.size(); ++i)
        if (names[i] == baseId) return double(cache[i]->load());
    return 0.0;
}

// Hands each module of the rack the parameters of its instance. Audio thread.
void SasProcessor::applyModuleParams(const sas::RackOrder& order) {
    using T = sas::ModuleType;
    double bpm = 0.0;  // the host tempo, asked once, when a delay wants it
    bool gateSeen = false;
    for (int i = 0; i < order.count; ++i) {
        const sas::SlotRef s = order.slots[std::size_t(i)];
        sas::Module* m = rack_.audioModule(s);
        if (!m) continue;
        auto p = [&](const char* id) { return moduleParam(s, id); };
        switch (s.type) {
            case T::Gate: {
                auto* g = static_cast<sas::GateModule*>(m);
                g->setParams(p(ids::gateOn) > 0.5, p(ids::gateThreshold), p(ids::gateRelease));
                if (!gateSeen) {
                    gateSeen = true;
                    gateGain_.store(float(g->gain()));
                }
                break;
            }
            case T::Compressor: {
                sas::CompControls c;
                c.sustain = p(ids::compSustain) / 10.0;
                c.blend = p(ids::compBlend) / 10.0;
                c.tone = p(ids::compTone) / 10.0;
                c.level = p(ids::compLevel) / 10.0;
                c.fastRelease = p(ids::compFast) > 0.5;
                static_cast<sas::CompressorModule*>(m)->setParams(p(ids::compOn) > 0.5, c);
                break;
            }
            case T::Overdrive:
                static_cast<sas::OverdriveModule*>(m)->setParams(p(ids::tsOn) > 0.5, {p(ids::tsDrive) / 10.0, p(ids::tsTone) / 10.0, p(ids::tsLevel) / 10.0});
                break;
            case T::NamPedal: {
                auto* n = static_cast<sas::NamPedalModule*>(m);
                n->setModel(namPedals_[s.instance].active.load());
                n->setParams(p(ids::nmpOn) > 0.5, p(ids::nmpDrive), p(ids::nmpLevel));
                break;
            }
            case T::Phaser: {
                sas::PhaserParams q;
                q.on = p(ids::phOn) > 0.5;
                q.speedHz = p(ids::phSpeed);
                q.intense = p(ids::phIntense) > 0.5;
                q.stereo = p(ids::phStereo) > 0.5;
                static_cast<sas::PhaserModule*>(m)->setParams(q);
                break;
            }
            case T::Chorus: {
                sas::ChorusParams q;
                q.on = p(ids::chOn) > 0.5;
                q.juno = p(ids::chMode) > 0.5;
                q.rateHz = p(ids::chRate);
                q.depth = p(ids::chDepth) / 100.0;
                q.junoButton = int(p(ids::chJuno) + 0.5);
                q.stereo = p(ids::chStereo) > 0.5;
                static_cast<sas::ChorusModule*>(m)->setParams(q);
                break;
            }
            case T::Vibe: {
                sas::VibeParams q;
                q.on = p(ids::vbOn) > 0.5;
                q.speedHz = p(ids::vbSpeed);
                q.intensity = p(ids::vbIntensity) / 100.0;
                q.vibrato = p(ids::vbVibrato) > 0.5;
                q.stereo = p(ids::vbStereo) > 0.5;
                static_cast<sas::VibeModule*>(m)->setParams(q);
                break;
            }
            case T::Tremolo: {
                sas::TremoloParams q;
                q.on = p(ids::trOn) > 0.5;
                q.rateHz = p(ids::trRate);
                q.depth = p(ids::trDepth) / 100.0;
                q.shape = int(p(ids::trShape) + 0.5);
                q.stereo = p(ids::trStereo) > 0.5;
                static_cast<sas::TremoloModule*>(m)->setParams(q);
                break;
            }
            case T::Flanger: {
                sas::FlangerParams q;
                q.on = p(ids::flOn) > 0.5;
                q.rateHz = p(ids::flRate);
                q.depth = p(ids::flDepth) / 100.0;
                q.manual = p(ids::flManual) / 100.0;
                q.feedback = p(ids::flFeedback) / 100.0;
                q.stereo = p(ids::flStereo) > 0.5;
                static_cast<sas::FlangerModule*>(m)->setParams(q);
                break;
            }
            case T::Fuzz: {
                sas::FuzzParams q;
                q.on = p(ids::fzOn) > 0.5;
                q.type = int(p(ids::fzType) + 0.5);
                q.fuzz = p(ids::fzFuzz) / 10.0;
                q.tone = p(ids::fzTone) / 10.0;
                q.bias = p(ids::fzBias) / 10.0;
                q.mix = p(ids::fzMix) / 100.0;
                q.levelDb = p(ids::fzLevel);
                static_cast<sas::FuzzModule*>(m)->setParams(q);
                break;
            }
            case T::Wah: {
                sas::WahParams q;
                q.on = p(ids::wahOn) > 0.5;
                q.mode = int(p(ids::wahMode) + 0.5);
                q.position = p(ids::wahPos) / 100.0;
                q.heelHz = p(ids::wahHeel);
                q.toeHz = p(ids::wahToe);
                q.q = p(ids::wahQ);
                q.mix = p(ids::wahMix) / 100.0;
                q.levelDb = p(ids::wahLevel);
                q.sensitivity = p(ids::wahSens) / 100.0;
                q.attackMs = p(ids::wahAttack);
                q.releaseMs = p(ids::wahRelease);
                q.down = p(ids::wahDir) > 0.5;
                q.rateHz = p(ids::wahRate);
                q.stereo = p(ids::wahStereo) > 0.5;
                static_cast<sas::WahModule*>(m)->setParams(q);
                break;
            }
            case T::Pitch:
                static_cast<sas::PitchModule*>(m)->setSemitones(p(ids::pitch) + p(ids::pitchFine) / 100.0);
                break;
            case T::Eq: {
                sas::EqParams q;
                q.on = p(ids::eqOn) > 0.5;
                q.hpfHz = p(ids::eqHpf);
                q.lpfHz = p(ids::eqLpf);
                for (int b = 0; b < 5; ++b) {
                    q.band[std::size_t(b)].freq = p(ids::eqFreq[b]);
                    q.band[std::size_t(b)].gainDb = p(ids::eqGain[b]);
                    q.band[std::size_t(b)].q = p(ids::eqQ[b]);
                }
                static_cast<sas::EqModule*>(m)->setParams(q);
                break;
            }
            case T::Delay: {
                sas::DelayParams d;
                d.on = p(ids::dlyOn) > 0.5;
                d.timeMs = p(ids::dlyTime);
                if (const int sync = int(p(ids::dlySync) + 0.5); sync > 0) {
                    // Tied to the host tempo (120 bpm when the host does not say): a quarter note, a dotted eighth, an eighth, a sixteenth.
                    if (bpm <= 0.0) {
                        bpm = 120.0;
                        if (auto* head = getPlayHead())
                            if (auto pos = head->getPosition())
                                if (auto b = pos->getBpm()) bpm = std::clamp(*b, 30.0, 300.0);
                    }
                    static constexpr double kBeats[] = {1.0, 1.0, 0.75, 0.5, 0.25};
                    d.timeMs = std::clamp(60000.0 / bpm * kBeats[std::min(sync, 4)], 20.0, sas::StereoDelay::kMaxMs);
                }
                d.feedback = std::clamp(p(ids::dlyFeedback) / 100.0, 0.0, 0.92);
                d.mix = std::clamp(p(ids::dlyMix) / 100.0, 0.0, 1.0);
                d.toneHz = p(ids::dlyTone);
                d.pingPong = p(ids::dlyPing) > 0.5;
                d.analog = p(ids::dlyAnalog) > 0.5;
                d.stereo = p(ids::dlyStereo) > 0.5;
                static_cast<sas::DelayModule*>(m)->setParams(d);
                break;
            }
            case T::Reverb: {
                sas::PlateParams pl;
                pl.on = p(ids::plOn) > 0.5;
                pl.hall = p(ids::plHall) > 0.5;
                pl.decaySec = p(ids::plDecay);
                pl.preMs = p(ids::plPre);
                pl.toneHz = p(ids::plTone);
                pl.lowCutHz = p(ids::plLow);
                pl.mix = std::clamp(p(ids::plMix) / 100.0, 0.0, 1.0);
                pl.mod = std::clamp(p(ids::plMod) / 100.0, 0.0, 1.0);
                pl.shimmer = std::clamp(p(ids::plShimmer) / 100.0, 0.0, 1.0);
                pl.shimmerFifth = p(ids::plShimmerInt) > 0.5;
                pl.stereo = p(ids::plStereo) > 0.5;
                static_cast<sas::ReverbModule*>(m)->setParams(pl);
                break;
            }
            case T::Mixer: {
                sas::MixerParams mp;
                mp.levelA = float(std::pow(10.0, p(ids::mixLevelA) / 20.0));
                mp.levelB = float(std::pow(10.0, p(ids::mixLevelB) / 20.0));
                mp.panA = float(p(ids::mixPanA) / 100.0);
                mp.panB = float(p(ids::mixPanB) / 100.0);
                mp.invertB = p(ids::mixInvertB) > 0.5;
                static_cast<sas::MixerModule*>(m)->setParams(mp);
                break;
            }
            default: break;
        }
    }
    if (!gateSeen) gateGain_.store(1.0f);
}

void SasProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) {
    juce::ScopedNoDenormals noDenormals;
    const int numCh = buffer.getNumChannels();
    const int numSamples = buffer.getNumSamples();
    if (!amps_[0].chain || numCh == 0) return;

    // Another amp was chosen: ask the builder, and swap in its circuits once they are ready. The
    // old amp keeps playing meanwhile.
    for (int instance = 0; instance < 2; ++instance) {
        auto& runner = amps_[instance];
        if (instance == 1 && !ampModule(1)) continue;  // no parallel: no second amp
        swapInBuiltChain(instance);
        const int wanted = wantedKey(instance);
        if (wanted != runner.activeKey && runner.requestedKey.load() != wanted && runner.ready.load() == nullptr) {
            runner.requestedKey.store(wanted);
            runner.builder->wake();
        }
    }

    double irLatencyOf[2] = {0.0, 0.0};
    bool irActiveOf[2] = {false, false};
    for (int cabinet = 0; cabinet < 2; ++cabinet) {
        auto* cabModule = static_cast<CabinetModule*>(rack_.audioModule({sas::ModuleType::Cabinet, cabinet}));
        if (!cabModule) continue;
        auto& cb = cabs_[cabinet];
        const bool aLoaded = cb.ir[0].loaded.load(), bLoaded = cb.ir[1].loaded.load();
        const bool irLoaded = aLoaded || bLoaded;
        const auto cabParam = static_cast<CabMode>(juce::jlimit(0, 3, int(cabValue(cabinet, ids::cab) + 0.5)));
        const bool irActive = cabParam == CabMode::Ir && irLoaded;
        // Only the first cabinet has a NAM cabinet; the second one, set to NAM, plays its filter.
        sas::NamModel* const namCab = cabinet == 0 && cabParam == CabMode::Nam ? namCabActive_.load() : nullptr;
        if (cabinet == 0) {
            if (namCab && !namCabWasActive_) namCab->reset();
            namCabWasActive_ = namCab != nullptr;
        }
        cb.namNow = namCab;
        cb.irActiveNow = irActive;
        cb.aLoaded = aLoaded;
        cb.bLoaded = bLoaded;
        // With no IR loaded the IR mode falls back to the cab filter instead of the raw amp (and so does a NAM cabinet that has no model).
        const bool cabFilterOn = cabParam == CabMode::Filter || (cabParam == CabMode::Ir && !irLoaded) || (cabParam == CabMode::Nam && namCab == nullptr);
        const double irLatency = irActive ? double(std::max(aLoaded ? cb.ir[0].conv.getLatency() : 0, bLoaded ? cb.ir[1].conv.getLatency() : 0)) : 0.0;
        const bool cabOn = cabValue(cabinet, ids::cabOn) > 0.5;
        if (cabOn && !cb.wasOn) {  // coming back: nothing of the convolution's old audio
            for (auto& slot : cb.ir) slot.conv.reset();
            cabModule->reset();
        }
        cb.wasOn = cabOn;
        cabModule->setBlock(cabFilterOn, cabOn ? irLatency : 0.0, cabOn);
        if (irActive && !cb.irWasActive)
            for (auto& slot : cb.ir) slot.conv.reset();
        cb.irWasActive = irActive;
        irLatencyOf[cabinet] = irLatency;
        irActiveOf[cabinet] = irActive;
    }
    for (int instance = 0; instance < 2; ++instance)
        if (auto* amp = ampModule(instance)) amp->setSettings(readSettings(anyIrLoaded(instance), instance));
    const sas::RackOrder order = rack_.order();
    applyModuleParams(order);

    // Latency follows the quality, the convolution and the pitch shifters; report it when it changes.
    const int latency = int(rack_.latencySamples());
    if (latency != latencyReported_) {
        latencyReported_ = latency;
        setLatencySamples(latency);
    }
    const bool tunerOn = apvts.getRawParameterValue(ids::tunerOn)->load() > 0.5f;
    const bool tunerMuted = tunerOn && apvts.getRawParameterValue(ids::tunerMute)->load() > 0.5f;
    idleMa_.store(float(amps_[0].chain->idleMilliamps()));
    for (int start = 0; start < numSamples; start += int(kMaxChunk)) {
        const int n = std::min<int>(int(kMaxChunk), numSamples - start);
        float* m = mono_.data();
        // Only the channels the host really feeds (with a mono input and a stereo output the extra buffer channel is not input).
        const int inCh = std::max(1, std::min(getTotalNumInputChannels(), numCh));
        const float inv = 1.0f / float(inCh);
        for (int i = 0; i < n; ++i) m[i] = 0.0f;
        for (int ch = 0; ch < inCh; ++ch) {
            const float* in = buffer.getReadPointer(ch) + start;
            for (int i = 0; i < n; ++i) m[i] += in[i];
        }
        for (int i = 0; i < n; ++i) m[i] *= inv;
        float inPeak = 0.0f;
        for (int i = 0; i < n; ++i) inPeak = std::max(inPeak, std::fabs(m[i]));
        if (tunerOn) tuner_.push(m, std::size_t(n));  // the raw input, before anything in the rack
        float* mR = monoR_.data();  // the right channel; the left one is m
        if (!rack_.process(m, mR, std::size_t(n))) std::copy(m, m + n, mR);
        amps_[0].chain->applyOutputStereo(m, mR, std::size_t(n));
        if (tunerMuted) {  // tuning: nobody wants to hear the amp
            std::fill(m, m + n, 0.0f);
            std::fill(mR, mR + n, 0.0f);
        }
        spectrumFeed_.push(m, mR, std::size_t(n));  // what is sounding, for the spectrum on the EQ page
        float outPeak = 0.0f;
        for (int i = 0; i < n; ++i) outPeak = std::max(outPeak, std::max(std::fabs(m[i]), std::fabs(mR[i])));
        if (inPeak > inputPeak_.load()) inputPeak_.store(inPeak);
        if (outPeak > outputPeak_.load()) outputPeak_.store(outPeak);

        for (int ch = 0; ch < numCh; ++ch) {
            float* out = buffer.getWritePointer(ch) + start;
            if (numCh == 1)
                for (int i = 0; i < n; ++i) out[i] = 0.5f * (m[i] + mR[i]);  // a mono output: the two ears summed
            else
                std::copy(ch % 2 == 0 ? m : mR, (ch % 2 == 0 ? m : mR) + n, out);
        }
    }
}

// The cabinet: the filter (or the NAM cabinet, or the impulse responses), then the automatic double tracking. A mono signal in, a stereo pair out.
void SasProcessor::processCabinet(CabinetModule& cab, float* m, float* mR, std::size_t nn) {
    const int n = int(nn);
    const int cabinet = cab.instance();
    auto& cb = cabs_[cabinet];
    if (cab.filterOn())
        for (int i = 0; i < n; ++i) m[i] = cab.filter().process(m[i]);
    if (cb.namNow) {  // the NAM cabinet: through a scratch block, the model reads its input while it writes
        cb.namNow->process(m, mono2_.data(), std::size_t(n));
        std::copy(mono2_.data(), mono2_.data() + n, m);
    }
    bool haveR = false;
    if (cb.irActiveNow) {
        const bool aLoaded = cb.aLoaded, bLoaded = cb.bLoaded;
        auto convolve = [n](IrSlot& slot, float* data) {
            float* chans[1] = {data};
            juce::dsp::AudioBlock<float> block(chans, 1, std::size_t(n));
            slot.conv.process(juce::dsp::ProcessContextReplacing<float>(block));
        };
        if (aLoaded && bLoaded) {
            // Both slots: an equal-power blend, and the width that separates them: at 0 both ears get the blend, at 10 the left ear gets A and the
            // right ear B (two microphones, one on each side). Ramped over the block so a knob move does not click.
            const double theta = juce::jlimit(0.0, 1.0, cabValue(cabinet, ids::irBlend) / 10.0) * (juce::MathConstants<double>::pi * 0.5);
            const float gA = float(std::cos(theta)), gB = float(std::sin(theta));
            const float width = juce::jlimit(0.0f, 1.0f, float(cabValue(cabinet, ids::irWidth)) / 10.0f);
            float* m2 = mono2_.data();
            std::copy(m, m + n, m2);
            convolve(cb.ir[0], m);
            convolve(cb.ir[1], m2);
            for (int i = 0; i < n; ++i) {
                const float t = float(i + 1) / float(n);
                const float ga = cb.blendGainA + (gA - cb.blendGainA) * t, gb = cb.blendGainB + (gB - cb.blendGainB) * t;
                const float w = cb.widthNow + (width - cb.widthNow) * t;
                const float a = m[i], b = m2[i];
                const float blend = a * ga + b * gb;
                m[i] = (1.0f - w) * blend + w * a;
                mR[i] = (1.0f - w) * blend + w * b;
            }
            cb.blendGainA = gA;
            cb.blendGainB = gB;
            cb.widthNow = width;
            haveR = true;
        } else {
            convolve(aLoaded ? cb.ir[0] : cb.ir[1], m);
            cb.blendGainA = aLoaded ? 1.0f : 0.0f;  // so a second slot loaded later fades in from this state
            cb.blendGainB = aLoaded ? 0.0f : 1.0f;
        }
    }
    if (!haveR) std::copy(m, m + n, mR);
    // Automatic double tracking widens whatever the cabinet left (identical channels, or the A / B pair).
    cb.dbl.setAmount(cabValue(cabinet, ids::doubleAmount) / 10.0, 8.0 + 22.0 * cabValue(cabinet, ids::doubleTime) / 10.0);
    cb.dbl.process(m, mR, std::size_t(n));
}

void CabinetModule::process(float* l, float* r, std::size_t n) { owner_.processCabinet(*this, l, r, n); }

// ------------------------------------------------------------------ the rack

std::unique_ptr<sas::Module> SasProcessor::makeModule(sas::ModuleType type, int instance) {
    using T = sas::ModuleType;
    switch (type) {
        case T::Amp: {
            auto amp = std::make_unique<sas::AmpModule>();
            amp->setChain(amps_[instance].chain.get());
            return amp;
        }
        case T::Cabinet: return std::make_unique<CabinetModule>(*this, instance);
        case T::Gate: return std::make_unique<sas::GateModule>();
        case T::Compressor: return std::make_unique<sas::CompressorModule>();
        case T::Overdrive: return std::make_unique<sas::OverdriveModule>();
        case T::NamPedal: return std::make_unique<sas::NamPedalModule>();
        case T::Phaser: return std::make_unique<sas::PhaserModule>();
        case T::Chorus: return std::make_unique<sas::ChorusModule>();
        case T::Vibe: return std::make_unique<sas::VibeModule>();
        case T::Tremolo: return std::make_unique<sas::TremoloModule>();
        case T::Flanger: return std::make_unique<sas::FlangerModule>();
        case T::Wah: return std::make_unique<sas::WahModule>();
        case T::Fuzz: return std::make_unique<sas::FuzzModule>();
        case T::Pitch: return std::make_unique<sas::PitchModule>();
        case T::Eq: return std::make_unique<sas::EqModule>();
        case T::Delay: return std::make_unique<sas::DelayModule>();
        case T::Reverb: return std::make_unique<sas::ReverbModule>();
        case T::Splitter: return std::make_unique<sas::SplitterModule>();
        case T::Mixer: return std::make_unique<sas::MixerModule>();
    }
    return nullptr;
}

// Makes the rack match `rackOrder_`: builds the modules that are new (here, on the message thread), publishes the order, retires the ones that are gone.
void SasProcessor::syncRack(sas::Transition how) {
    auto inOrder = [this](sas::SlotRef s) {
        for (int i = 0; i < rackOrder_.count; ++i)
            if (rackOrder_.slots[std::size_t(i)] == s) return true;
        return false;
    };
    for (int t = 0; t < sas::kModuleTypeCount; ++t)
        for (int k = 0; k < RackState::maxInstances(static_cast<sas::ModuleType>(t)); ++k) {
            const sas::SlotRef s{static_cast<sas::ModuleType>(t), k};
            if (inOrder(s) && !rack_.module(s)) rack_.setModule(s, makeModule(s.type, s.instance));
        }
    rack_.publish(rackOrder_, how);
    for (int t = 0; t < sas::kModuleTypeCount; ++t)
        for (int k = 0; k < RackState::maxInstances(static_cast<sas::ModuleType>(t)); ++k) {
            const sas::SlotRef s{static_cast<sas::ModuleType>(t), k};
            if (!inOrder(s) && rack_.module(s)) rack_.removeModule(s);
        }
    rack_.collect();
}

void SasProcessor::setRackOrder(const sas::RackOrder& order) {
    rackOrder_ = RackState::fromTree(RackState::toTree(order));
    syncRack(sas::Transition::Immediate);
}

bool SasProcessor::canAdd(sas::ModuleType type) const {
    int modules = 0, instances = 0;
    for (int i = 0; i < rackOrder_.count; ++i) {
        if (rackOrder_.slots[std::size_t(i)].type != sas::ModuleType::Amp) ++modules;
        if (rackOrder_.slots[std::size_t(i)].type == type) ++instances;
    }
    // The two ends of a parallel come in with the parallel itself. A second amp or cabinet is a module of lane B: only while there is a parallel.
    const bool ends = type == sas::ModuleType::Splitter || type == sas::ModuleType::Mixer;
    const bool second = type == sas::ModuleType::Amp || type == sas::ModuleType::Cabinet;
    if (ends || (second && !hasParallel())) return false;
    return modules < RackState::kMaxModules && instances < RackState::maxInstances(type);
}

// The entries of the order move together with their lane.
void SasProcessor::insertEntry(int at, sas::SlotRef slot, int lane) {
    for (int i = rackOrder_.count; i > at; --i) {
        rackOrder_.slots[std::size_t(i)] = rackOrder_.slots[std::size_t(i - 1)];
        rackOrder_.lane[std::size_t(i)] = rackOrder_.lane[std::size_t(i - 1)];
    }
    rackOrder_.slots[std::size_t(at)] = slot;
    rackOrder_.lane[std::size_t(at)] = std::uint8_t(lane);
    ++rackOrder_.count;
}

void SasProcessor::eraseEntry(int at) {
    for (int i = at; i + 1 < rackOrder_.count; ++i) {
        rackOrder_.slots[std::size_t(i)] = rackOrder_.slots[std::size_t(i + 1)];
        rackOrder_.lane[std::size_t(i)] = rackOrder_.lane[std::size_t(i + 1)];
    }
    --rackOrder_.count;
}

int SasProcessor::indexInOrder(sas::SlotRef slot) const {
    for (int i = 0; i < rackOrder_.count; ++i)
        if (rackOrder_.slots[std::size_t(i)] == slot) return i;
    return -1;
}

void SasProcessor::normalizeLanes() {
    const int s = rackOrder_.splitterIndex(), m = rackOrder_.mixerIndex();
    for (int i = 0; i < rackOrder_.count; ++i)
        if (s < 0 || m < 0 || i <= s || i >= m) rackOrder_.lane[std::size_t(i)] = 0;
}

bool SasProcessor::layoutOk() const {
    using T = sas::ModuleType;
    if (!rackOrder_.valid()) return false;
    const int a1 = indexInOrder({T::Amp, 1}), c1 = indexInOrder({T::Cabinet, 1});
    if (!hasParallel()) return a1 < 0 && c1 < 0;
    // The second amp and cabinet, if they are there, are in lane B; the first ones are in the shared part or in lane A.
    if ((a1 >= 0 && rackOrder_.lane[std::size_t(a1)] != 1) || (c1 >= 0 && rackOrder_.lane[std::size_t(c1)] != 1)) return false;
    for (const auto fixed : {sas::SlotRef{T::Amp, 0}, sas::SlotRef{T::Cabinet, 0}}) {
        const int i = indexInOrder(fixed);
        if (i < 0 || rackOrder_.lane[std::size_t(i)] != 0) return false;
    }
    return true;
}

bool SasProcessor::addModule(sas::ModuleType type, int insertAt, int wantedLane) {
    if (!canAdd(type)) return false;
    int instance = 0;
    for (; instance < RackState::maxInstances(type); ++instance)
        if (indexInOrder({type, instance}) < 0) break;
    const sas::SlotRef slot{type, instance};
    const int ampAt = std::max(0, indexInOrder({sas::ModuleType::Amp, 0}));
    if (insertAt < 0) {
        using T = sas::ModuleType;
        switch (type) {
            case T::Overdrive: insertAt = ampAt; break;  // next to the amp, behind a compressor
            case T::Compressor:  // in front of the overdrives that sit next to the amp, so the amp can run both as it always did
                insertAt = ampAt;
                while (insertAt > 0 && rackOrder_.slots[std::size_t(insertAt - 1)].type == T::Overdrive) --insertAt;
                break;
            case T::Gate: case T::NamPedal: case T::Pitch: case T::Wah: case T::Fuzz: {
                // In front of the compressor and the overdrive that sit next to the amp, so the amp keeps running them as it always did.
                insertAt = ampAt;
                while (insertAt > 0 && (rackOrder_.slots[std::size_t(insertAt - 1)].type == T::Overdrive || rackOrder_.slots[std::size_t(insertAt - 1)].type == T::Compressor)) --insertAt;
                break;
            }
            case T::Amp: {  // the second amp: at the end of lane B, in front of its cabinet if it has one
                const int cabB = indexInOrder({T::Cabinet, 1});
                insertAt = cabB >= 0 ? cabB : rackOrder_.mixerIndex();
                break;
            }
            case T::Cabinet: {  // the second cabinet: behind the second amp, or at the end of lane B
                const int ampB = indexInOrder({T::Amp, 1});
                insertAt = ampB >= 0 ? ampB + 1 : rackOrder_.mixerIndex();
                break;
            }
            default: insertAt = rackOrder_.count; break;
        }
    }
    insertAt = juce::jlimit(0, rackOrder_.count, insertAt);
    // Inside the parallel a new module joins the lane of the one before it.
    const int s = rackOrder_.splitterIndex(), m = rackOrder_.mixerIndex();
    int lane = (s >= 0 && insertAt > s + 1 && insertAt <= m) ? rackOrder_.lane[std::size_t(insertAt - 1)] : 0;
    if (wantedLane == 1 && !hasParallel()) return false;
    if (type == sas::ModuleType::Amp || type == sas::ModuleType::Cabinet || wantedLane == 1) {  // the second amp and cabinet are always in lane B, inside the parallel
        lane = 1;
        insertAt = juce::jlimit(s + 1, m, insertAt);
    }
    const auto saved = rackOrder_;
    insertEntry(insertAt, slot, lane);
    normalizeLanes();
    if (!layoutOk()) {
        rackOrder_ = saved;
        return false;
    }
    RackState::resetParams(apvts, slot);
    if (slot == sas::SlotRef{sas::ModuleType::Amp, 1}) {
        // The second amp starts as a copy of the first one's choice, so the sum is at once a doubled amp.
        if (auto* p = apvts.getParameter(RackState::paramId(ids::amp, 1))) {
            p->beginChangeGesture();
            p->setValueNotifyingHost(apvts.getParameter(ids::amp)->getValue());
            p->endChangeGesture();
        }
    }
    if (slot == sas::SlotRef{sas::ModuleType::Cabinet, 1}) {  // and its cabinet with the default impulse
        clearImpulseResponse(0, 1);
        clearImpulseResponse(1, 1);
        loadBuiltInImpulseResponse(firstCab_, 0, 1);
    }
    syncRack(sas::Transition::Duck);
    return true;
}

bool SasProcessor::removeModule(sas::SlotRef slot) {
    if (slot.type == sas::ModuleType::Splitter || slot.type == sas::ModuleType::Mixer) return removeParallel();  // the two ends go together
    if (RackState::isFixed(slot)) return false;  // the amps, the cabinets and the first gate are always there
    const int at = indexInOrder(slot);
    if (at < 0) return false;
    eraseEntry(at);
    syncRack(sas::Transition::Duck);
    if (slot.type == sas::ModuleType::NamPedal) clearNamPedal(slot.instance);  // its model goes with it, so it is not kept in the project
    if (slot == sas::SlotRef{sas::ModuleType::Cabinet, 1}) {                     // and so do the second cabinet's impulses
        clearImpulseResponse(0, 1);
        clearImpulseResponse(1, 1);
    }
    return true;
}

bool SasProcessor::moveModule(sas::SlotRef slot, int newIndex) {
    if (slot.type == sas::ModuleType::Splitter || slot.type == sas::ModuleType::Mixer) return false;  // the ends of the parallel stay where they are
    const int at = indexInOrder(slot);
    if (at < 0) return false;
    const auto saved = rackOrder_;
    const int lane = rackOrder_.lane[std::size_t(at)];
    eraseEntry(at);
    newIndex = juce::jlimit(0, rackOrder_.count, newIndex);
    insertEntry(newIndex, slot, lane);
    normalizeLanes();
    if (!layoutOk()) {
        rackOrder_ = saved;
        return false;
    }
    syncRack(sas::Transition::Duck);
    return true;
}

bool SasProcessor::moveModuleToLane(sas::SlotRef slot, int lane, int newIndex) {
    if (!hasParallel() || (lane != 0 && lane != 1)) return false;
    if (slot.type == sas::ModuleType::Splitter || slot.type == sas::ModuleType::Mixer || slot.type == sas::ModuleType::Amp || slot.type == sas::ModuleType::Cabinet) return false;
    const int at = indexInOrder(slot);
    if (at < 0) return false;
    const auto saved = rackOrder_;
    eraseEntry(at);
    newIndex = juce::jlimit(0, rackOrder_.count, newIndex);
    insertEntry(newIndex, slot, lane);
    const int s = rackOrder_.splitterIndex(), m = rackOrder_.mixerIndex();
    if (newIndex <= s || newIndex > m) {  // outside the parallel there are no lanes
        rackOrder_ = saved;
        return false;
    }
    normalizeLanes();
    if (!layoutOk()) {
        rackOrder_ = saved;
        return false;
    }
    syncRack(sas::Transition::Duck);
    return true;
}

bool SasProcessor::canAddParallel() const {
    int modules = 0;
    for (int i = 0; i < rackOrder_.count; ++i)
        if (rackOrder_.slots[std::size_t(i)].type != sas::ModuleType::Amp) ++modules;
    return !hasParallel() && modules + 2 <= RackState::kMaxModules;  // the splitter and the mixer
}

bool SasProcessor::addParallel() {
    using T = sas::ModuleType;
    if (!canAddParallel()) return false;
    const int ampAt = indexInOrder({T::Amp, 0}), cabAt = indexInOrder({T::Cabinet, 0});
    if (ampAt < 0 || cabAt < 0) return false;
    // The splitter goes in front of the amp and of the compressor and overdrive that sit next to it (they keep working inside the amp, in lane A);
    // the mixer goes behind the cabinet. Lane B starts empty: what goes in it is the player's choice (a second amp and cabinet, a reverb on its own...).
    int start = ampAt;
    while (start > 0 && (rackOrder_.slots[std::size_t(start - 1)].type == T::Overdrive || rackOrder_.slots[std::size_t(start - 1)].type == T::Compressor)) --start;
    const int end = std::max(ampAt, cabAt);
    RackState::resetParams(apvts, {T::Mixer, 0});
    const auto saved = rackOrder_;
    insertEntry(end + 1, {T::Mixer, 0}, 0);
    insertEntry(start, {T::Splitter, 0}, 0);
    if (!layoutOk()) {
        rackOrder_ = saved;
        return false;
    }
    syncRack(sas::Transition::Duck);
    return true;
}

bool SasProcessor::removeParallel() {
    using T = sas::ModuleType;
    if (!hasParallel()) return false;
    std::vector<sas::SlotRef> gone;
    for (int i = rackOrder_.count - 1; i >= 0; --i) {
        const auto slot = rackOrder_.slots[std::size_t(i)];
        if (slot.type == T::Splitter || slot.type == T::Mixer || rackOrder_.lane[std::size_t(i)] != 0) {
            gone.push_back(slot);
            eraseEntry(i);
        }
    }
    syncRack(sas::Transition::Duck);
    for (const auto& slot : gone) {
        if (slot.type == T::NamPedal) clearNamPedal(slot.instance);
        RackState::resetParams(apvts, slot);
    }
    clearImpulseResponse(0, 1);
    clearImpulseResponse(1, 1);
    return true;
}

// ------------------------------------------------------------------ impulse response

// Magnitude response in dB (mean 0) at 96 log-spaced frequencies from 50 Hz to 10 kHz, each the average power in a 1/6 octave band.
static std::vector<float> responseCurve(const juce::AudioBuffer<float>& mono, double sampleRate, double& guitarBandPower) {
    constexpr int order = 15, points = 96;
    constexpr int fftSize = 1 << order;
    juce::dsp::FFT fft(order);
    std::vector<float> data(std::size_t(fftSize) * 2, 0.0f);
    const int n = std::min(mono.getNumSamples(), fftSize / 2);
    for (int i = 0; i < n; ++i) data[std::size_t(i)] = mono.getSample(0, i);
    fft.performFrequencyOnlyForwardTransform(data.data());
    {
        // Mean power of the spectrum over the guitar band (100 Hz to 5 kHz): the reference for the IR's level.
        const int lo = std::max(1, int(100.0 * fftSize / sampleRate)), hi = std::min(fftSize / 2 - 1, int(5000.0 * fftSize / sampleRate));
        double power = 0.0;
        for (int b = lo; b <= hi; ++b) power += double(data[std::size_t(b)]) * data[std::size_t(b)];
        guitarBandPower = hi >= lo ? power / double(hi - lo + 1) : 0.0;
    }
    std::vector<float> curve(points);
    double mean = 0.0;
    for (int k = 0; k < points; ++k) {
        const double f = 50.0 * std::pow(200.0, double(k) / double(points - 1));
        const double lo = f / std::pow(2.0, 1.0 / 12.0), hi = f * std::pow(2.0, 1.0 / 12.0);
        const int b0 = std::max(1, int(lo * fftSize / sampleRate)), b1 = std::min(fftSize / 2 - 1, std::max(b0, int(hi * fftSize / sampleRate)));
        double power = 0.0;
        for (int b = b0; b <= b1; ++b) power += double(data[std::size_t(b)]) * data[std::size_t(b)];
        curve[std::size_t(k)] = float(10.0 * std::log10(power / double(b1 - b0 + 1) + 1e-20));
        mean += curve[std::size_t(k)];
    }
    mean /= points;
    for (auto& v : curve) v -= float(mean);
    return curve;
}

bool SasProcessor::loadImpulseResponseFromMemory(const void* data, std::size_t size, const juce::String& name, int slot, int cabinet) {
    if (cabinet < 0 || cabinet > 1) return false;
    auto& cb = cabs_[cabinet];
    if (slot < 0 || slot >= kIrSlots) return false;
    // The file is decoded here and the samples are handed over by value: the memory overload of the JUCE convolution reads its
    // source later on a background thread, after a caller's temporary buffer is gone, and then silently loads nothing.
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(std::make_unique<juce::MemoryInputStream>(data, size, false)));
    if (!reader || reader->lengthInSamples <= 0 || reader->numChannels == 0 || reader->sampleRate <= 0.0) return false;
    // A cabinet is a few hundred milliseconds; anything longer is cut to keep the convolution cheap.
    const int length = int(std::min<juce::int64>(reader->lengthInSamples, juce::int64(reader->sampleRate * kMaxIrSeconds)));
    juce::AudioBuffer<float> all(int(reader->numChannels), length);
    reader->read(&all, 0, length, 0, true, true);
    juce::AudioBuffer<float> mono(1, length);
    mono.copyFrom(0, 0, all, 0, 0, length);  // first channel: the amp is mono
    double energy = 0.0;
    for (int i = 0; i < length; ++i) energy += double(mono.getSample(0, i)) * double(mono.getSample(0, i));
    if (!(energy > 0.0)) return false;
    // Level: unity gain over the guitar band (100 Hz to 5 kHz), so switching from the cabinet filter to an impulse does not jump in
    // volume. (Equal total energy would not do: a cabinet has almost nothing above 5 kHz, so its energy sits in the guitar band
    // and it would come out 9 to 14 dB louder. JUCE's own normalisation is about 20 dB lower.)
    double bandPower = 0.0;
    auto curve = responseCurve(mono, reader->sampleRate, bandPower);
    const double gain = bandPower > 0.0 ? 1.0 / std::sqrt(bandPower) : 1.0 / std::sqrt(energy);
    mono.applyGain(float(gain));
    cb.ir[slot].conv.loadImpulseResponse(std::move(mono), reader->sampleRate, juce::dsp::Convolution::Stereo::no, juce::dsp::Convolution::Trim::yes,
                                       juce::dsp::Convolution::Normalise::no);
    {
        const juce::ScopedLock lock(irLock_);
        cb.ir[slot].bytes = juce::MemoryBlock(data, size);
        cb.ir[slot].name = name;
        cb.ir[slot].curve = std::move(curve);
    }
    cb.ir[slot].loaded = true;
    return true;
}

// ------------------------------------------------------------------ NAM model

namespace {
constexpr std::size_t kMaxStoredNamBytes = 16 * 1024 * 1024;
}

bool SasProcessor::loadNamFromMemory(const void* data, std::size_t size, const juce::String& name) {
    const double fs = sampleRate_ > 0.0 ? sampleRate_ : 48000.0;
    std::string error;
    auto model = sas::NamModel::loadFromMemory(data, size, name.toStdString(), fs, 512, error);
    const juce::ScopedLock lock(namLock_);
    if (!model) {
        namMessage_ = error.empty() ? juce::String("the file could not be loaded") : juce::String(error);
        return false;
    }
    namPrev_ = std::move(nam_);
    nam_ = model;
    namBytes_ = juce::MemoryBlock(data, size);
    namName_ = name;
    namRate_ = fs;
    namMessage_ = model->rateMatches()
                      ? juce::String()
                      : "Trained at " + juce::String(model->trainedRate(), 0) + " Hz, played at " + juce::String(fs, 0) + " Hz: it will sound off-pitch.";
    namActive_.store(model.get());
    return true;
}

bool SasProcessor::loadNamModel(const juce::File& file) {
    if (!file.existsAsFile() || file.getSize() > juce::int64(kMaxStoredNamBytes)) {
        const juce::ScopedLock lock(namLock_);
        namMessage_ = file.existsAsFile() ? "the file is too big" : "the file does not exist";
        return false;
    }
    juce::MemoryBlock bytes;
    if (!file.loadFileAsData(bytes)) {
        const juce::ScopedLock lock(namLock_);
        namMessage_ = "the file could not be read";
        return false;
    }
    return loadNamFromMemory(bytes.getData(), bytes.getSize(), file.getFileNameWithoutExtension());
}

void SasProcessor::clearNamModel() {
    const juce::ScopedLock lock(namLock_);
    namActive_.store(nullptr);
    namPrev_ = std::move(nam_);
    nam_.reset();
    namBytes_.reset();
    namName_.clear();
    namMessage_.clear();
}

juce::String SasProcessor::namModelName() const {
    const juce::ScopedLock lock(namLock_);
    return namName_;
}

juce::String SasProcessor::namModelMessage() const {
    const juce::ScopedLock lock(namLock_);
    return namMessage_;
}

bool SasProcessor::loadNamPedalFromMemory(const void* data, std::size_t size, const juce::String& name, int instance) {
    if (instance < 0 || instance >= kNamPedalSlots) return false;
    auto& slot = namPedals_[instance];
    const double fs = sampleRate_ > 0.0 ? sampleRate_ : 48000.0;
    std::string error;
    auto model = sas::NamModel::loadFromMemory(data, size, name.toStdString(), fs, int(kMaxChunk), error);
    const juce::ScopedLock lock(namLock_);
    if (!model) {
        slot.message = error.empty() ? juce::String("the file could not be loaded") : juce::String(error);
        return false;
    }
    slot.prev = std::move(slot.model);
    slot.model = model;
    slot.bytes = juce::MemoryBlock(data, size);
    slot.name = name;
    slot.rate = fs;
    slot.message = model->rateMatches()
                       ? juce::String()
                       : "Trained at " + juce::String(model->trainedRate(), 0) + " Hz, played at " + juce::String(fs, 0) + " Hz: it will sound off-pitch.";
    slot.active.store(model.get());
    return true;
}

bool SasProcessor::loadNamPedal(const juce::File& file, int instance) {
    if (instance < 0 || instance >= kNamPedalSlots) return false;
    auto& slot = namPedals_[instance];
    if (!file.existsAsFile() || file.getSize() > juce::int64(kMaxStoredNamBytes)) {
        const juce::ScopedLock lock(namLock_);
        slot.message = file.existsAsFile() ? "the file is too big" : "the file does not exist";
        return false;
    }
    juce::MemoryBlock bytes;
    if (!file.loadFileAsData(bytes)) {
        const juce::ScopedLock lock(namLock_);
        slot.message = "the file could not be read";
        return false;
    }
    return loadNamPedalFromMemory(bytes.getData(), bytes.getSize(), file.getFileNameWithoutExtension(), instance);
}

void SasProcessor::clearNamPedal(int instance) {
    if (instance < 0 || instance >= kNamPedalSlots) return;
    auto& slot = namPedals_[instance];
    const juce::ScopedLock lock(namLock_);
    slot.active.store(nullptr);
    slot.prev = std::move(slot.model);
    slot.model.reset();
    slot.bytes.reset();
    slot.name.clear();
    slot.message.clear();
}

juce::String SasProcessor::namPedalName(int instance) const {
    const juce::ScopedLock lock(namLock_);
    return namPedals_[instance].name;
}

juce::String SasProcessor::namPedalMessage(int instance) const {
    const juce::ScopedLock lock(namLock_);
    return namPedals_[instance].message;
}

bool SasProcessor::loadNamCabFromMemory(const void* data, std::size_t size, const juce::String& name) {
    const double fs = sampleRate_ > 0.0 ? sampleRate_ : 48000.0;
    std::string error;
    auto model = sas::NamModel::loadFromMemory(data, size, name.toStdString(), fs, int(kMaxChunk), error);
    const juce::ScopedLock lock(namLock_);
    if (!model) {
        namCabMessage_ = error.empty() ? juce::String("the file could not be loaded") : juce::String(error);
        return false;
    }
    namCabPrev_ = std::move(namCab_);
    namCab_ = model;
    namCabBytes_ = juce::MemoryBlock(data, size);
    namCabName_ = name;
    namCabRate_ = fs;
    namCabMessage_ = model->rateMatches()
                         ? juce::String()
                         : "Trained at " + juce::String(model->trainedRate(), 0) + " Hz, played at " + juce::String(fs, 0) + " Hz: it will sound off-pitch.";
    namCabActive_.store(model.get());
    return true;
}

bool SasProcessor::loadNamCab(const juce::File& file) {
    if (!file.existsAsFile() || file.getSize() > juce::int64(kMaxStoredNamBytes)) {
        const juce::ScopedLock lock(namLock_);
        namCabMessage_ = file.existsAsFile() ? "the file is too big" : "the file does not exist";
        return false;
    }
    juce::MemoryBlock bytes;
    if (!file.loadFileAsData(bytes)) {
        const juce::ScopedLock lock(namLock_);
        namCabMessage_ = "the file could not be read";
        return false;
    }
    return loadNamCabFromMemory(bytes.getData(), bytes.getSize(), file.getFileNameWithoutExtension());
}

void SasProcessor::clearNamCab() {
    const juce::ScopedLock lock(namLock_);
    namCabActive_.store(nullptr);
    namCabPrev_ = std::move(namCab_);
    namCab_.reset();
    namCabBytes_.reset();
    namCabName_.clear();
    namCabMessage_.clear();
}

juce::String SasProcessor::namCabName() const {
    const juce::ScopedLock lock(namLock_);
    return namCabName_;
}

juce::String SasProcessor::namCabMessage() const {
    const juce::ScopedLock lock(namLock_);
    return namCabMessage_;
}

int SasProcessor::builtInImpulseResponseCount() { return CabData::namedResourceListSize; }

juce::String SasProcessor::builtInImpulseResponseName(int index) {
    if (index < 0 || index >= CabData::namedResourceListSize) return {};
    // "cab_4x12_os_mix1_wav" -> "4x12 OS Mix 1"
    juce::String n = juce::String(CabData::originalFilenames[index]).upToLastOccurrenceOf(".", false, false);
    n = n.fromFirstOccurrenceOf("cab_", false, false).replaceCharacter('_', ' ');
    juce::StringArray words = juce::StringArray::fromTokens(n, " ", "");
    for (auto& w : words) {
        if (w.startsWith("mix") && w.length() > 3) w = "Mix " + w.substring(3);
        else if (w.length() <= 2 && !w.containsAnyOf("0123456789")) w = w.toUpperCase();
        else if (!w.containsAnyOf("0123456789")) w = w.substring(0, 1).toUpperCase() + w.substring(1);
    }
    return words.joinIntoString(" ");
}

bool SasProcessor::loadBuiltInImpulseResponse(int index, int slot, int cabinet) {
    if (index < 0 || index >= CabData::namedResourceListSize) return false;
    int size = 0;
    const char* data = CabData::getNamedResource(CabData::namedResourceList[index], size);
    return data != nullptr && loadImpulseResponseFromMemory(data, std::size_t(size), builtInImpulseResponseName(index), slot, cabinet);
}

bool SasProcessor::loadImpulseResponse(const juce::File& file, int slot, int cabinet) {
    juce::MemoryBlock bytes;
    if (!file.existsAsFile() || !file.loadFileAsData(bytes) || bytes.getSize() == 0) return false;
    return loadImpulseResponseFromMemory(bytes.getData(), bytes.getSize(), file.getFileName(), slot, cabinet);
}

void SasProcessor::clearImpulseResponse(int slot, int cabinet) {
    if (cabinet < 0 || cabinet > 1) return;
    auto& cb = cabs_[cabinet];
    if (slot < 0 || slot >= kIrSlots) return;
    cb.ir[slot].loaded = false;
    const juce::ScopedLock lock(irLock_);
    cb.ir[slot].bytes.reset();
    cb.ir[slot].name.clear();
    cb.ir[slot].curve.clear();
}

juce::String SasProcessor::impulseResponseName(int slot, int cabinet) const {
    const auto& cb = cabs_[juce::jlimit(0, 1, cabinet)];
    const juce::ScopedLock lock(irLock_);
    return slot >= 0 && slot < kIrSlots ? cb.ir[slot].name : juce::String();
}

std::vector<float> SasProcessor::impulseResponseCurve(int slot, int cabinet) const {
    const auto& cb = cabs_[juce::jlimit(0, 1, cabinet)];
    const juce::ScopedLock lock(irLock_);
    return slot >= 0 && slot < kIrSlots ? cb.ir[slot].curve : std::vector<float>{};
}

// ------------------------------------------------------------------ state

// Slot A keeps the property names of the first versions ("irData" / "irName"), so older projects still load.
// The NAM pedal of the first instance keeps the property names of the first versions; the others add their number.
static juce::String namPedalKey(const char* base, int instance) { return instance == 0 ? juce::String(base) : juce::String(base) + juce::String(instance + 1); }

// The first cabinet keeps the property names of the first versions; the second one adds "C2".
static juce::String irDataKey(int cabinet, int slot) { return juce::String(slot == 0 ? "irData" : "irDataB") + (cabinet == 0 ? "" : "C2"); }
static juce::String irNameKey(int cabinet, int slot) { return juce::String(slot == 0 ? "irName" : "irNameB") + (cabinet == 0 ? "" : "C2"); }

void SasProcessor::getStateInformation(juce::MemoryBlock& dest) {
    auto state = apvts.copyState();
    {
        const juce::ScopedLock lock(irLock_);
        for (int cabinet = 0; cabinet < 2; ++cabinet)
            for (int s = 0; s < kIrSlots; ++s) {
                const auto& cb = cabs_[cabinet];
            state.removeProperty(irDataKey(cabinet, s), nullptr);
            state.removeProperty(irNameKey(cabinet, s), nullptr);
            if (cb.ir[s].bytes.getSize() > 0 && cb.ir[s].bytes.getSize() <= kMaxStoredIrBytes) {
                state.setProperty(irNameKey(cabinet, s), cb.ir[s].name, nullptr);
                state.setProperty(irDataKey(cabinet, s), juce::Base64::toBase64(cb.ir[s].bytes.getData(), cb.ir[s].bytes.getSize()), nullptr);
            }
        }
    }
    while (state.getChildWithName("RACK").isValid()) state.removeChild(state.getChildWithName("RACK"), nullptr);
    state.appendChild(RackState::toTree(rackOrder_), nullptr);
    state.setProperty("rackVersion", RackState::kRackVersion, nullptr);
    state.removeProperty("namData", nullptr);
    state.removeProperty("namName", nullptr);
    state.removeProperty("namCabData", nullptr);
    state.removeProperty("namCabName", nullptr);
    for (int i = 0; i < kNamPedalSlots; ++i) {
        state.removeProperty(namPedalKey("namPedalData", i), nullptr);
        state.removeProperty(namPedalKey("namPedalName", i), nullptr);
    }
    {
        const juce::ScopedLock lock(namLock_);
        for (int i = 0; i < kNamPedalSlots; ++i) {
            const auto& slot = namPedals_[i];
            if (slot.bytes.getSize() > 0 && slot.bytes.getSize() <= kMaxStoredNamBytes) {
                state.setProperty(namPedalKey("namPedalName", i), slot.name, nullptr);
                state.setProperty(namPedalKey("namPedalData", i), juce::Base64::toBase64(slot.bytes.getData(), slot.bytes.getSize()), nullptr);
            }
        }
    }
    {
        const juce::ScopedLock lock(namLock_);
        if (namCabBytes_.getSize() > 0 && namCabBytes_.getSize() <= kMaxStoredNamBytes) {
            state.setProperty("namCabName", namCabName_, nullptr);
            state.setProperty("namCabData", juce::Base64::toBase64(namCabBytes_.getData(), namCabBytes_.getSize()), nullptr);
        }
    }
    {
        const juce::ScopedLock lock(namLock_);
        if (namBytes_.getSize() > 0 && namBytes_.getSize() <= kMaxStoredNamBytes) {
            state.setProperty("namName", namName_, nullptr);
            state.setProperty("namData", juce::Base64::toBase64(namBytes_.getData(), namBytes_.getSize()), nullptr);
        }
    }
    if (auto xml = state.createXml()) copyXmlToBinary(*xml, dest);
}

void SasProcessor::setStateInformation(const void* data, int size) {
    std::unique_ptr<juce::XmlElement> xml(getXmlFromBinary(data, size));
    if (!xml || !xml->hasTagName(apvts.state.getType())) return;
    const auto tree = juce::ValueTree::fromXml(*xml);
    apvts.replaceState(tree);
    // A state from before the rack has no order: it comes from the old parameters.
    if (const auto rack = tree.getChildWithName("RACK"); rack.isValid()) rackOrder_ = RackState::fromTree(rack);
    else rackOrder_ = RackState::fromLegacy(apvts, tree.hasProperty("namPedalData"));
    syncRack(sas::Transition::Immediate);
    for (int cabinet = 0; cabinet < 2; ++cabinet)
        for (int s = 0; s < kIrSlots; ++s) {
            if (tree.hasProperty(irDataKey(cabinet, s))) {
                juce::MemoryOutputStream out;
                if (juce::Base64::convertFromBase64(out, tree[irDataKey(cabinet, s)].toString()) && out.getDataSize() > 0)
                    if (!loadImpulseResponseFromMemory(out.getData(), out.getDataSize(), tree[irNameKey(cabinet, s)].toString(), s, cabinet)) clearImpulseResponse(s, cabinet);
            } else {
                clearImpulseResponse(s, cabinet);
            }
        }
    if (tree.hasProperty("namData")) {
        juce::MemoryOutputStream out;
        if (juce::Base64::convertFromBase64(out, tree["namData"].toString()) && out.getDataSize() > 0)
            if (!loadNamFromMemory(out.getData(), out.getDataSize(), tree["namName"].toString())) clearNamModel();
    } else {
        clearNamModel();
    }
    for (int i = 0; i < kNamPedalSlots; ++i) {
        if (tree.hasProperty(namPedalKey("namPedalData", i))) {
            juce::MemoryOutputStream out;
            if (juce::Base64::convertFromBase64(out, tree[namPedalKey("namPedalData", i)].toString()) && out.getDataSize() > 0)
                if (!loadNamPedalFromMemory(out.getData(), out.getDataSize(), tree[namPedalKey("namPedalName", i)].toString(), i)) clearNamPedal(i);
        } else {
            clearNamPedal(i);
        }
    }
    if (tree.hasProperty("namCabData")) {
        juce::MemoryOutputStream out;
        if (juce::Base64::convertFromBase64(out, tree["namCabData"].toString()) && out.getDataSize() > 0)
            if (!loadNamCabFromMemory(out.getData(), out.getDataSize(), tree["namCabName"].toString())) clearNamCab();
    } else {
        clearNamCab();
    }
}

void SasProcessor::applyEqPreset(int index, int instance) {
    if (index < 0 || index >= kEqPresetCount) return;
    const EqPreset& p = kEqPresets[index];
    auto set = [this, instance](const char* id, double value) {
        if (auto* q = apvts.getParameter(RackState::paramId(id, instance))) {
            q->beginChangeGesture();
            q->setValueNotifyingHost(q->convertTo0to1(float(value)));
            q->endChangeGesture();
        }
    };
    set(ids::eqOn, 1.0);
    set(ids::eqHpf, p.hpfHz);
    set(ids::eqLpf, p.lpfHz);
    for (int b = 0; b < 5; ++b) {
        set(ids::eqFreq[b], p.band[std::size_t(b)].freq);
        set(ids::eqGain[b], p.band[std::size_t(b)].gainDb);
        set(ids::eqQ[b], p.band[std::size_t(b)].q);
    }
}

void SasProcessor::applyFuzzPreset(int index, int instance) {
    if (index < 0 || index >= kFuzzPresetCount) return;
    const FuzzPreset& p = kFuzzPresets[index];
    auto set = [this, instance](const char* id, double value) {
        if (auto* q = apvts.getParameter(RackState::paramId(id, instance))) {
            q->beginChangeGesture();
            q->setValueNotifyingHost(q->convertTo0to1(float(value)));
            q->endChangeGesture();
        }
    };
    set(ids::fzOn, p.on ? 1.0 : 0.0);
    if (!p.on) return;  // "Off" only switches it off, so the knobs keep what the player had set
    set(ids::fzType, p.type);
    set(ids::fzFuzz, p.fuzz);
    set(ids::fzTone, p.tone);
    set(ids::fzBias, p.bias);
    set(ids::fzMix, p.mix);
    set(ids::fzLevel, p.levelDb);
}

void SasProcessor::applyWahPreset(int index, int instance) {
    if (index < 0 || index >= kWahPresetCount) return;
    const WahPreset& p = kWahPresets[index];
    auto set = [this, instance](const char* id, double value) {
        if (auto* q = apvts.getParameter(RackState::paramId(id, instance))) {
            q->beginChangeGesture();
            q->setValueNotifyingHost(q->convertTo0to1(float(value)));
            q->endChangeGesture();
        }
    };
    set(ids::wahOn, p.on ? 1.0 : 0.0);
    if (!p.on) return;  // "Off" only switches it off, so the knobs keep what the player had set
    set(ids::wahMode, p.mode);
    set(ids::wahPos, p.position);
    set(ids::wahHeel, p.heelHz);
    set(ids::wahToe, p.toeHz);
    set(ids::wahQ, p.q);
    set(ids::wahMix, p.mix);
    set(ids::wahLevel, p.levelDb);
    set(ids::wahSens, p.sensitivity);
    set(ids::wahAttack, p.attackMs);
    set(ids::wahRelease, p.releaseMs);
    set(ids::wahDir, p.down ? 1.0 : 0.0);
    set(ids::wahRate, p.rateHz);
    set(ids::wahStereo, p.stereo ? 1.0 : 0.0);
}

void SasProcessor::applyDelayPreset(int index, int instance) {
    if (index < 0 || index >= kDelayPresetCount) return;
    const DelayPreset& p = kDelayPresets[index];
    auto set = [this, instance](const char* id, double value) {
        if (auto* q = apvts.getParameter(RackState::paramId(id, instance))) {
            q->beginChangeGesture();
            q->setValueNotifyingHost(q->convertTo0to1(float(value)));
            q->endChangeGesture();
        }
    };
    set(ids::dlyOn, p.on ? 1.0 : 0.0);
    if (!p.on) return;  // "Off" only switches it off, so the knobs keep what the player had set
    set(ids::dlySync, 0.0);
    set(ids::dlyTime, p.timeMs);
    set(ids::dlyFeedback, p.feedback);
    set(ids::dlyMix, p.mix);
    set(ids::dlyTone, p.toneHz);
    set(ids::dlyPing, p.pingPong ? 1.0 : 0.0);
    set(ids::dlyAnalog, p.analog ? 1.0 : 0.0);
}

void SasProcessor::applyReverbPreset(int index, int instance) {
    if (index < 0 || index >= kReverbPresetCount) return;
    const ReverbPreset& p = kReverbPresets[index];
    auto set = [this, instance](const char* id, double value) {
        if (auto* q = apvts.getParameter(RackState::paramId(id, instance))) {
            q->beginChangeGesture();
            q->setValueNotifyingHost(q->convertTo0to1(float(value)));
            q->endChangeGesture();
        }
    };
    set(ids::plOn, p.on ? 1.0 : 0.0);
    if (!p.on) return;
    set(ids::plHall, p.hall ? 1.0 : 0.0);
    set(ids::plDecay, p.decaySec);
    set(ids::plPre, p.preMs);
    set(ids::plTone, p.toneHz);
    set(ids::plLow, p.lowCutHz);
    set(ids::plMix, p.mix);
    set(ids::plMod, p.mod);
    set(ids::plShimmer, p.shimmer);
    set(ids::plShimmerInt, p.shimmerFifth ? 1.0 : 0.0);
}

juce::File SasProcessor::presetFolder() {
    return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory).getChildFile("Schematic Amp Sim").getChildFile("Presets");
}

bool SasProcessor::savePreset(const juce::File& file) {
    juce::MemoryBlock state;
    getStateInformation(state);
    const std::unique_ptr<juce::XmlElement> xml(getXmlFromBinary(state.getData(), int(state.getSize())));
    if (!xml) return false;
    file.getParentDirectory().createDirectory();
    return file.replaceWithText(xml->toString());
}

bool SasProcessor::loadPreset(const juce::File& file) {
    const std::unique_ptr<juce::XmlElement> xml(juce::XmlDocument::parse(file));
    if (!xml || !xml->hasTagName(apvts.state.getType())) return false;
    juce::MemoryBlock state;
    copyXmlToBinary(*xml, state);
    setStateInformation(state.getData(), int(state.getSize()));
    return true;
}

juce::AudioProcessorEditor* SasProcessor::createEditor() { return new SasEditor(*this); }

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new SasProcessor(); }
