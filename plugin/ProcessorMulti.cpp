#include "ProcessorCommon.h"

#include <limits>

#include "sas/multi_ir.h"

// The multi impulse loader: eight impulses that a cursor on a plane mixes (cabinet mode MULTI of the first cabinet). Each impulse has its own
// polarity, delay and filters, baked into it when it is loaded or when one of those moves (message thread); level, solo and the cursor only change
// gains, ramped over each block (audio thread). An impulse nobody hears is not convolved at all.

bool SasProcessor::anyMultiLoaded() const {
    for (const auto& s : multi_)
        if (s.loaded.load()) return true;
    return false;
}

juce::String SasProcessor::multiImpulseName(int slot) const {
    if (slot < 0 || slot >= kMultiSlots) return {};
    const juce::ScopedLock lock(irLock_);
    return multi_[slot].name;
}

std::vector<float> SasProcessor::multiImpulseCurve(int slot) const {
    if (slot < 0 || slot >= kMultiSlots) return {};
    const juce::ScopedLock lock(irLock_);
    return multi_[slot].curve;
}

bool SasProcessor::loadMultiImpulse(const juce::File& file, int slot) {
    juce::MemoryBlock bytes;
    if (!file.existsAsFile() || !file.loadFileAsData(bytes) || bytes.getSize() == 0) return false;
    return loadMultiImpulseFromMemory(bytes.getData(), bytes.getSize(), file.getFileName(), slot);
}

bool SasProcessor::loadMultiImpulseFromMemory(const void* data, std::size_t size, const juce::String& name, int slot) {
    if (slot < 0 || slot >= kMultiSlots) return false;
    juce::AudioBuffer<float> mono;
    double rate = 0.0;
    std::vector<float> curve;
    if (!decodeImpulse(data, size, mono, rate, curve)) return false;
    {
        const juce::ScopedLock lock(irLock_);
        multi_[slot].bytes = juce::MemoryBlock(data, size);
        multi_[slot].name = name;
        multi_[slot].curve = std::move(curve);
        multi_[slot].mono = std::move(mono);
        multi_[slot].rate = rate;
    }
    multi_[slot].loaded = true;
    refreshMulti(slot);
    return true;
}

bool SasProcessor::loadBuiltInMultiImpulse(int index, int slot) {
    if (index < 0 || index >= CabData::namedResourceListSize) return false;
    int size = 0;
    const char* data = CabData::getNamedResource(CabData::namedResourceList[index], size);
    return data != nullptr && loadMultiImpulseFromMemory(data, std::size_t(size), builtInImpulseResponseName(index), slot);
}

void SasProcessor::clearMultiImpulse(int slot) {
    if (slot < 0 || slot >= kMultiSlots) return;
    multi_[slot].loaded = false;
    {
        const juce::ScopedLock lock(irLock_);
        multi_[slot].bytes.reset();
        multi_[slot].name.clear();
        multi_[slot].curve.clear();
        multi_[slot].mono.setSize(0, 0);
        multiAlignShown_[slot] = MultiAlignInfo{};
    }
    if (mcAlignP_->load() > 0.5f) refreshMulti(-1);  // the others were lined up with this one
}

SasProcessor::MultiAlignInfo SasProcessor::multiAlignInfo(int slot) const {
    if (slot < 0 || slot >= kMultiSlots) return {};
    const juce::ScopedLock lock(irLock_);
    return multiAlignShown_[slot];
}

// One slot (its own settings changed or it was just loaded), or, with "in phase" on or slot -1, all of them: with "in phase" every impulse is
// lined up with the first one, so loading or removing any of them can move the others.
void SasProcessor::refreshMulti(int slot) {
    const bool align = mcAlignP_->load() > 0.5f;
    if (!align && slot >= 0) {
        bakeMulti(slot);
        return;
    }
    if (align) {
        recomputeMultiAlign();
    } else {
        const juce::ScopedLock lock(irLock_);
        for (int k = 0; k < kMultiSlots; ++k) {
            multiExtra_[k] = 0;
            multiFlip_[k] = false;
            multiAlignShown_[k] = MultiAlignInfo{};
        }
    }
    for (int k = 0; k < kMultiSlots; ++k) bakeMulti(k);
}

