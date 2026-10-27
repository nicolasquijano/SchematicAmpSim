#include "PluginProcessor.h"

#include <algorithm>

#include "PluginEditor.h"
#include "sas/embedded_amps.h"

namespace {

using Layout = juce::AudioProcessorValueTreeState::ParameterLayout;
constexpr int kVersion = 1;

// 0..10 knob, shown with one decimal like the amp's dial.
std::unique_ptr<juce::AudioParameterFloat> knob(const char* id, const char* name, float def) {
    return std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{id, kVersion}, name, juce::NormalisableRange<float>(0.0f, 10.0f, 0.0f), def,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction(
            [](float v, int) { return juce::String(v, 1); }));
}

std::unique_ptr<juce::AudioParameterFloat> dbParam(const char* id, const char* name) {
    return std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{id, kVersion}, name, juce::NormalisableRange<float>(-24.0f, 24.0f, 0.0f), 0.0f,
        juce::AudioParameterFloatAttributes().withLabel("dB").withStringFromValueFunction(
            [](float v, int) { return juce::String(v, 1); }));
}

constexpr std::size_t kMaxChunk = 4096;
constexpr std::size_t kMaxStoredIrBytes = 4 * 1024 * 1024;

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
    l.add(dbParam(ids::output, "Output"));
    // New qualities are only appended so saved projects keep theirs (same rule as the Rust build).
    l.add(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID{ids::quality, kVersion}, "Quality", juce::StringArray{"Normal (2x)", "High (4x)", "Lite (2x)", "Eco (2x)"}, 0));
    l.add(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID{ids::cab, kVersion}, "Cabinet", juce::StringArray{"Off", "Filter", "IR"}, 1));
    l.add(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID{ids::jack, kVersion}, "Input jack", juce::StringArray{"Jack 1", "Jack 2", "Jack 3", "Jack 4"}, 0));
    l.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID{ids::levelMatch, kVersion}, "Level match", true));
    l.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID{ids::tsOn, kVersion}, "Tube Screamer", false));
    l.add(knob(ids::tsDrive, "TS Drive", 5.0f));
    l.add(knob(ids::tsTone, "TS Tone", 5.0f));
    l.add(knob(ids::tsLevel, "TS Level", 5.0f));
    l.add(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID{ids::pickup, kVersion}, "Guitar pickup", juce::StringArray{"Off", "Single coil", "Humbucker", "P-90"}, 0));
    l.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{ids::cable, kVersion}, "Guitar cable", juce::NormalisableRange<float>(0.0f, 14.0f, 2.0f), 4.0f,
                                                       juce::AudioParameterFloatAttributes().withLabel("m").withStringFromValueFunction([](float v, int) { return juce::String(v, 0); })));
    l.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID{ids::gateOn, kVersion}, "Noise gate", false));
    l.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{ids::gateThreshold, kVersion}, "Gate threshold", juce::NormalisableRange<float>(-80.0f, -20.0f, 0.0f), -60.0f,
                                                       juce::AudioParameterFloatAttributes().withLabel("dB").withStringFromValueFunction([](float v, int) { return juce::String(v, 0); })));
    l.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID{ids::gateRelease, kVersion}, "Gate release", juce::NormalisableRange<float>(20.0f, 500.0f, 0.0f, 0.5f), 120.0f,
                                                       juce::AudioParameterFloatAttributes().withLabel("ms").withStringFromValueFunction([](float v, int) { return juce::String(v, 0); })));
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
            const int amp = owner_.requestedAmp_.exchange(-1);
            if (amp < 0 || amp >= int(owner_.defs_.size()) || owner_.sampleRate_ <= 0.0) continue;
            try {
                auto* built = new sas::Chain(owner_.defs_[std::size_t(amp)], owner_.sampleRate_);
                owner_.readyAmp_.store(amp);
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

void SasProcessor::swapInBuiltChain() {
    sas::Chain* built = ready_.exchange(nullptr);
    if (!built) return;
    const int amp = readyAmp_.load();
    // Start the new circuits at the current knob positions instead of ramping from their defaults.
    activeAmp_ = amp;
    built->snap(readSettings(irLoaded_.load()));
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
                for (const auto& id : sas::embeddedAmpIds()) defs.push_back(sas::loadEmbeddedAmp(id));
                return defs;
            }())) {
    for (const auto& id : sas::embeddedAmpIds()) defs_.push_back(sas::loadEmbeddedAmp(id));

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
    builder_ = std::make_unique<ChainBuilder>(*this);
}

