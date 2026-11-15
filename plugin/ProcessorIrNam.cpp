#include "ProcessorCommon.h"

#include "sas/ir_align.h"

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
    {
        const juce::ScopedLock lock(irLock_);
        cb.ir[slot].bytes = juce::MemoryBlock(data, size);
        cb.ir[slot].name = name;
        cb.ir[slot].curve = std::move(curve);
        cb.ir[slot].mono = std::move(mono);
        cb.ir[slot].rate = reader->sampleRate;
    }
    cb.ir[slot].loaded = true;
    applyImpulseResponses(cabinet);
    return true;
}

// The convolutions of a cabinet. With "in phase" off every impulse is loaded as it is (JUCE trims the silence around it). With it on and both
// impulses there, the cross-correlation of their direct sound gives the offset and the polarity that make them add up: the earlier one is
// delayed, B is flipped when it is upside down, and the silence they share in front (and what is left behind) is trimmed here, because
// the trimming of JUCE would take the delay away again.
void SasProcessor::applyImpulseResponses(int cabinet) {
    auto& cb = cabs_[cabinet];
    const bool align = cabValue(cabinet, ids::irAlign) > 0.5 && cb.ir[0].loaded && cb.ir[1].loaded && cb.ir[0].rate == cb.ir[1].rate &&
                       cb.ir[0].mono.getNumSamples() > 0 && cb.ir[1].mono.getNumSamples() > 0;
    IrAlignInfo info;
    auto loadInto = [](IrSlot& s, juce::AudioBuffer<float> buffer, double rate, juce::dsp::Convolution::Trim trim) {
        juce::AudioBuffer<float> twin(buffer);  // the right channel's convolution gets the same impulse
        s.conv.loadImpulseResponse(std::move(buffer), rate, juce::dsp::Convolution::Stereo::no, trim, juce::dsp::Convolution::Normalise::no);
        s.convR.loadImpulseResponse(std::move(twin), rate, juce::dsp::Convolution::Stereo::no, trim, juce::dsp::Convolution::Normalise::no);
    };
    if (!align) {
        for (int s = 0; s < kIrSlots; ++s) {
            if (!cb.ir[s].loaded) continue;
            juce::AudioBuffer<float> copy;
            double rate;
            {
                const juce::ScopedLock lock(irLock_);
                copy.makeCopyOf(cb.ir[s].mono);
                rate = cb.ir[s].rate;
            }
            loadInto(cb.ir[s], std::move(copy), rate, juce::dsp::Convolution::Trim::yes);
        }
    } else {
        juce::AudioBuffer<float> a, b;
        double rate;
        {
            const juce::ScopedLock lock(irLock_);
            a.makeCopyOf(cb.ir[0].mono);
            b.makeCopyOf(cb.ir[1].mono);
            rate = cb.ir[0].rate;
        }
        const auto r = sas::alignImpulses(a.getReadPointer(0), std::size_t(a.getNumSamples()), b.getReadPointer(0), std::size_t(b.getNumSamples()),
                                          int(rate * 0.003), std::size_t(rate * 0.05));
        auto shifted = [](const juce::AudioBuffer<float>& src, int delay, bool invert) {
            juce::AudioBuffer<float> out(1, src.getNumSamples() + delay);
            out.clear();
            const float g = invert ? -1.0f : 1.0f;
            for (int i = 0; i < src.getNumSamples(); ++i) out.setSample(0, i + delay, g * src.getSample(0, i));
            return out;
        };
        auto fa = shifted(a, r.delayA, false);
        auto fb = shifted(b, r.delayB, r.invertB);
        constexpr float kSilence = 1.0e-4f;  // -80 dB, the threshold JUCE uses
        auto firstLoud = [&](const juce::AudioBuffer<float>& x) {
            for (int i = 0; i < x.getNumSamples(); ++i)
                if (std::abs(x.getSample(0, i)) > kSilence) return i;
            return x.getNumSamples();
        };
        auto lastLoud = [&](const juce::AudioBuffer<float>& x) {
            for (int i = x.getNumSamples() - 1; i >= 0; --i)
                if (std::abs(x.getSample(0, i)) > kSilence) return i + 1;
            return 0;
        };
        const int lead = std::min(firstLoud(fa), firstLoud(fb));
        auto cut = [&](const juce::AudioBuffer<float>& x) {
            const int end = std::max(lead + 1, lastLoud(x));
            juce::AudioBuffer<float> out(1, std::max(1, end - lead));
            out.clear();
            for (int i = lead; i < end && i < x.getNumSamples(); ++i) out.setSample(0, i - lead, x.getSample(0, i));
            return out;
        };
        loadInto(cb.ir[0], cut(fa), rate, juce::dsp::Convolution::Trim::no);
        loadInto(cb.ir[1], cut(fb), rate, juce::dsp::Convolution::Trim::no);
        info.valid = true;
        info.invertB = r.invertB;
        info.delayAms = 1000.0 * r.delayA / rate;
        info.delayBms = 1000.0 * r.delayB / rate;
        info.correlation = r.correlation;
    }
    const juce::ScopedLock lock(irLock_);
    cb.alignInfo = info;
}

SasProcessor::IrAlignInfo SasProcessor::irAlignInfo(int cabinet) const {
    const juce::ScopedLock lock(irLock_);
    return cabs_[juce::jlimit(0, 1, cabinet)].alignInfo;
}

// "In phase" switched (by the panel, by an automation lane or by a restored project): the impulses are loaded again on the message thread.
void SasProcessor::parameterChanged(const juce::String& id, float) {
    for (int c = 0; c < 2; ++c)
        if (id == RackState::paramId(ids::irAlign, c)) alignDirty_[c] = true;
    triggerAsyncUpdate();
}

void SasProcessor::handleAsyncUpdate() {
    for (int c = 0; c < 2; ++c)
        if (alignDirty_[c].exchange(false)) applyImpulseResponses(c);
}

// ------------------------------------------------------------------ NAM model

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
    std::string errorR;
    auto modelR = model ? sas::NamModel::loadFromMemory(data, size, name.toStdString(), fs, int(kMaxChunk), errorR) : nullptr;
    const juce::ScopedLock lock(namLock_);
    if (!model || !modelR) {
        namCabMessage_ = error.empty() && errorR.empty() ? juce::String("the file could not be loaded") : juce::String(error.empty() ? errorR : error);
        return false;
    }
    namCabPrev_ = std::move(namCab_);
    namCab_ = model;
    namCabPrevR_ = std::move(namCabR_);
    namCabR_ = modelR;
    namCabBytes_ = juce::MemoryBlock(data, size);
    namCabName_ = name;
    namCabRate_ = fs;
    namCabMessage_ = model->rateMatches()
                         ? juce::String()
                         : "Trained at " + juce::String(model->trainedRate(), 0) + " Hz, played at " + juce::String(fs, 0) + " Hz: it will sound off-pitch.";
    namCabActiveR_.store(modelR.get());
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
    namCabActiveR_.store(nullptr);
    namCabPrev_ = std::move(namCab_);
    namCab_.reset();
    namCabPrevR_ = std::move(namCabR_);
    namCabR_.reset();
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
    {
        const juce::ScopedLock lock(irLock_);
        cb.ir[slot].bytes.reset();
        cb.ir[slot].name.clear();
        cb.ir[slot].curve.clear();
        cb.ir[slot].mono.setSize(0, 0);
    }
    applyImpulseResponses(cabinet);  // the one that is left is no longer lined up with anything
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

