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
        juce::ParameterID{ids::quality, kVersion}, "Quality", juce::StringArray{"Normal (2x)", "High (4x)", "Lite (2x)", "Eco (2x)", "Efficient (2x/1x)"}, 0));
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
    RackState::addExtraInstances(l);
    return l;
}

// ------------------------------------------------------------------ amp switching

// Builds the circuits for another amp off the audio thread. At most one build runs at a time and
// a newer request replaces an older one that has not started yet.
class SasProcessor::ChainBuilder final : public juce::Thread {
public:
    explicit ChainBuilder(SasProcessor& p) : juce::Thread("SAS chain builder"), owner_(p) { startThread(juce::Thread::Priority::low); }
    ~ChainBuilder() override { stopThread(5000); }

    void wake() { notify(); }

    void run() override {
        while (!threadShouldExit()) {
            wait(-1);
            if (threadShouldExit()) break;
            delete owner_.retired_.exchange(nullptr);  // chains the audio thread has swapped out
            const int key = owner_.requestedKey_.exchange(-1);
            const int amp = key & 31;
            if (key < 0 || amp >= int(owner_.defs_.size()) || owner_.sampleRate_ <= 0.0) continue;
            try {
                const sas::Condition cond{(key >> 5) & 7, (key >> 8) & 15, key >> 12};
                auto* built = new sas::Chain(sas::applyCondition(owner_.defs_[std::size_t(amp)], cond), owner_.sampleRate_);
                owner_.readyKey_.store(key);
                delete owner_.ready_.exchange(built);  // replaces a build nobody picked up
            } catch (const std::exception&) {
                // A broken amp definition leaves the current amp running.
            }
        }
    }

private:
    SasProcessor& owner_;
};

int SasProcessor::selectedAmpIndex() const {
    return juce::jlimit(0, int(defs_.size()) - 1, int(apvts.getRawParameterValue(ids::amp)->load() + 0.5f));
}

sas::Condition SasProcessor::wantedCondition() const {
    const auto& def = defs_[std::size_t(selectedAmpIndex())];
    sas::Condition c;
    const int tubes = int(def.power.tubes.size());
// The parameter holds a tube TYPE, not a position: every amp lists its tubes in its own order (stock first), so a position picked on
    // one amp would mean another tube on the next. An amp that does not offer the type keeps its own tubes.
    c.tube = 0;
    if (const int type = juce::jlimit(0, 3, int(apvts.getRawParameterValue(ids::tube)->load() + 0.5f)); type > 0 && tubes > 1)
        for (int k = 0; k < tubes; ++k)
            if (def.power.tubes[std::size_t(k)].name == ids::tubeTypes[type]) c.tube = k;
    c.wear = juce::jlimit(0, 10, int(apvts.getRawParameterValue(ids::wear)->load() + 0.5f));
    c.unit = juce::jlimit(0, 9, int(apvts.getRawParameterValue(ids::unit)->load() + 0.5f));
    return c;
}

int SasProcessor::wantedKey() const { return makeKey(selectedAmpIndex(), wantedCondition()); }

