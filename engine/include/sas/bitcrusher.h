#pragma once
// A bitcrusher / lo-fi effect: it throws away resolution (Bits) and sampling rate (Rate), as the first samplers, game consoles and telephone codecs did.
//  - Bits: the signal is rounded to 2^(bits - 1) steps on each side of zero (1 to 16 bits; fractions work: 7.5 bits is a smoother in-between). With few bits
//    the rounding error is no longer noise but a distortion that follows the signal: the grainy, gritty sound. Dither adds a little triangular noise
//    before the rounding, which turns that distortion into a steady hiss (more natural, and the quietest notes still come through).
//  - Rate: a sample-and-hold at that rate. The held steps fold what is above half of it back down as inharmonic aliasing (the metallic, digital sound); the
//    Anti-alias switch puts a low-pass in front of the hold (as a real converter has), which makes it a duller, cleaner lo-fi. Jitter makes the hold's clock
//    wobble a little, which spreads the aliasing into a rougher grain.
//  - Tone: a low-pass after the crusher (two poles) to take the edge off the steps.
// The two channels are processed apart. Mix is not in here (the rack module's Mix does it).

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace sas {

struct BitcrusherParams {
    bool on = false;
    double bits = 8.0;       // 1 .. 16
    double rateHz = 12000.0; // 500 .. 48000, the sample-and-hold rate (at or above the host rate: no hold)
    bool antiAlias = false;  // a low-pass at 0.45 x the rate in front of the hold
    double toneHz = 12000.0; // 1000 .. 20000, the low-pass after the crusher
    double dither = 0.0;     // 0 .. 1
    double jitter = 0.0;     // 0 .. 1
    double mix = 1.0;        // 0 .. 1 (used by the rack module)
};

class Bitcrusher {
public:
    void prepare(double fs) {
        fs_ = fs;
        reset();
    }
    void reset() {
        for (auto& c : ch_) c = Chan{};
        rng_ = 22222u;
        phase_ = 0.0;
    }
    void setParams(const BitcrusherParams& p) { target_ = p; }
    bool active() const { return target_.on; }

    // `r` may be null (a single channel).
    void process(float* l, float* r, std::size_t n) {
        if (!active()) return;
        constexpr double kTwoPi = 6.283185307179586;
        const double rate = std::clamp(target_.rateHz, 200.0, fs_);
        const double step = rate / fs_;  // the hold's phase advance per sample (1 = every sample is a new one)
        const bool hold = step < 0.9999;
        const double half = std::exp2(std::clamp(target_.bits, 1.0, 16.0) - 1.0);  // steps on each side of zero
        const float aPre = float(1.0 - std::exp(-kTwoPi * std::min(0.45 * rate, 0.45 * fs_) / fs_));
        const float aTone = float(1.0 - std::exp(-kTwoPi * std::clamp(target_.toneHz, 500.0, 0.45 * fs_) / fs_));
        const bool pre = target_.antiAlias && hold;
        const float dither = float(std::clamp(target_.dither, 0.0, 1.0));
        const double jitter = std::clamp(target_.jitter, 0.0, 1.0);
        for (std::size_t i = 0; i < n; ++i) {
            // One clock for both channels, so they stay together.
            bool tick = true;
            if (hold) {
                phase_ += step * (1.0 + jitter * 0.9 * noise());
                if (phase_ >= 1.0) phase_ -= std::floor(phase_);
                else tick = false;
            }
            for (int c = 0; c < (r ? 2 : 1); ++c) {
                float* buf = c == 0 ? l : r;
                Chan& s = ch_[c];
                float x = buf[i];
                if (pre) {
                    s.p1 += aPre * (x - s.p1);
                    s.p2 += aPre * (s.p1 - s.p2);
                    x = s.p2;
                }
                if (tick) {
                    const float d = dither > 0.0f ? dither * 0.5f * (noise() + noise()) / float(half) : 0.0f;  // triangular, up to 1 LSB
                    const float v = std::clamp(x, -1.0f, 1.0f) + d;
                    s.held = float(std::floor(double(v) * half + 0.5) / half);
                }
                s.t1 += aTone * (s.held - s.t1);
                s.t2 += aTone * (s.t1 - s.t2);
                buf[i] = std::fabs(s.t2) < 1e-20f ? 0.0f : s.t2;
            }
        }
    }

private:
    float noise() {  // uniform, -1 .. 1
        rng_ = rng_ * 1664525u + 1013904223u;
        return float(std::int32_t(rng_)) * (1.0f / 2147483648.0f);
    }
    struct Chan {
        float p1 = 0.0f, p2 = 0.0f, held = 0.0f, t1 = 0.0f, t2 = 0.0f;
    };
    double fs_ = 48000.0, phase_ = 0.0;
    Chan ch_[2];
    std::uint32_t rng_ = 22222u;
    BitcrusherParams target_;
};

}  // namespace sas