SasProcessor::~SasProcessor() {
    builder_.reset();  // joins the thread before the chains it may own go away
    delete ready_.exchange(nullptr);
    delete retired_.exchange(nullptr);
}

bool SasProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const {
    const auto& out = layouts.getMainOutputChannelSet();
    return (out == juce::AudioChannelSet::mono() || out == juce::AudioChannelSet::stereo())
           && out == layouts.getMainInputChannelSet();
}

void SasProcessor::prepareToPlay(double sampleRate, int samplesPerBlock) {
    const int wanted = selectedAmpIndex();
    if (!chain_ || sampleRate != sampleRate_ || wanted != activeAmp_) {
        sampleRate_ = sampleRate;
        delete ready_.exchange(nullptr);
        chain_ = std::make_unique<sas::Chain>(defs_[std::size_t(wanted)], sampleRate);  // heavy: solves the DC points
        activeAmp_ = wanted;
    } else {
        chain_->reset();
    }
    juce::dsp::ProcessSpec spec{sampleRate, static_cast<juce::uint32>(std::min<int>(samplesPerBlock, (int) kMaxChunk)), 1};
    convolution_.prepare(spec);
    irWasActive_ = false;
    latencyReported_ = -1;
    gate_.prepare(sampleRate);
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
    s.quality = static_cast<sas::Quality>(juce::jlimit(0, 3, int(raw(ids::quality) + 0.5)));
    s.jack = std::size_t(juce::jlimit(0, int(def.preamp.jacks.size()) - 1, int(raw(ids::jack) + 0.5)));
    const auto cab = static_cast<CabMode>(juce::jlimit(0, 2, int(raw(ids::cab) + 0.5)));
    // With no IR loaded the IR mode falls back to the cab filter instead of the raw amp.
    s.cabFilter = cab == CabMode::Filter || (cab == CabMode::Ir && !irAvailable);
    s.jackLevelMatch = raw(ids::levelMatch) > 0.5;
    s.vibratoOn = raw(ids::vibOn) > 0.5;
    s.vibratoSpeed = double(ap.vibSpeed->load()) / 10.0;
    s.tsOn = raw(ids::tsOn) > 0.5;
    s.pickup = int(raw(ids::pickup) + 0.5);
    s.cableMeters = int(raw(ids::cable) + 0.5);
    s.tsDrive = raw(ids::tsDrive) / 10.0;
    s.tsTone = raw(ids::tsTone) / 10.0;
    s.tsLevel = raw(ids::tsLevel) / 10.0;
    return s;
}

void SasProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) {
    juce::ScopedNoDenormals noDenormals;
    const int numCh = buffer.getNumChannels();
    const int numSamples = buffer.getNumSamples();
    if (!chain_ || numCh == 0) return;

    // Another amp was chosen: ask the builder, and swap in its circuits once they are ready. The
    // old amp keeps playing meanwhile.
    swapInBuiltChain();
    const int wanted = selectedAmpIndex();
    if (wanted != activeAmp_ && requestedAmp_.load() != wanted && ready_.load() == nullptr) {
        requestedAmp_.store(wanted);
        builder_->wake();
    }

    const bool irLoaded = irLoaded_.load();
    const auto cabParam = static_cast<CabMode>(juce::jlimit(0, 2, int(apvts.getRawParameterValue(ids::cab)->load() + 0.5f)));
    const bool irActive = cabParam == CabMode::Ir && irLoaded;
    chain_->setSettings(readSettings(irLoaded));
    gate_.setParams(apvts.getRawParameterValue(ids::gateOn)->load() > 0.5f, double(apvts.getRawParameterValue(ids::gateThreshold)->load()),
                    double(apvts.getRawParameterValue(ids::gateRelease)->load()));

    // Latency follows the quality and the convolution; report it when it changes.
    const int latency = int(chain_->latencySamples()) + (irActive ? convolution_.getLatency() : 0);
    if (latency != latencyReported_) {
        latencyReported_ = latency;
        setLatencySamples(latency);
    }
    if (irActive && !irWasActive_) convolution_.reset();
    irWasActive_ = irActive;

    gateGain_.store(float(gate_.gain()));
    for (int start = 0; start < numSamples; start += int(kMaxChunk)) {
        const int n = std::min<int>(int(kMaxChunk), numSamples - start);
        float* m = mono_.data();
        const float inv = 1.0f / float(numCh);
        for (int i = 0; i < n; ++i) m[i] = 0.0f;
        for (int ch = 0; ch < numCh; ++ch) {
            const float* in = buffer.getReadPointer(ch) + start;
            for (int i = 0; i < n; ++i) m[i] += in[i];
        }
        for (int i = 0; i < n; ++i) m[i] *= inv;
        float inPeak = 0.0f;
        for (int i = 0; i < n; ++i) inPeak = std::max(inPeak, std::fabs(m[i]));
        gate_.process(m, std::size_t(n));

        chain_->processAmp(m, m, std::size_t(n));
        if (irActive) {
            float* chans[1] = {m};
            juce::dsp::AudioBlock<float> block(chans, 1, std::size_t(n));
            convolution_.process(juce::dsp::ProcessContextReplacing<float>(block));
        }
        chain_->applyOutput(m, std::size_t(n));
        float outPeak = 0.0f;
        for (int i = 0; i < n; ++i) outPeak = std::max(outPeak, std::fabs(m[i]));
        if (inPeak > inputPeak_.load()) inputPeak_.store(inPeak);
        if (outPeak > outputPeak_.load()) outputPeak_.store(outPeak);

        for (int ch = 0; ch < numCh; ++ch) std::copy(m, m + n, buffer.getWritePointer(ch) + start);
    }
}