void SasProcessor::swapInBuiltChain() {
    sas::Chain* built = ready_.exchange(nullptr);
    if (!built) return;
    const int key = readyKey_.load();
    const int amp = key & 31;
    // Start the new circuits at the current knob positions instead of ramping from their defaults.
    activeAmp_ = amp;
    activeKey_ = key;
    if (auto* amp = ampModule()) {
        amp->setSettings(readSettings(anyIrLoaded()));
        amp->snapChain(*built);
        amp->setChain(built);
    } else {
        built->snap(readSettings(anyIrLoaded()));
    }
    sas::Chain* old = chain_.release();
    chain_.reset(built);
    // The builder frees it; if it has not yet freed the previous one, do it here (rare).
    delete retired_.exchange(old);
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

    // Resolve, per amp, which parameter drives each of its knobs (via its "ui" block).
    for (const auto& def : defs_) {
        AmpParams ap;
        ap.knobs.assign(def.preamp.knobs.size(), nullptr);
        ap.powerKnobs.assign(def.power.knobs.size(), nullptr);
        for (const auto& u : def.ui) {
            if (u.param == ids::presence) ap.presence = apvts.getRawParameterValue(u.param);
            if (u.param == ids::vibSpeed) ap.vibSpeed = apvts.getRawParameterValue(u.param);
            if (!u.knob.empty()) ap.knobs[def.knobIndex(u.knob)] = apvts.getRawParameterValue(u.param);
            if (!u.powerKnob.empty()) ap.powerKnobs[def.powerKnobIndex(u.powerKnob)] = apvts.getRawParameterValue(u.param);
        }
        if (!ap.presence) ap.presence = apvts.getRawParameterValue(ids::presence);
        if (!ap.vibSpeed) ap.vibSpeed = apvts.getRawParameterValue(ids::vibSpeed);
        ampParams_.push_back(std::move(ap));
    }
    settings_.knobs.reserve(16);  // never reallocate on the audio thread
    settings_.powerKnobs.reserve(16);
    mono_.resize(kMaxChunk);
    mono2_.resize(kMaxChunk);
    monoR_.resize(kMaxChunk);
    builder_ = std::make_unique<ChainBuilder>(*this);
    // A fresh instance has "4x12 OS Mix 1" ready in slot A (a restored project replaces it); the first of the list if that one is not there.
    int firstCab = 0;
    for (int i = 0; i < builtInImpulseResponseCount(); ++i)
        if (builtInImpulseResponseName(i) == "4x12 OS Mix 1") firstCab = i;
    loadBuiltInImpulseResponse(firstCab, 0);
    // The parameters of every module instance, found once (the audio thread reads them by pointer).
    for (int t = 0; t < sas::kModuleTypeCount; ++t)
        for (int k = 0; k < RackState::maxInstances(static_cast<sas::ModuleType>(t)); ++k)
            for (const char* base : RackState::paramIds(static_cast<sas::ModuleType>(t)))
                modParams_[std::size_t(t)][std::size_t(k)].push_back(apvts.getRawParameterValue(RackState::paramId(base, k)));
    rackOrder_ = RackState::defaultOrder();
    syncRack(sas::Transition::Immediate);
}

SasProcessor::~SasProcessor() {
    builder_.reset();  // joins the thread before the chains it may own go away
    delete ready_.exchange(nullptr);
    delete retired_.exchange(nullptr);
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
    const int wanted = wantedKey();
    if (!chain_ || sampleRate != sampleRate_ || wanted != activeKey_) {
        sampleRate_ = sampleRate;
        delete ready_.exchange(nullptr);
        chain_ = std::make_unique<sas::Chain>(sas::applyCondition(defs_[std::size_t(wanted & 31)], wantedCondition()), sampleRate);  // heavy: solves the DC points
        activeAmp_ = wanted & 31;
        activeKey_ = wanted;
    } else {
        chain_->reset();
    }
    if (auto* amp = ampModule()) amp->setChain(chain_.get());
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
    for (auto& slot : ir_) slot.conv.prepare(spec);
    irWasActive_ = false;
    latencyReported_ = -1;
    rack_.prepare(sampleRate, int(kMaxChunk));
    tuner_.prepare(sampleRate);
    double_.prepare(sampleRate);
}