// The cross-correlation of every loaded impulse with the first one (each with its own polarity setting, and without its silence in front):
// what each needs in delay and polarity to add up with it.
void SasProcessor::recomputeMultiAlign() {
    constexpr float kSilence = 1.0e-4f;
    std::vector<float> data[kMultiSlots];
    const float* irs[kMultiSlots] = {};
    std::size_t lengths[kMultiSlots] = {};
    double rates[kMultiSlots] = {};
    bool loaded[kMultiSlots] = {};
    int leads[kMultiSlots] = {};
    {
        const juce::ScopedLock lock(irLock_);
        for (int k = 0; k < kMultiSlots; ++k) {
            if (!multi_[k].loaded.load() || multi_[k].mono.getNumSamples() == 0) continue;
            const float* src = multi_[k].mono.getReadPointer(0);
            const int n = multi_[k].mono.getNumSamples();
            int lead = 0;
            while (lead < n - 1 && std::abs(src[lead]) <= kSilence) ++lead;
            leads[k] = lead;
            const bool flip = sas::impulseFlipped(src, std::size_t(n), juce::jlimit(0, 2, int(mcPolP_[k]->load() + 0.5f)));
            data[k].assign(src, src + n);
            if (flip)
                for (auto& v : data[k]) v = -v;
            irs[k] = data[k].data();
            lengths[k] = data[k].size();
            rates[k] = multi_[k].rate;
            loaded[k] = true;
        }
    }
    const auto r = sas::alignMultiImpulses(irs, lengths, rates, loaded);
    const juce::ScopedLock lock(irLock_);
    // The silence all of them share in front (their own plus what they are delayed by) is taken off every one: no latency for nothing.
    multiCommonLead_ = 0;
    if (r.reference >= 0) {
        int common = std::numeric_limits<int>::max();
        for (int k = 0; k < kMultiSlots; ++k)
            if (loaded[k]) common = std::min(common, leads[k] + r.extraDelay[k]);
        multiCommonLead_ = common == std::numeric_limits<int>::max() ? 0 : common;
    }
    for (int k = 0; k < kMultiSlots; ++k) {
        multiExtra_[k] = r.reference >= 0 && loaded[k] ? r.extraDelay[k] : 0;
        multiFlip_[k] = r.reference >= 0 && loaded[k] && r.flip[k];
        multiAlignShown_[k] = MultiAlignInfo{};
        if (r.reference >= 0 && loaded[k]) {
            multiAlignShown_[k].valid = true;
            multiAlignShown_[k].flipped = multiFlip_[k];
            multiAlignShown_[k].delayMs = 1000.0 * double(multiExtra_[k]) / rates[k];
        }
    }
}

// The slot's impulse as it is convolved: its own silence in front removed (as JUCE does for the other modes), then polarity, delay and filters.
void SasProcessor::bakeMulti(int slot) {
    auto& s = multi_[slot];
    if (!s.loaded.load()) return;
    juce::AudioBuffer<float> source;
    double rate;
    {
        const juce::ScopedLock lock(irLock_);
        source.makeCopyOf(s.mono);
        rate = s.rate;
    }
    if (source.getNumSamples() == 0 || rate <= 0.0) return;
    constexpr float kSilence = 1.0e-4f;  // -80 dB
    // With "in phase" the impulses keep their silence in front (it is the time difference between the microphones) and lose the shared part of it.
    const bool aligned = mcAlignP_->load() > 0.5f && multiAlignShown_[slot].valid;
    int lead = 0;
    if (!aligned)
        while (lead < source.getNumSamples() - 1 && std::abs(source.getSample(0, lead)) <= kSilence) ++lead;
    sas::IrBake bake;
    // Polarity: the slot's own setting, then (with "in phase" on) the flip that lines it up with the first impulse, and its extra delay.
    bool flip = sas::impulseFlipped(source.getReadPointer(0) + lead, std::size_t(source.getNumSamples() - lead), juce::jlimit(0, 2, int(mcPolP_[slot]->load() + 0.5f)));
    bake.delayMs = double(mcDelayP_[slot]->load());
    if (aligned) {
        flip = flip != multiFlip_[slot];
        bake.delayMs += 1000.0 * double(multiExtra_[slot]) / rate;
    }
    bake.polarity = flip ? 2 : 1;
    bake.hpHz = double(mcHpP_[slot]->load());
    bake.lpHz = double(mcLpP_[slot]->load());
    const auto baked = sas::bakeImpulse(source.getReadPointer(0) + lead, std::size_t(source.getNumSamples() - lead), rate, bake);
    int end = int(baked.size());
    while (end > 1 && std::abs(baked[std::size_t(end - 1)]) <= kSilence) --end;
    const int drop = aligned ? std::min(multiCommonLead_, end - 1) : 0;
    juce::AudioBuffer<float> buffer(1, std::max(1, end - drop));
    for (int i = drop; i < end; ++i) buffer.setSample(0, i - drop, baked[std::size_t(i)]);
    auto curve = impulseCurve(buffer, rate);
    juce::AudioBuffer<float> twin(buffer);
    s.conv.loadImpulseResponse(std::move(buffer), rate, juce::dsp::Convolution::Stereo::no, juce::dsp::Convolution::Trim::no, juce::dsp::Convolution::Normalise::no);
    s.convR.loadImpulseResponse(std::move(twin), rate, juce::dsp::Convolution::Stereo::no, juce::dsp::Convolution::Trim::no, juce::dsp::Convolution::Normalise::no);
    const juce::ScopedLock lock(irLock_);
    s.curve = std::move(curve);
}

