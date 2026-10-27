#include "PluginProcessor.h"

#include <algorithm>
#include <array>

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
    l.add(knob(ids::irBlend, "IR blend", 5.0f));
    l.add(knob(ids::speaker, "Speaker dynamics", 0.0f));
    l.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID{ids::speakerIntense, kVersion}, "Speaker dynamics intense", false));
    l.add(knob(ids::bias, "Bias", 5.0f));
    l.add(std::make_unique<juce::AudioParameterChoice>(juce::ParameterID{ids::tube, kVersion}, "Power tubes", juce::StringArray{"Tubes 1", "Tubes 2", "Tubes 3", "Tubes 4"}, 0));
    l.add(std::make_unique<juce::AudioParameterChoice>(juce::ParameterID{ids::noise, kVersion}, "Noise and hum", juce::StringArray{"Off", "Low", "Real"}, 0));
    l.add(std::make_unique<juce::AudioParameterInt>(juce::ParameterID{ids::wear, kVersion}, "Wear", 0, 10, 0,
                                                     juce::AudioParameterIntAttributes().withStringFromValueFunction([](int v, int) { return juce::String(v * 10) + " %"; })));
    l.add(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID{ids::unit, kVersion}, "Unit",
        juce::StringArray{"Ideal", "Unit 1", "Unit 2", "Unit 3", "Unit 4", "Unit 5", "Unit 6", "Unit 7", "Unit 8", "Unit 9"}, 0));
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
    c.tube = tubes > 1 ? juce::jlimit(0, tubes - 1, int(apvts.getRawParameterValue(ids::tube)->load() + 0.5f)) : 0;
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
    built->snap(readSettings(anyIrLoaded()));
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
    mono2_.resize(kMaxChunk);
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
    juce::dsp::ProcessSpec spec{sampleRate, static_cast<juce::uint32>(std::min<int>(samplesPerBlock, (int) kMaxChunk)), 1};
    for (auto& slot : ir_) slot.conv.prepare(spec);
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
    s.bias = 0.75 + 0.05 * raw(ids::bias);
    s.speaker = raw(ids::speaker) / 10.0;
    s.speakerIntense = raw(ids::speakerIntense) > 0.5;
    s.noise = std::array<double, 3>{0.0, 0.5, 1.0}[std::size_t(juce::jlimit(0, 2, int(raw(ids::noise) + 0.5)))];
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
    const int wanted = wantedKey();
    if (wanted != activeKey_ && requestedKey_.load() != wanted && ready_.load() == nullptr) {
        requestedKey_.store(wanted);
        builder_->wake();
    }

    const bool aLoaded = ir_[0].loaded.load(), bLoaded = ir_[1].loaded.load();
    const bool irLoaded = aLoaded || bLoaded;
    const auto cabParam = static_cast<CabMode>(juce::jlimit(0, 2, int(apvts.getRawParameterValue(ids::cab)->load() + 0.5f)));
    const bool irActive = cabParam == CabMode::Ir && irLoaded;
    chain_->setSettings(readSettings(irLoaded));
    gate_.setParams(apvts.getRawParameterValue(ids::gateOn)->load() > 0.5f, double(apvts.getRawParameterValue(ids::gateThreshold)->load()),
                    double(apvts.getRawParameterValue(ids::gateRelease)->load()));

    // Latency follows the quality and the convolution; report it when it changes.
    const int latency = int(chain_->latencySamples()) + (irActive ? std::max(aLoaded ? ir_[0].conv.getLatency() : 0, bLoaded ? ir_[1].conv.getLatency() : 0) : 0);
    if (latency != latencyReported_) {
        latencyReported_ = latency;
        setLatencySamples(latency);
    }
    if (irActive && !irWasActive_)
        for (auto& slot : ir_) slot.conv.reset();
    irWasActive_ = irActive;

    gateGain_.store(float(gate_.gain()));
    idleMa_.store(float(chain_->idleMilliamps()));
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
            auto convolve = [n](IrSlot& slot, float* data) {
                float* chans[1] = {data};
                juce::dsp::AudioBlock<float> block(chans, 1, std::size_t(n));
                slot.conv.process(juce::dsp::ProcessContextReplacing<float>(block));
            };
            if (aLoaded && bLoaded) {
                // Both slots: equal-power blend (the IRs have unit energy), ramped over the block so a knob move does not click.
                const double theta = juce::jlimit(0.0, 1.0, double(apvts.getRawParameterValue(ids::irBlend)->load()) / 10.0) * (juce::MathConstants<double>::pi * 0.5);
                const float gA = float(std::cos(theta)), gB = float(std::sin(theta));
                float* m2 = mono2_.data();
                std::copy(m, m + n, m2);
                convolve(ir_[0], m);
                convolve(ir_[1], m2);
                for (int i = 0; i < n; ++i) {
                    const float t = float(i + 1) / float(n);
                    m[i] = m[i] * (blendGainA_ + (gA - blendGainA_) * t) + m2[i] * (blendGainB_ + (gB - blendGainB_) * t);
                }
                blendGainA_ = gA;
                blendGainB_ = gB;
            } else {
                convolve(aLoaded ? ir_[0] : ir_[1], m);
                blendGainA_ = aLoaded ? 1.0f : 0.0f;  // so a second slot loaded later fades in from this state
                blendGainB_ = aLoaded ? 0.0f : 1.0f;
            }
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

// Magnitude response in dB (mean 0) at 96 log-spaced frequencies from 50 Hz to 10 kHz, each the average power in a 1/6 octave band.
static std::vector<float> responseCurve(const juce::AudioBuffer<float>& mono, double sampleRate) {
    constexpr int order = 15, points = 96;
    constexpr int fftSize = 1 << order;
    juce::dsp::FFT fft(order);
    std::vector<float> data(std::size_t(fftSize) * 2, 0.0f);
    const int n = std::min(mono.getNumSamples(), fftSize / 2);
    for (int i = 0; i < n; ++i) data[std::size_t(i)] = mono.getSample(0, i);
    fft.performFrequencyOnlyForwardTransform(data.data());
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
    // Unit energy: a white signal keeps its level through the cabinet (the normalisation of JUCE is about 20 dB lower).
    double energy = 0.0;
    for (int i = 0; i < length; ++i) energy += double(mono.getSample(0, i)) * double(mono.getSample(0, i));
    if (!(energy > 0.0)) return false;
    mono.applyGain(float(1.0 / std::sqrt(energy)));
    auto curve = responseCurve(mono, reader->sampleRate);
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
    if (auto xml = state.createXml()) copyXmlToBinary(*xml, dest);
}

void SasProcessor::setStateInformation(const void* data, int size) {
    std::unique_ptr<juce::XmlElement> xml(getXmlFromBinary(data, size));
    if (!xml || !xml->hasTagName(apvts.state.getType())) return;
    const auto tree = juce::ValueTree::fromXml(*xml);
    apvts.replaceState(tree);
    for (int s = 0; s < kIrSlots; ++s) {
        if (tree.hasProperty(kIrDataKey[s])) {
            juce::MemoryOutputStream out;
            if (juce::Base64::convertFromBase64(out, tree[kIrDataKey[s]].toString()) && out.getDataSize() > 0)
                if (!loadImpulseResponseFromMemory(out.getData(), out.getDataSize(), tree[kIrNameKey[s]].toString(), s)) clearImpulseResponse(s);
        } else {
            clearImpulseResponse(s);
        }
    }
}

juce::AudioProcessorEditor* SasProcessor::createEditor() { return new SasEditor(*this); }

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new SasProcessor(); }