sas::ChainSettings SasProcessor::readSettings(bool irAvailable) {
    auto& s = settings_;
    const auto& def = defs_[std::size_t(activeAmp_)];
    const auto& ap = ampParams_[std::size_t(activeAmp_)];
    s.knobs.resize(def.preamp.knobs.size());
    for (std::size_t i = 0; i < s.knobs.size(); ++i)
        s.knobs[i] = ap.knobs[i] ? double(ap.knobs[i]->load()) / 10.0 : def.preamp.knobs[i].def;
    auto raw = [this](const char* id) { return double(apvts.getRawParameterValue(id)->load()); };
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
    s.cabFilter = cab == CabMode::Filter || (cab == CabMode::Ir && !irAvailable) || (cab == CabMode::Nam && namCabActive_.load() == nullptr);
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
                static_cast<sas::ChorusModule*>(m)->setParams(q);
                break;
            }
            case T::Vibe: {
                sas::VibeParams q;
                q.on = p(ids::vbOn) > 0.5;
                q.speedHz = p(ids::vbSpeed);
                q.intensity = p(ids::vbIntensity) / 100.0;
                q.vibrato = p(ids::vbVibrato) > 0.5;
                static_cast<sas::VibeModule*>(m)->setParams(q);
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
                static_cast<sas::ReverbModule*>(m)->setParams(pl);
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
    if (!chain_ || numCh == 0) return;

    // Another amp was chosen: ask the builder, and swap in its circuits once they are ready. The
    // old amp keeps playing meanwhile.
    swapInBuiltChain();
    const int wanted = wantedKey();
    if (wanted != activeKey_ && requestedKey_.load() != wanted && ready_.load() == nullptr) {
        requestedKey_.store(wanted);
        builder_->wake();
    }

    const bool aLoaded = ir_[0].loaded.load(), bLoaded = ir_[1].loaded.load();
    const bool irLoaded = aLoaded || bLoaded;
    const auto cabParam = static_cast<CabMode>(juce::jlimit(0, 3, int(apvts.getRawParameterValue(ids::cab)->load() + 0.5f)));
    const bool irActive = cabParam == CabMode::Ir && irLoaded;
    sas::NamModel* const namCab = cabParam == CabMode::Nam ? namCabActive_.load() : nullptr;
    if (namCab && !namCabWasActive_) namCab->reset();
    namCabWasActive_ = namCab != nullptr;
    cabNamCab_ = namCab;
    cabIrActive_ = irActive;
    cabALoaded_ = aLoaded;
    cabBLoaded_ = bLoaded;
    // With no IR loaded the IR mode falls back to the cab filter instead of the raw amp (and so does a NAM cabinet that has no model).
    const bool cabFilterOn = cabParam == CabMode::Filter || (cabParam == CabMode::Ir && !irLoaded) || (cabParam == CabMode::Nam && namCab == nullptr);
    const double irLatency = irActive ? double(std::max(aLoaded ? ir_[0].conv.getLatency() : 0, bLoaded ? ir_[1].conv.getLatency() : 0)) : 0.0;
    if (auto* cab = static_cast<CabinetModule*>(rack_.audioModule({sas::ModuleType::Cabinet, 0}))) cab->setBlock(cabFilterOn, irLatency);
    if (auto* amp = ampModule()) amp->setSettings(readSettings(irLoaded));
    const sas::RackOrder order = rack_.order();
    applyModuleParams(order);

    // Latency follows the quality, the convolution and the pitch shifters; report it when it changes.
    const int latency = int(rack_.latencySamples());
    if (latency != latencyReported_) {
        latencyReported_ = latency;
        setLatencySamples(latency);
    }
    if (irActive && !irWasActive_)
        for (auto& slot : ir_) slot.conv.reset();
    irWasActive_ = irActive;

    const bool tunerOn = apvts.getRawParameterValue(ids::tunerOn)->load() > 0.5f;
    const bool tunerMuted = tunerOn && apvts.getRawParameterValue(ids::tunerMute)->load() > 0.5f;
    idleMa_.store(float(chain_->idleMilliamps()));
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
        chain_->applyOutputStereo(m, mR, std::size_t(n));
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
    if (cab.filterOn())
        for (int i = 0; i < n; ++i) m[i] = cab.filter().process(m[i]);
    if (cabNamCab_) {  // the NAM cabinet: through a scratch block, the model reads its input while it writes
        cabNamCab_->process(m, mono2_.data(), std::size_t(n));
        std::copy(mono2_.data(), mono2_.data() + n, m);
    }
    bool haveR = false;
    if (cabIrActive_) {
        const bool aLoaded = cabALoaded_, bLoaded = cabBLoaded_;
        auto convolve = [n](IrSlot& slot, float* data) {
            float* chans[1] = {data};
            juce::dsp::AudioBlock<float> block(chans, 1, std::size_t(n));
            slot.conv.process(juce::dsp::ProcessContextReplacing<float>(block));
        };
        if (aLoaded && bLoaded) {
            // Both slots: an equal-power blend, and the width that separates them: at 0 both ears get the blend, at 10 the left ear gets A and the
            // right ear B (two microphones, one on each side). Ramped over the block so a knob move does not click.
            const double theta = juce::jlimit(0.0, 1.0, double(apvts.getRawParameterValue(ids::irBlend)->load()) / 10.0) * (juce::MathConstants<double>::pi * 0.5);
            const float gA = float(std::cos(theta)), gB = float(std::sin(theta));
            const float width = juce::jlimit(0.0f, 1.0f, apvts.getRawParameterValue(ids::irWidth)->load() / 10.0f);
            float* m2 = mono2_.data();
            std::copy(m, m + n, m2);
            convolve(ir_[0], m);
            convolve(ir_[1], m2);
            for (int i = 0; i < n; ++i) {
                const float t = float(i + 1) / float(n);
                const float ga = blendGainA_ + (gA - blendGainA_) * t, gb = blendGainB_ + (gB - blendGainB_) * t;
                const float w = widthNow_ + (width - widthNow_) * t;
                const float a = m[i], b = m2[i];
                const float blend = a * ga + b * gb;
                m[i] = (1.0f - w) * blend + w * a;
                mR[i] = (1.0f - w) * blend + w * b;
            }
            blendGainA_ = gA;
            blendGainB_ = gB;
            widthNow_ = width;
            haveR = true;
        } else {
            convolve(aLoaded ? ir_[0] : ir_[1], m);
            blendGainA_ = aLoaded ? 1.0f : 0.0f;  // so a second slot loaded later fades in from this state
            blendGainB_ = aLoaded ? 0.0f : 1.0f;
        }
    }
    if (!haveR) std::copy(m, m + n, mR);
    // Automatic double tracking widens whatever the cabinet left (identical channels, or the A / B pair).
    double_.setAmount(double(apvts.getRawParameterValue(ids::doubleAmount)->load()) / 10.0,
                      8.0 + 22.0 * double(apvts.getRawParameterValue(ids::doubleTime)->load()) / 10.0);
    double_.process(m, mR, std::size_t(n));
}