// Audio thread. `m` (and `mR` for a stereo pair) go in, the mix of the impulses the cursor reaches comes out.
void SasProcessor::processMulti(float* m, float* mR, bool pair, std::size_t nn) {
    const int n = int(nn);
    float xs[kMultiSlots], ys[kMultiSlots], loaded[kMultiSlots], w[kMultiSlots], target[kMultiSlots];
    bool anySolo = false;
    for (int k = 0; k < kMultiSlots; ++k) {
        xs[k] = mcXP_[k]->load();
        ys[k] = mcYP_[k]->load();
        loaded[k] = multi_[k].loaded.load() ? 1.0f : 0.0f;
        if (loaded[k] > 0.0f && mcSoloP_[k]->load() > 0.5f) anySolo = true;
    }
    if (anySolo)  // only the soloed ones take part, and they share the power between them
        for (int k = 0; k < kMultiSlots; ++k)
            if (mcSoloP_[k]->load() <= 0.5f) loaded[k] = 0.0f;
    const float spread = 0.08f + 0.03f * mcSpreadP_->load();
    sas::multiIrWeights(mcCursorXP_->load(), mcCursorYP_->load(), xs, ys, loaded, kMultiSlots, spread, 0.003f, w);
    for (int k = 0; k < kMultiSlots; ++k) {
        const float db = mcLevelP_[k]->load();
        target[k] = w[k] * (db <= -23.9f ? 0.0f : juce::Decibels::decibelsToGain(db));
        multiShown_[k] = w[k];
    }
    float* acc = multiAcc_.data();
    float* accR = multiAccR_.data();
    std::fill(acc, acc + n, 0.0f);
    if (pair) std::fill(accR, accR + n, 0.0f);
    const int piece = convBlock_;
    auto convolve = [n, piece](juce::dsp::Convolution& conv, float* data) {
        for (int from = 0; from < n; from += piece) {
            float* chans[1] = {data + from};
            juce::dsp::AudioBlock<float> block(chans, 1, std::size_t(std::min(piece, n - from)));
            conv.process(juce::dsp::ProcessContextReplacing<float>(block));
        }
    };
    for (int k = 0; k < kMultiSlots; ++k) {
        if (!(target[k] > 0.0f) && !(multiGainNow_[k] > 0.0f)) {
            multiRunning_[k] = false;  // nobody hears it: no convolution, and it starts fresh when the cursor comes back
            continue;
        }
        if (!multiRunning_[k]) {
            multi_[k].conv.reset();
            multi_[k].convR.reset();
            multiRunning_[k] = true;
        }
        const float g0 = multiGainNow_[k], g1 = target[k];
        float* tmp = mono2_.data();
        std::copy(m, m + n, tmp);
        convolve(multi_[k].conv, tmp);
        for (int i = 0; i < n; ++i) acc[i] += tmp[i] * (g0 + (g1 - g0) * (float(i + 1) / float(n)));
        if (pair) {
            float* tmpR = mono3_.data();
            std::copy(mR, mR + n, tmpR);
            convolve(multi_[k].convR, tmpR);
            for (int i = 0; i < n; ++i) accR[i] += tmpR[i] * (g0 + (g1 - g0) * (float(i + 1) / float(n)));
        }
        multiGainNow_[k] = g1;
    }
    std::copy(acc, acc + n, m);
    if (pair) std::copy(accR, accR + n, mR);
}
