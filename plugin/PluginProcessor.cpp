#include "PluginProcessor.h"

#include "PluginEditor.h"
#include "aes/embedded_amps.h"

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

Layout AesProcessor::createLayout() {
    Layout l;
    l.add(knob(ids::volume, "Preamp Volume", 6.0f));
    l.add(knob(ids::bass, "Bass", 5.0f));
    l.add(knob(ids::middle, "Middle", 5.0f));
    l.add(knob(ids::treble, "Treble", 6.0f));
    l.add(knob(ids::presence, "Presence", 5.0f));
    l.add(knob(ids::master, "Master", 5.0f));
    l.add(dbParam(ids::input, "Input"));
    l.add(dbParam(ids::output, "Output"));
    // Lite stays last so saved projects keep their quality (same rule as the Rust build).
    l.add(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID{ids::quality, kVersion}, "Quality", juce::StringArray{"Normal (2x)", "High (4x)", "Lite (2x)"}, 0));
    l.add(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID{ids::cab, kVersion}, "Cabinet", juce::StringArray{"Off", "Filter", "IR"}, 1));
    l.add(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID{ids::jack, kVersion}, "Input jack", juce::StringArray{"High", "Low"}, 0));
    l.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID{ids::tsOn, kVersion}, "Tube Screamer", false));
    l.add(knob(ids::tsDrive, "TS Drive", 5.0f));
    l.add(knob(ids::tsTone, "TS Tone", 5.0f));
    l.add(knob(ids::tsLevel, "TS Level", 5.0f));
    return l;
}

AesProcessor::AesProcessor()
    : AudioProcessor(BusesProperties()
                         .withInput("Input", juce::AudioChannelSet::stereo(), true)
                         .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      apvts(*this, nullptr, "AES", createLayout()),
      def_(aes::loadEmbeddedAmp("jcm800_2203")) {
    // Knob parameters are looked up by the amp's knob names (the ids above are those names).
    for (const auto& k : def_.preamp.knobs) knobParams_.push_back(apvts.getRawParameterValue(k.name));
    settings_.knobs.resize(def_.preamp.knobs.size());
    mono_.resize(kMaxChunk);
}

bool AesProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const {
    const auto& out = layouts.getMainOutputChannelSet();
    return (out == juce::AudioChannelSet::mono() || out == juce::AudioChannelSet::stereo())
           && out == layouts.getMainInputChannelSet();
}

void AesProcessor::prepareToPlay(double sampleRate, int samplesPerBlock) {
    if (!chain_ || sampleRate != sampleRate_) {
        chain_ = std::make_unique<aes::Chain>(def_, sampleRate);  // heavy: solves the DC points
        sampleRate_ = sampleRate;
    } else {
        chain_->reset();
    }
    juce::dsp::ProcessSpec spec{sampleRate, static_cast<juce::uint32>(std::min<int>(samplesPerBlock, (int) kMaxChunk)), 1};
    convolution_.prepare(spec);
    irWasActive_ = false;
    latencyReported_ = -1;
}

aes::ChainSettings AesProcessor::readSettings(bool irAvailable) {
    auto& s = settings_;
    for (std::size_t i = 0; i < knobParams_.size(); ++i)
        s.knobs[i] = knobParams_[i] ? double(knobParams_[i]->load()) / 10.0 : def_.preamp.knobs[i].def;
    auto raw = [this](const char* id) { return double(apvts.getRawParameterValue(id)->load()); };
    s.presence = raw(ids::presence) / 10.0;
    s.inputDb = raw(ids::input);
    s.outputDb = raw(ids::output);
    s.quality = static_cast<aes::Quality>(juce::jlimit(0, 2, int(raw(ids::quality) + 0.5)));
    s.jack = std::size_t(juce::jlimit(0, int(def_.preamp.jacks.size()) - 1, int(raw(ids::jack) + 0.5)));
    const auto cab = static_cast<CabMode>(juce::jlimit(0, 2, int(raw(ids::cab) + 0.5)));
    // With no IR loaded the IR mode falls back to the cab filter instead of the raw amp.
    s.cabFilter = cab == CabMode::Filter || (cab == CabMode::Ir && !irAvailable);
    s.tsOn = raw(ids::tsOn) > 0.5;
    s.tsDrive = raw(ids::tsDrive) / 10.0;
    s.tsTone = raw(ids::tsTone) / 10.0;
    s.tsLevel = raw(ids::tsLevel) / 10.0;
    return s;
}

void AesProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) {
    juce::ScopedNoDenormals noDenormals;
    const int numCh = buffer.getNumChannels();
    const int numSamples = buffer.getNumSamples();
    if (!chain_ || numCh == 0) return;

    const bool irLoaded = irLoaded_.load();
    const auto cabParam = static_cast<CabMode>(juce::jlimit(0, 2, int(apvts.getRawParameterValue(ids::cab)->load() + 0.5f)));
    const bool irActive = cabParam == CabMode::Ir && irLoaded;
    chain_->setSettings(readSettings(irLoaded));

    // Latency follows the quality and the convolution; report it when it changes.
    const int latency = int(chain_->latencySamples()) + (irActive ? convolution_.getLatency() : 0);
    if (latency != latencyReported_) {
        latencyReported_ = latency;
        setLatencySamples(latency);
    }
    if (irActive && !irWasActive_) convolution_.reset();
    irWasActive_ = irActive;

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

        chain_->processAmp(m, m, std::size_t(n));
        if (irActive) {
            float* chans[1] = {m};
            juce::dsp::AudioBlock<float> block(chans, 1, std::size_t(n));
            convolution_.process(juce::dsp::ProcessContextReplacing<float>(block));
        }
        chain_->applyOutput(m, std::size_t(n));

        for (int ch = 0; ch < numCh; ++ch) std::copy(m, m + n, buffer.getWritePointer(ch) + start);
    }
}

// ------------------------------------------------------------------ impulse response

void AesProcessor::loadImpulseResponseFromMemory(const void* data, std::size_t size, const juce::String& name) {
    convolution_.loadImpulseResponse(data, size, juce::dsp::Convolution::Stereo::no, juce::dsp::Convolution::Trim::yes, 0,
                                     juce::dsp::Convolution::Normalise::yes);
    {
        const juce::ScopedLock lock(irLock_);
        irBytes_ = juce::MemoryBlock(data, size);
        irName_ = name;
    }
    irLoaded_ = true;
}

bool AesProcessor::loadImpulseResponse(const juce::File& file) {
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

void AesProcessor::clearImpulseResponse() {
    irLoaded_ = false;
    const juce::ScopedLock lock(irLock_);
    irBytes_.reset();
    irName_.clear();
}

juce::String AesProcessor::impulseResponseName() const {
    const juce::ScopedLock lock(irLock_);
    return irName_;
}

// ------------------------------------------------------------------ state

void AesProcessor::getStateInformation(juce::MemoryBlock& dest) {
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

void AesProcessor::setStateInformation(const void* data, int size) {
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

juce::AudioProcessorEditor* AesProcessor::createEditor() { return new AesEditor(*this); }

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new AesProcessor(); }
