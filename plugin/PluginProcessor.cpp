#include "ProcessorCommon.h"


Layout SasProcessor::createLayout(const std::vector<sas::AmpDef>& defs) {
    Layout l;
    juce::StringArray ampNames;
    for (const auto& d : defs) ampNames.add(d.name);
    l.add(std::make_unique<juce::AudioParameterChoice>(juce::ParameterID{ids::amp, kVersion}, "Amp", ampNames, 0));
    l.add(knob(ids::volume, "Preamp Volume", 6.0f));
    l.add(knob(ids::volume2, "Preamp Volume II", 5.0f));
    for (int e = 0; e < 5; ++e) l.add(knob(ids::aeq[e], "Graphic EQ " + juce::String(e + 1), 5.0f));
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
    l.add(knob(ids::cleanGain, "Clean Gain", 5.0f));
    l.add(knob(ids::cleanTreble, "Clean Treble", 5.0f));
    l.add(knob(ids::cleanMiddle, "Clean Middle", 5.0f));
    l.add(knob(ids::cleanBass, "Clean Bass", 5.0f));
    l.add(knob(ids::cleanPresence, "Clean Presence", 5.0f));
    l.add(knob(ids::cleanVolume, "Clean Volume", 5.0f));
    l.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID{ids::vibOn, kVersion}, "Vibrato", false));
    l.add(dbParam(ids::input, "Input"));
    // The amps are calibrated to a loud, full-scale speaker signal; a new instance opens 10 dB lower so it does not hit the host hot.
    l.add(dbParam(ids::output, "Output", -10.0f));
    // New qualities are only appended so saved projects keep theirs (same rule as the Rust build).
    l.add(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID{ids::quality, kVersion}, "Quality", juce::StringArray{"Normal (2x)", "High (4x)", "Lite (2x)", "Eco (2x)", "Efficient (2x/1x)"}, 4));  // a new instance opens in Efficient
    l.add(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID{ids::cab, kVersion}, "Cabinet", juce::StringArray{"Off", "Filter", "IR", "NAM", "Multi"}, 1));
    l.add(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID{ids::jack, kVersion}, "Input jack", juce::StringArray{"Jack 1", "Jack 2", "Jack 3", "Jack 4", "Jack 5", "Jack 6", "Jack 7", "Jack 8"}, 0));
    l.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID{ids::levelMatch, kVersion}, "Level match", true));
    for (int i = 0; i < 6; ++i)
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
    RackState::addParams(l, sas::ModuleType::Boost, 0);
    RackState::addParams(l, sas::ModuleType::TapeEcho, 0);
    RackState::addParams(l, sas::ModuleType::Acoustic, 0);
    RackState::addParams(l, sas::ModuleType::Klon, 0);
    RackState::addParams(l, sas::ModuleType::Leslie, 0);
    RackState::addParams(l, sas::ModuleType::SpringReverb, 0);
    RackState::addParams(l, sas::ModuleType::TenBandEq, 0);
    RackState::addParams(l, sas::ModuleType::Limiter, 0);
    RackState::addParams(l, sas::ModuleType::Octaver, 0);
    RackState::addParams(l, sas::ModuleType::Harmonist, 0);
    RackState::addParams(l, sas::ModuleType::BbdDelay, 0);
    RackState::addParams(l, sas::ModuleType::Bitcrusher, 0);
    RackState::addParams(l, sas::ModuleType::Layers, 0);
    RackState::addParams(l, sas::ModuleType::Distortion, 0);
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
    l.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID{ids::irAlign, kVersion}, "IR phase align", false));
    // The multi impulse loader: eight slots with their own level, solo, polarity, delay, filters and place on the plane, and the cursor.
    for (int k = 0; k < sas::kMultiIrSlots; ++k) {
        const juce::String n = "Multi IR " + juce::String(k + 1) + " ";
        l.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{ids::multiId(ids::mcLevel, k), kVersion}, n + "level",
                                                          juce::NormalisableRange<float>(-24.0f, 12.0f, 0.1f), 0.0f, juce::AudioParameterFloatAttributes().withLabel("dB")));
        l.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID{ids::multiId(ids::mcSolo, k), kVersion}, n + "solo", false));
        l.add(std::make_unique<juce::AudioParameterChoice>(juce::ParameterID{ids::multiId(ids::mcPolarity, k), kVersion}, n + "polarity",
                                                           juce::StringArray{"Auto", "Normal", "Inverted"}, 0));
        l.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{ids::multiId(ids::mcDelay, k), kVersion}, n + "delay",
                                                          juce::NormalisableRange<float>(0.0f, 3.0f, 0.01f), 0.0f, juce::AudioParameterFloatAttributes().withLabel("ms")));
        l.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{ids::multiId(ids::mcHp, k), kVersion}, n + "high-pass",
                                                          juce::NormalisableRange<float>(20.0f, 500.0f, 1.0f, 0.4f), 20.0f, juce::AudioParameterFloatAttributes().withLabel("Hz")));
        l.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{ids::multiId(ids::mcLp, k), kVersion}, n + "low-pass",
                                                          juce::NormalisableRange<float>(2000.0f, 20000.0f, 10.0f, 0.4f), 20000.0f, juce::AudioParameterFloatAttributes().withLabel("Hz")));
        float x, y;
        sas::multiIrDefaultPosition(k, x, y);
        l.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{ids::multiId(ids::mcX, k), kVersion}, n + "x", juce::NormalisableRange<float>(0.0f, 1.0f, 0.0f), x));
        l.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{ids::multiId(ids::mcY, k), kVersion}, n + "y", juce::NormalisableRange<float>(0.0f, 1.0f, 0.0f), y));
    }
    l.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{ids::mcCursorX, kVersion}, "Multi IR cursor x", juce::NormalisableRange<float>(0.0f, 1.0f, 0.0f), 0.5f));
    l.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{ids::mcCursorY, kVersion}, "Multi IR cursor y", juce::NormalisableRange<float>(0.0f, 1.0f, 0.0f), 0.5f));
    l.add(knob(ids::mcSpread, "Multi IR spread", 5.0f));
    l.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID{ids::mcAlign, kVersion}, "Multi IR phase align", false));
    l.add(knob(ids::speaker, "Speaker dynamics", 0.0f));
    l.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID{ids::speakerIntense, kVersion}, "Speaker dynamics intense", false));
    l.add(knob(ids::bias, "Bias", 5.0f));
    l.add(knob(ids::sag, "Sag", 5.0f));
    RackState::addNamToneParams(l, 0);
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
            const int amp = keyAmp(key);
            if (key < 0 || amp >= int(owner_.defs_.size()) || owner_.sampleRate_ <= 0.0) continue;
            try {
                sas::Condition cond = keyCondition(key);
                cond.parts = owner_.partFactors(amp);
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

sas::Condition SasProcessor::wantedCondition(int instance, bool withParts) const {
    const auto& def = defs_[std::size_t(selectedAmpIndex(instance))];
    sas::Condition c;
    if (withParts) c.parts = partFactors(selectedAmpIndex(instance));
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

int SasProcessor::wantedKey(int instance) const {
    const int amp = selectedAmpIndex(instance);
    return makeKey(amp, wantedCondition(instance, false), partsRev_[std::size_t(amp)].load());  // no lock on the audio thread: the edits are in the revision
}

std::map<std::string, double> SasProcessor::partFactors(int amp) const {
    const juce::ScopedLock lock(partsLock_);
    return amp >= 0 && amp < int(parts_.size()) ? parts_[std::size_t(amp)] : std::map<std::string, double>{};
}

double SasProcessor::partFactor(int amp, const std::string& name) const {
    const juce::ScopedLock lock(partsLock_);
    if (amp < 0 || amp >= int(parts_.size())) return 1.0;
    const auto it = parts_[std::size_t(amp)].find(name);
    return it == parts_[std::size_t(amp)].end() ? 1.0 : it->second;
}

int SasProcessor::partEditCount(int amp) const {
    const juce::ScopedLock lock(partsLock_);
    return amp >= 0 && amp < int(parts_.size()) ? int(parts_[std::size_t(amp)].size()) : 0;
}

void SasProcessor::setPartFactor(int amp, const std::string& name, double factor) {
    if (amp < 0 || amp >= int(defs_.size())) return;
    factor = juce::jlimit(0.1, 10.0, factor);
    {
        const juce::ScopedLock lock(partsLock_);
        auto& m = parts_[std::size_t(amp)];
        if (std::abs(factor - 1.0) < 1e-6) {
            if (m.erase(name) == 0) return;
        } else {
            auto [it, fresh] = m.try_emplace(name, factor);
            if (!fresh) {
                if (it->second == factor) return;
                it->second = factor;
            }
        }
    }
    partsRev_[std::size_t(amp)].fetch_add(1);
}

void SasProcessor::setPartFactors(int amp, const std::map<std::string, double>& factors) {
    if (amp < 0 || amp >= int(defs_.size())) return;
    std::map<std::string, double> clean;
    for (const auto& [name, f] : factors)
        if (f >= 0.1 && f <= 10.0 && std::abs(f - 1.0) > 1e-6) clean[name] = f;
    {
        const juce::ScopedLock lock(partsLock_);
        if (parts_[std::size_t(amp)] == clean) return;
        parts_[std::size_t(amp)] = std::move(clean);
    }
    partsRev_[std::size_t(amp)].fetch_add(1);
}

void SasProcessor::clearPartFactors(int amp) {
    if (amp < 0 || amp >= int(defs_.size())) return;
    {
        const juce::ScopedLock lock(partsLock_);
        if (parts_[std::size_t(amp)].empty()) return;
        parts_[std::size_t(amp)].clear();
    }
    partsRev_[std::size_t(amp)].fetch_add(1);
}

void SasProcessor::restoreParts(const juce::ValueTree& tree) {
    std::vector<std::map<std::string, double>> fresh(defs_.size());
    if (const auto node = tree.getChildWithName("PARTS"); node.isValid())
        for (const auto amp : node)
            for (std::size_t i = 0; i < defs_.size(); ++i)
                if (defs_[i].id == amp["id"].toString().toStdString())
                    for (const auto part : amp) {
                        const double f = double(part["f"]);
                        if (f >= 0.1 && f <= 10.0) fresh[i][part["n"].toString().toStdString()] = f;
                    }
    bool changed[kMaxAmps] = {};
    {
        const juce::ScopedLock lock(partsLock_);
        for (std::size_t i = 0; i < defs_.size(); ++i) changed[i] = parts_[i] != fresh[i];
        parts_ = std::move(fresh);
    }
    for (std::size_t i = 0; i < defs_.size(); ++i)
        if (changed[i]) partsRev_[i].fetch_add(1);
}

void SasProcessor::swapInBuiltChain(int instance) {
    auto& runner = amps_[instance];
    sas::Chain* built = runner.ready.exchange(nullptr);
    if (!built) return;
    const int key = runner.readyKey.load();
    const int amp = keyAmp(key);
    // Start the new circuits at the current knob positions instead of ramping from their defaults.
    runner.activeAmp = amp;
    runner.activeKey = key;
    if (auto* module = ampModule(instance)) {
        module->setSettings(readSettings(anyIrLoaded(), instance));
        module->setOn(ampValue(instance, ids::ampOn) > 0.5);
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
    // The chain key (makeKey) and partsRev_ hold up to kMaxAmps amps: past that the key would name another amp.
    if (defs_.size() > std::size_t(kMaxAmps)) throw std::runtime_error("more amps than the chain key can name: raise kAmpBits");
    parts_.resize(defs_.size());

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
    mono3_.resize(kMaxChunk);
    monoR_.resize(kMaxChunk);
    multiAcc_.resize(kMaxChunk);
    multiAccR_.resize(kMaxChunk);
    for (int k = 0; k < sas::kMultiIrSlots; ++k) {
        multiShown_[k] = 0.0f;
        mcLevelP_[k] = apvts.getRawParameterValue(ids::multiId(ids::mcLevel, k));
        mcSoloP_[k] = apvts.getRawParameterValue(ids::multiId(ids::mcSolo, k));
        mcPolP_[k] = apvts.getRawParameterValue(ids::multiId(ids::mcPolarity, k));
        mcDelayP_[k] = apvts.getRawParameterValue(ids::multiId(ids::mcDelay, k));
        mcHpP_[k] = apvts.getRawParameterValue(ids::multiId(ids::mcHp, k));
        mcLpP_[k] = apvts.getRawParameterValue(ids::multiId(ids::mcLp, k));
        mcXP_[k] = apvts.getRawParameterValue(ids::multiId(ids::mcX, k));
        mcYP_[k] = apvts.getRawParameterValue(ids::multiId(ids::mcY, k));
    }
    mcCursorXP_ = apvts.getRawParameterValue(ids::mcCursorX);
    mcCursorYP_ = apvts.getRawParameterValue(ids::mcCursorY);
    mcSpreadP_ = apvts.getRawParameterValue(ids::mcSpread);
    mcAlignP_ = apvts.getRawParameterValue(ids::mcAlign);
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
    for (int c = 0; c < 2; ++c) apvts.addParameterListener(RackState::paramId(ids::irAlign, c), this);
    for (int k = 0; k < sas::kMultiIrSlots; ++k)
        for (const char* base : {ids::mcPolarity, ids::mcDelay, ids::mcHp, ids::mcLp}) apvts.addParameterListener(ids::multiId(base, k), this);
    apvts.addParameterListener(ids::mcAlign, this);
}

SasProcessor::~SasProcessor() {
    for (int c = 0; c < 2; ++c) apvts.removeParameterListener(RackState::paramId(ids::irAlign, c), this);
    for (int k = 0; k < sas::kMultiIrSlots; ++k)
        for (const char* base : {ids::mcPolarity, ids::mcDelay, ids::mcHp, ids::mcLp}) apvts.removeParameterListener(ids::multiId(base, k), this);
    apvts.removeParameterListener(ids::mcAlign, this);
    cancelPendingUpdate();
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
        amps_[0].chain = std::make_unique<sas::Chain>(sas::applyCondition(defs_[std::size_t(keyAmp(wanted))], wantedCondition(0)), sampleRate);  // heavy: solves the DC points
        amps_[0].activeAmp = keyAmp(wanted);
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
    // The convolution is told the largest block it will get: the one the host announces, as a power of two. Telling it kMaxChunk (4096) made it
    // work in much bigger FFTs than the host's blocks need and cost about nine times more. processCabinet cuts what it hands over into pieces
    // of at most that size (some hosts send bigger blocks than they announced, and a block above the announced maximum crashes it).
    convBlock_ = samplesPerBlock > 0 ? juce::jlimit(128, int(kMaxChunk), juce::nextPowerOfTwo(samplesPerBlock)) : 1024;
    juce::dsp::ProcessSpec spec{sampleRate, static_cast<juce::uint32>(convBlock_), 1};
    for (auto& bank : cabs_) {
        for (auto& slot : bank.ir) {
            slot.conv.prepare(spec);
            slot.convR.prepare(spec);
        }
        bank.irWasActive = false;
        bank.dbl.prepare(sampleRate);
    }
    for (auto& slot : multi_) {
        slot.conv.prepare(spec);
        slot.convR.prepare(spec);
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
    for (int e = 0; e < 5; ++e) s.geq[std::size_t(e)] = raw(ids::aeq[e]) / 10.0;
    s.inputDb = raw(ids::input);
    s.outputDb = raw(ids::output);
    s.quality = static_cast<sas::Quality>(juce::jlimit(0, 4, int(raw(ids::quality) + 0.5)));
    s.jack = def.preamp.jacks.empty() ? 0 : std::size_t(juce::jlimit(0, int(def.preamp.jacks.size()) - 1, int(raw(ids::jack) + 0.5)));
    const auto cab = static_cast<CabMode>(juce::jlimit(0, 4, int(raw(ids::cab) + 0.5)));
    // With no IR loaded the IR mode falls back to the cab filter instead of the raw amp.
    s.cabFilter = cab == CabMode::Filter || (cab == CabMode::Ir && !irAvailable) || (cab == CabMode::Nam && namCabActive_.load() == nullptr) ||
                   (cab == CabMode::Multi && !anyMultiLoaded());  // (the cabinet module applies it)
    s.cabFilterInChain = false;  // the cabinet module applies it
    s.jackLevelMatch = true;  // the jacks always play at a similar level; the old "level_match" parameter stays only so saved projects load
    s.selectors.clear();
    for (std::size_t i = 0; i < def.preamp.selectors.size() && i < 6; ++i)
        s.selectors.push_back(std::size_t(juce::jlimit(0, int(def.preamp.selectors[i].positions.size()) - 1, int(raw(ids::sel[i]) + 0.5))));
    s.vibratoOn = raw(ids::vibOn) > 0.5;
    s.vibratoSpeed = double(ap.vibSpeed->load()) / 10.0;
    s.tsOn = false;  // the overdrive and the compressor are rack modules: the amp module takes the ones next to it
    s.compOn = false;
    s.pickup = int(raw(ids::pickup) + 0.5);
    s.cableMeters = int(raw(ids::cable) + 0.5);
    s.bias = 0.75 + 0.05 * raw(ids::bias);
    s.sag = std::pow(3.0, (raw(ids::sag) - 5.0) / 5.0);  // 0 = a third of the rectifier's resistance, 5 = as drawn, 10 = three times
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
        s.namBassHz = raw(ids::namBassHz);
        s.namMidHz = raw(ids::namMidHz);
        s.namMidQ = raw(ids::namMidQ);
        s.namTrebleHz = raw(ids::namTrebleHz);
        s.namPresence = raw(ids::presence) / 10.0;
        s.namDepth = raw(ids::resonance) / 10.0;
        s.namPost = raw(ids::namPost) > 0.5;
    }
    s.noise = std::array<double, 3>{0.0, 0.5, 1.0}[std::size_t(juce::jlimit(0, 2, int(raw(ids::noise) + 0.5)))];
    return s;
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
        const auto cabParam = static_cast<CabMode>(juce::jlimit(0, 4, int(cabValue(cabinet, ids::cab) + 0.5)));
        const bool multiNow = cabinet == 0 && cabParam == CabMode::Multi && anyMultiLoaded();
        const bool irActive = cabParam == CabMode::Ir && irLoaded;
        // Only the first cabinet has a NAM cabinet; the second one, set to NAM, plays its filter.
        sas::NamModel* const namCab = cabinet == 0 && cabParam == CabMode::Nam ? namCabActive_.load() : nullptr;
        sas::NamModel* const namCabR = namCab ? namCabActiveR_.load() : nullptr;
        if (cabinet == 0) {
            if (namCab && !namCabWasActive_) {
                namCab->reset();
                if (namCabR) namCabR->reset();
            }
            namCabWasActive_ = namCab != nullptr;
        }
        cb.namNow = namCab;
        cb.namNowR = namCabR;
        cb.irActiveNow = irActive;
        cb.aLoaded = aLoaded;
        cb.bLoaded = bLoaded;
        // With no IR loaded the IR mode falls back to the cab filter instead of the raw amp (and so does a NAM cabinet that has no model).
        const bool cabFilterOn = cabParam == CabMode::Filter || (cabParam == CabMode::Ir && !irLoaded) || (cabParam == CabMode::Nam && namCab == nullptr) ||
                                 (cabParam == CabMode::Multi && !multiNow);
        double irLatency = irActive ? double(std::max(aLoaded ? cb.ir[0].conv.getLatency() : 0, bLoaded ? cb.ir[1].conv.getLatency() : 0)) : 0.0;
        if (multiNow)
            for (const auto& s : multi_)
                if (s.loaded.load()) irLatency = std::max(irLatency, double(s.conv.getLatency()));
        if (cabinet == 0) {
            if (multiNow && !multiWasActive_)  // coming in: the convolutions start from nothing, and the gains from zero
                for (int k = 0; k < kMultiSlots; ++k) {
                    multi_[k].conv.reset();
                    multi_[k].convR.reset();
                    multiRunning_[k] = false;
                    multiGainNow_[k] = 0.0f;
                }
            multiWasActive_ = multiNow;
            multiActiveNow_ = multiNow;
        }
        const bool cabOn = cabValue(cabinet, ids::cabOn) > 0.5;
        if (cabOn && !cb.wasOn) {  // coming back: nothing of the convolution's old audio
            for (auto& slot : cb.ir) {
                slot.conv.reset();
                slot.convR.reset();
            }
            cabModule->reset();
        }
        cb.wasOn = cabOn;
        cabModule->setBlock(cabFilterOn, cabOn ? irLatency : 0.0, cabOn);
        if (irActive && !cb.irWasActive)
            for (auto& slot : cb.ir) {
                slot.conv.reset();
                slot.convR.reset();
            }
        cb.irWasActive = irActive;
        irLatencyOf[cabinet] = irLatency;
        irActiveOf[cabinet] = irActive;
    }
    for (int instance = 0; instance < 2; ++instance)
        if (auto* amp = ampModule(instance)) {
            amp->setSettings(readSettings(anyIrLoaded(instance), instance));
            amp->setOn(ampValue(instance, ids::ampOn) > 0.5);
        }
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
        if (inPeak > inputMax_.load()) inputMax_.store(inPeak);
        if (outPeak > outputMax_.load()) outputMax_.store(outPeak);

        {
            float levels[kMaxMeters];
            const int count = int(amps_[0].chain->takeMeters(levels, std::size_t(kMaxMeters)));
            meterCount_.store(count);
            for (int i = 0; i < count; ++i)
                if (levels[i] > meterMax_[i].load()) meterMax_[i].store(levels[i]);
        }

        for (int ch = 0; ch < numCh; ++ch) {
            float* out = buffer.getWritePointer(ch) + start;
            if (numCh == 1)
                for (int i = 0; i < n; ++i) out[i] = 0.5f * (m[i] + mR[i]);  // a mono output: the two ears summed
            else
                std::copy(ch % 2 == 0 ? m : mR, (ch % 2 == 0 ? m : mR) + n, out);
        }
    }
}

// The cabinet: the filter (or the NAM cabinet, or the impulse responses), then the automatic double tracking. A mono signal in, a stereo pair out;
// a stereo pair in (a stereo effect in front of the cabinet) goes through the same cabinet channel by channel and stays a pair.
void SasProcessor::processCabinet(CabinetModule& cab, float* m, float* mR, std::size_t nn) {
    const int n = int(nn);
    const int cabinet = cab.instance();
    auto& cb = cabs_[cabinet];
    bool pair = cab.stereoInput();
    // A NAM cabinet needs its twin for the right channel; without it the pair is summed first, as it used to be.
    if (pair && cb.namNow && !cb.namNowR) {
        for (int i = 0; i < n; ++i) m[i] = 0.5f * (m[i] + mR[i]);
        pair = false;
    }
    if (cab.filterOn()) {
        for (int i = 0; i < n; ++i) m[i] = cab.filter().process(m[i]);
        if (pair)
            for (int i = 0; i < n; ++i) mR[i] = cab.filterR().process(mR[i]);
    }
    if (cb.namNow) {  // the NAM cabinet: through a scratch block, the model reads its input while it writes
        cb.namNow->process(m, mono2_.data(), std::size_t(n));
        std::copy(mono2_.data(), mono2_.data() + n, m);
        if (pair) {
            cb.namNowR->process(mR, mono2_.data(), std::size_t(n));
            std::copy(mono2_.data(), mono2_.data() + n, mR);
        }
    }
    bool haveR = pair;
    if (cabinet == 0 && multiActiveNow_) {
        processMulti(m, mR, pair, nn);
    } else if (cb.irActiveNow) {
        const bool aLoaded = cb.aLoaded, bLoaded = cb.bLoaded;
        const int piece = convBlock_;
        auto convolve = [n, piece](juce::dsp::Convolution& conv, float* data) {
            for (int from = 0; from < n; from += piece) {
                float* chans[1] = {data + from};
                juce::dsp::AudioBlock<float> block(chans, 1, std::size_t(std::min(piece, n - from)));
                conv.process(juce::dsp::ProcessContextReplacing<float>(block));
            }
        };
        if (pair) {
            // Each channel goes through the cabinet. With two impulses the blend applies to both channels; the A / B width, which makes a pair out of
            // a mono signal, has nothing to do here (the pair is already there), so it is parked at 0 for when the signal is mono again.
            if (aLoaded && bLoaded) {
                const double theta = juce::jlimit(0.0, 1.0, cabValue(cabinet, ids::irBlend) / 10.0) * (juce::MathConstants<double>::pi * 0.5);
                const float gA = float(std::cos(theta)), gB = float(std::sin(theta));
                float* m2 = mono2_.data();
                float* r2 = mono3_.data();
                std::copy(m, m + n, m2);
                std::copy(mR, mR + n, r2);
                convolve(cb.ir[0].conv, m);
                convolve(cb.ir[1].conv, m2);
                convolve(cb.ir[0].convR, mR);
                convolve(cb.ir[1].convR, r2);
                for (int i = 0; i < n; ++i) {
                    const float t = float(i + 1) / float(n);
                    const float ga = cb.blendGainA + (gA - cb.blendGainA) * t, gb = cb.blendGainB + (gB - cb.blendGainB) * t;
                    m[i] = m[i] * ga + m2[i] * gb;
                    mR[i] = mR[i] * ga + r2[i] * gb;
                }
                cb.blendGainA = gA;
                cb.blendGainB = gB;
                cb.widthNow = 0.0f;
            } else {
                auto& slot = aLoaded ? cb.ir[0] : cb.ir[1];
                convolve(slot.conv, m);
                convolve(slot.convR, mR);
                cb.blendGainA = aLoaded ? 1.0f : 0.0f;
                cb.blendGainB = aLoaded ? 0.0f : 1.0f;
            }
        } else if (aLoaded && bLoaded) {
            // Both slots: an equal-power blend, and the width that separates them: at 0 both ears get the blend, at 10 the left ear gets A and the
            // right ear B (two microphones, one on each side). Ramped over the block so a knob move does not click.
            const double theta = juce::jlimit(0.0, 1.0, cabValue(cabinet, ids::irBlend) / 10.0) * (juce::MathConstants<double>::pi * 0.5);
            const float gA = float(std::cos(theta)), gB = float(std::sin(theta));
            const float width = juce::jlimit(0.0f, 1.0f, float(cabValue(cabinet, ids::irWidth)) / 10.0f);
            float* m2 = mono2_.data();
            std::copy(m, m + n, m2);
            convolve(cb.ir[0].conv, m);
            convolve(cb.ir[1].conv, m2);
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
            convolve((aLoaded ? cb.ir[0] : cb.ir[1]).conv, m);
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

juce::AudioProcessorEditor* SasProcessor::createEditor() { return new SasEditor(*this); }

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new SasProcessor(); }

float SasProcessor::autoInputDb(float rawPeak) {
    if (rawPeak < 1e-4f) return 0.0f;
    return juce::jlimit(-24.0f, 24.0f, kAutoInputPeakDb - juce::Decibels::gainToDecibels(rawPeak));
}

float SasProcessor::autoOutputDb(float currentDb, float outputPeak) {
    if (outputPeak < 1e-5f) return currentDb;
    return juce::jlimit(-24.0f, 24.0f, currentDb + kAutoOutputPeakDb - juce::Decibels::gainToDecibels(outputPeak));
}