// ------------------------------------------------------------------ impulse response

void SasProcessor::loadImpulseResponseFromMemory(const void* data, std::size_t size, const juce::String& name) {
    convolution_.loadImpulseResponse(data, size, juce::dsp::Convolution::Stereo::no, juce::dsp::Convolution::Trim::yes, 0,
                                     juce::dsp::Convolution::Normalise::yes);
    {
        const juce::ScopedLock lock(irLock_);
        irBytes_ = juce::MemoryBlock(data, size);
        irName_ = name;
    }
    irLoaded_ = true;
}

bool SasProcessor::loadImpulseResponse(const juce::File& file) {
    juce::MemoryBlock bytes;
    if (!file.existsAsFile() || !file.loadFileAsData(bytes) || bytes.getSize() == 0) return false;
    // Reject anything JUCE cannot decode before replacing the current IR.
    juce::AudioFormatManager fm;
    fm.registerBasicFormats();
    if (std::unique_ptr<juce::AudioFormatReader> r{fm.createReaderFor(std::make_unique<juce::MemoryInputStream>(bytes.getData(), bytes.getSize(), false))};
        !r || r->lengthInSamples <= 0)
        return false;
    loadImpulseResponseFromMemory(bytes.getData(), bytes.getSize(), file.getFileName());
    return true;
}

void SasProcessor::clearImpulseResponse() {
    irLoaded_ = false;
    const juce::ScopedLock lock(irLock_);
    irBytes_.reset();
    irName_.clear();
}

juce::String SasProcessor::impulseResponseName() const {
    const juce::ScopedLock lock(irLock_);
    return irName_;
}

// ------------------------------------------------------------------ state

void SasProcessor::getStateInformation(juce::MemoryBlock& dest) {
    auto state = apvts.copyState();
    {
        const juce::ScopedLock lock(irLock_);
        state.removeProperty("irData", nullptr);
        state.removeProperty("irName", nullptr);
        if (irBytes_.getSize() > 0 && irBytes_.getSize() <= kMaxStoredIrBytes) {
            state.setProperty("irName", irName_, nullptr);
            state.setProperty("irData", juce::Base64::toBase64(irBytes_.getData(), irBytes_.getSize()), nullptr);
        }
    }
    if (auto xml = state.createXml()) copyXmlToBinary(*xml, dest);
}

void SasProcessor::setStateInformation(const void* data, int size) {
    std::unique_ptr<juce::XmlElement> xml(getXmlFromBinary(data, size));
    if (!xml || !xml->hasTagName(apvts.state.getType())) return;
    const auto tree = juce::ValueTree::fromXml(*xml);
    apvts.replaceState(tree);
    if (tree.hasProperty("irData")) {
        juce::MemoryOutputStream out;
        if (juce::Base64::convertFromBase64(out, tree["irData"].toString()) && out.getDataSize() > 0)
            loadImpulseResponseFromMemory(out.getData(), out.getDataSize(), tree["irName"].toString());
    } else {
        clearImpulseResponse();
    }
}

juce::AudioProcessorEditor* SasProcessor::createEditor() { return new SasEditor(*this); }

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new SasProcessor(); }