void CabinetModule::process(float* l, float* r, std::size_t n) { owner_.processCabinet(*this, l, r, n); }

// ------------------------------------------------------------------ the rack

std::unique_ptr<sas::Module> SasProcessor::makeModule(sas::ModuleType type) {
    using T = sas::ModuleType;
    switch (type) {
        case T::Amp: {
            auto amp = std::make_unique<sas::AmpModule>();
            amp->setChain(chain_.get());
            return amp;
        }
        case T::Cabinet: return std::make_unique<CabinetModule>(*this);
        case T::Gate: return std::make_unique<sas::GateModule>();
        case T::Compressor: return std::make_unique<sas::CompressorModule>();
        case T::Overdrive: return std::make_unique<sas::OverdriveModule>();
        case T::NamPedal: return std::make_unique<sas::NamPedalModule>();
        case T::Phaser: return std::make_unique<sas::PhaserModule>();
        case T::Chorus: return std::make_unique<sas::ChorusModule>();
        case T::Vibe: return std::make_unique<sas::VibeModule>();
        case T::Pitch: return std::make_unique<sas::PitchModule>();
        case T::Eq: return std::make_unique<sas::EqModule>();
        case T::Delay: return std::make_unique<sas::DelayModule>();
        case T::Reverb: return std::make_unique<sas::ReverbModule>();
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
            if (inOrder(s) && !rack_.module(s)) rack_.setModule(s, makeModule(s.type));
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
    return type != sas::ModuleType::Amp && modules < RackState::kMaxModules && instances < RackState::maxInstances(type);
}

bool SasProcessor::addModule(sas::ModuleType type, int insertAt) {
    if (!canAdd(type)) return false;
    int instance = 0;
    for (; instance < RackState::maxInstances(type); ++instance) {
        bool used = false;
        for (int i = 0; i < rackOrder_.count; ++i) used = used || rackOrder_.slots[std::size_t(i)] == sas::SlotRef{type, instance};
        if (!used) break;
    }
    const sas::SlotRef slot{type, instance};
    int ampAt = 0;
    for (int i = 0; i < rackOrder_.count; ++i)
        if (rackOrder_.slots[std::size_t(i)].type == sas::ModuleType::Amp) ampAt = i;
    if (insertAt < 0) {
        using T = sas::ModuleType;
        switch (type) {
            case T::Overdrive: insertAt = ampAt; break;  // next to the amp, behind a compressor
            case T::Compressor:  // in front of the overdrives that sit next to the amp, so the amp can run both as it always did
                insertAt = ampAt;
                while (insertAt > 0 && rackOrder_.slots[std::size_t(insertAt - 1)].type == T::Overdrive) --insertAt;
                break;
            case T::Gate: case T::NamPedal: case T::Pitch: {
                // In front of the compressor and the overdrive that sit next to the amp, so the amp keeps running them as it always did.
                insertAt = ampAt;
                while (insertAt > 0 && (rackOrder_.slots[std::size_t(insertAt - 1)].type == T::Overdrive || rackOrder_.slots[std::size_t(insertAt - 1)].type == T::Compressor)) --insertAt;
                break;
            }
            case T::Cabinet: insertAt = ampAt + 1; break;
            default: insertAt = rackOrder_.count; break;
        }
    }
    insertAt = juce::jlimit(0, rackOrder_.count, insertAt);
    RackState::resetParams(apvts, slot);
    for (int i = rackOrder_.count; i > insertAt; --i) rackOrder_.slots[std::size_t(i)] = rackOrder_.slots[std::size_t(i - 1)];
    rackOrder_.slots[std::size_t(insertAt)] = slot;
    ++rackOrder_.count;
    syncRack(sas::Transition::Duck);
    return true;
}

bool SasProcessor::removeModule(sas::SlotRef slot) {
    if (RackState::isFixed(slot)) return false;  // the amp, the first gate and the cabinet are always there
    int at = -1;
    for (int i = 0; i < rackOrder_.count; ++i)
        if (rackOrder_.slots[std::size_t(i)] == slot) at = i;
    if (at < 0) return false;
    for (int i = at; i + 1 < rackOrder_.count; ++i) rackOrder_.slots[std::size_t(i)] = rackOrder_.slots[std::size_t(i + 1)];
    --rackOrder_.count;
    syncRack(sas::Transition::Duck);
    if (slot.type == sas::ModuleType::NamPedal) clearNamPedal(slot.instance);  // its model goes with it, so it is not kept in the project
    return true;
}

bool SasProcessor::moveModule(sas::SlotRef slot, int newIndex) {
    int at = -1;
    for (int i = 0; i < rackOrder_.count; ++i)
        if (rackOrder_.slots[std::size_t(i)] == slot) at = i;
    if (at < 0) return false;
    for (int i = at; i + 1 < rackOrder_.count; ++i) rackOrder_.slots[std::size_t(i)] = rackOrder_.slots[std::size_t(i + 1)];
    newIndex = juce::jlimit(0, rackOrder_.count - 1, newIndex);
    for (int i = rackOrder_.count - 1; i > newIndex; --i) rackOrder_.slots[std::size_t(i)] = rackOrder_.slots[std::size_t(i - 1)];
    rackOrder_.slots[std::size_t(newIndex)] = slot;
    syncRack(sas::Transition::Duck);
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

bool SasProcessor::loadImpulseResponseFromMemory(const void* data, std::size_t size, const juce::String& name, int slot) {
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
    ir_[slot].conv.loadImpulseResponse(std::move(mono), reader->sampleRate, juce::dsp::Convolution::Stereo::no, juce::dsp::Convolution::Trim::yes,
                                       juce::dsp::Convolution::Normalise::no);
    {
        const juce::ScopedLock lock(irLock_);
        ir_[slot].bytes = juce::MemoryBlock(data, size);
        ir_[slot].name = name;
        ir_[slot].curve = std::move(curve);
    }
    ir_[slot].loaded = true;
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

bool SasProcessor::loadBuiltInImpulseResponse(int index, int slot) {
    if (index < 0 || index >= CabData::namedResourceListSize) return false;
    int size = 0;
    const char* data = CabData::getNamedResource(CabData::namedResourceList[index], size);
    return data != nullptr && loadImpulseResponseFromMemory(data, std::size_t(size), builtInImpulseResponseName(index), slot);
}

bool SasProcessor::loadImpulseResponse(const juce::File& file, int slot) {
    juce::MemoryBlock bytes;
    if (!file.existsAsFile() || !file.loadFileAsData(bytes) || bytes.getSize() == 0) return false;
    return loadImpulseResponseFromMemory(bytes.getData(), bytes.getSize(), file.getFileName(), slot);
}

void SasProcessor::clearImpulseResponse(int slot) {
    if (slot < 0 || slot >= kIrSlots) return;
    ir_[slot].loaded = false;
    const juce::ScopedLock lock(irLock_);
    ir_[slot].bytes.reset();
    ir_[slot].name.clear();
    ir_[slot].curve.clear();
}

juce::String SasProcessor::impulseResponseName(int slot) const {
    const juce::ScopedLock lock(irLock_);
    return slot >= 0 && slot < kIrSlots ? ir_[slot].name : juce::String();
}

std::vector<float> SasProcessor::impulseResponseCurve(int slot) const {
    const juce::ScopedLock lock(irLock_);
    return slot >= 0 && slot < kIrSlots ? ir_[slot].curve : std::vector<float>{};
}

// ------------------------------------------------------------------ state

// Slot A keeps the property names of the first versions ("irData" / "irName"), so older projects still load.
// The NAM pedal of the first instance keeps the property names of the first versions; the others add their number.
static juce::String namPedalKey(const char* base, int instance) { return instance == 0 ? juce::String(base) : juce::String(base) + juce::String(instance + 1); }

static const char* const kIrDataKey[SasProcessor::kIrSlots] = {"irData", "irDataB"};
static const char* const kIrNameKey[SasProcessor::kIrSlots] = {"irName", "irNameB"};

void SasProcessor::getStateInformation(juce::MemoryBlock& dest) {
    auto state = apvts.copyState();
    {
        const juce::ScopedLock lock(irLock_);
        for (int s = 0; s < kIrSlots; ++s) {
            state.removeProperty(kIrDataKey[s], nullptr);
            state.removeProperty(kIrNameKey[s], nullptr);
            if (ir_[s].bytes.getSize() > 0 && ir_[s].bytes.getSize() <= kMaxStoredIrBytes) {
                state.setProperty(kIrNameKey[s], ir_[s].name, nullptr);
                state.setProperty(kIrDataKey[s], juce::Base64::toBase64(ir_[s].bytes.getData(), ir_[s].bytes.getSize()), nullptr);
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
    else rackOrder_ = RackState::fromLegacy(apvts, tree.hasProperty("namPedalData"), true);  // every module: their switches may be automated
    syncRack(sas::Transition::Immediate);
    for (int s = 0; s < kIrSlots; ++s) {
        if (tree.hasProperty(kIrDataKey[s])) {
            juce::MemoryOutputStream out;
            if (juce::Base64::convertFromBase64(out, tree[kIrDataKey[s]].toString()) && out.getDataSize() > 0)
                if (!loadImpulseResponseFromMemory(out.getData(), out.getDataSize(), tree[kIrNameKey[s]].toString(), s)) clearImpulseResponse(s);
        } else {
            clearImpulseResponse(s);
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
