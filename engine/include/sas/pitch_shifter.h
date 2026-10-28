#pragma once
// Granular delay-line pitch shifter for the guitar (time domain, two Hann-crossfaded read heads). Port of the Rust octave shifter.
//
// No pitch detection is involved, so it works the same for single notes and chords. Each time a read head restarts, its start point is
// nudged (WSOLA-style) to the spot that best matches what the other head is playing, which keeps the grains phase-aligned and avoids
// detuning and warble on tonal material. Latency is about half the window plus half the alignment search.
//
// The window and the search range are in milliseconds, so they mean the same at every sample rate.

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <vector>

namespace sas {

class PitchShifter {
public:
    static constexpr double kWindowMs = 20.0;      // grain length
    static constexpr double kSearchMs = 4.0;       // alignment search range (adds up to this much delay)
    static constexpr double kCorrMs = 8.0;         // how much signal is compared when aligning a grain
    static constexpr double kRatioSmoothMs = 30.0; // the ratio glides so a change of pitch does not click

    void prepare(double fs) {
        fs_ = fs;
        window_ = kWindowMs * 0.001 * fs;
        search_ = std::max<std::size_t>(8, std::size_t(kSearchMs * 0.001 * fs));
        corr_ = std::max<std::size_t>(16, std::size_t(kCorrMs * 0.001 * fs));
        std::size_t size = 1;
        while (size < std::size_t(window_) + search_ + corr_ + 8) size <<= 1;
        buf_.assign(size, 0.0f);
        mask_ = size - 1;
        smooth_ = 1.0 - std::exp(-1.0 / (kRatioSmoothMs * 0.001 * fs));
        reset();
    }

    void reset() {
        std::fill(buf_.begin(), buf_.end(), 0.0f);
        write_ = 0;
        phase_ = 0.0;
        offsets_[0] = offsets_[1] = 0.0;
        ratio_ = 1.0;
    }

    // Delay, in samples, between input and output (the same wherever the pitch is set).
    double latencySamples() const { return window_ * 0.5 + double(search_) * 0.5; }

    // `ratio` is the playback-rate factor: 2 = one octave up, 0.5 = one octave down. In place is fine.
    void process(const float* in, float* out, std::size_t n, double ratio) {
        for (std::size_t i = 0; i < n; ++i) {
            ratio_ += (ratio - ratio_) * smooth_;
            out[i] = processSample(in[i], ratio_);
        }
    }

    static double ratioFromSemitones(double semitones) { return std::exp2(semitones / 12.0); }

private:
    float processSample(float x, double ratio) {
        buf_[write_] = x;
        const double oldPhase = phase_;
        double gains[2], delays[2];
        for (int k = 0; k < 2; ++k) {
            const double p = fract(oldPhase + 0.5 * k);
            const double s = std::sin(kPi * p);
            gains[k] = s * s;  // the two heads sum to exactly 1
            delays[k] = p * window_ + offsets_[k];
        }
        const float y = float(gains[0] * read(delays[0]) + gains[1] * read(delays[1]));

        double newPhase = oldPhase + (1.0 - ratio) / window_;
        newPhase -= std::floor(newPhase);
        if (ratio != 1.0) {
            for (int k = 0; k < 2; ++k) {
                const double pOld = fract(oldPhase + 0.5 * k), pNew = fract(newPhase + 0.5 * k);
                // The phase moves up when the ratio is below 1 and down when it is above; a jump the other way is a wrap.
                const bool wrapped = ratio < 1.0 ? pNew < pOld : pNew > pOld;
                if (wrapped) realign(k, pNew, newPhase);
            }
        }
        phase_ = newPhase;
        write_ = (write_ + 1) & mask_;
        return y;
    }

    // Picks head k's start offset again so it lines up with the other head's current output.
    void realign(int k, double pNew, double phase) {
        const int j = 1 - k;
        const double pOther = fract(phase + 0.5 * j);
        const std::size_t refDelay = std::size_t(pOther * window_ + offsets_[j]);
        const std::size_t base = std::size_t(pNew * window_);
        const std::size_t len = buf_.size();
        auto at = [&](std::size_t delay) { return buf_[(write_ + len - delay) & mask_]; };
        float bestScore = -1e30f;
        std::size_t best = 0;
        for (std::size_t d = 0; d <= search_; ++d) {
            float dot = 0.0f, energy = 1e-9f;
            for (std::size_t i = 0; i < corr_; ++i) {
                const float c = at(base + d + i);
                dot += at(refDelay + i) * c;
                energy += c * c;
            }
            const float score = dot / std::sqrt(energy);
            if (score > bestScore) {
                bestScore = score;
                best = d;
            }
        }
        offsets_[k] = double(best);
    }

    // Linear-interpolated read `delay` samples behind the write head.
    float read(double delay) const {
        const std::size_t whole = std::size_t(delay);
        const float frac = float(delay - double(whole));
        const std::size_t len = buf_.size();
        const std::size_t i0 = (write_ + len - whole) & mask_;
        const std::size_t i1 = (i0 + len - 1) & mask_;
        return buf_[i0] + (buf_[i1] - buf_[i0]) * frac;
    }

    static double fract(double v) { return v - std::floor(v); }
    static constexpr double kPi = 3.14159265358979323846;

    double fs_ = 48000.0, window_ = 960.0, smooth_ = 0.0, phase_ = 0.0, ratio_ = 1.0;
    std::size_t search_ = 192, corr_ = 384, write_ = 0, mask_ = 0;
    double offsets_[2] = {0.0, 0.0};
    std::vector<float> buf_;
};

}  // namespace sas
