#pragma once
// A vibe unit in the manner of the classic 1960s pedal: four all-pass stages that are NOT alike (each has its own capacitor, so they sit at
// different frequencies), whose resistors are photocells lit by a lamp that a slow sine oscillator brightens and dims. The photocells answer
// slowly and unevenly (they light up quicker than they fade), which gives the sweep its lopsided, organic motion. Two modes:
//  - Chorus: the dry signal and the phase-shifted one are mixed, which puts soft notches across the spectrum (the sound of "chorus" on that pedal,
//    although nothing is delayed).
//  - Vibrato: only the phase-shifted signal, which as it sweeps bends the pitch a little up and down.
// Speed is the oscillator; Intensity how far the lamp swings (how wide the sweep is). Works on one channel (before the amp) or on a stereo pair
// (after it). Switching it on or off fades over 20 ms. The pedal's own circuit (a lamp, four photocells, transistors) is not in the circuit
// solver: this is its behaviour in DSP, not the solved circuit.

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>

namespace sas {

struct VibeParams {
    bool on = false;
    double speedHz = 3.0;      // 0.3 .. 10
    double intensity = 0.7;    // 0 .. 1
    bool vibrato = false;      // false: chorus
};

class Vibe {
public:
    void prepare(double fs) {
        fs_ = fs;
        reset();
    }
    void reset() {
        for (auto& ch : z_) ch.fill(0.0f);
        phase_ = 0.0;
        lamp_ = 0.5;
        amount_ = target_.on ? 1.0 : 0.0;
    }
    void setParams(const VibeParams& p) {
        if (!ready_) {
            amount_ = p.on ? 1.0 : 0.0;
            ready_ = true;
        }
        target_ = p;
    }
    bool active() const { return target_.on || amount_ > 1e-4; }

    // l is always processed; r only when given (a stereo pair).
    void process(float* l, float* r, std::size_t n) {
        if (!active()) return;
        const double kAmount = 1.0 - std::exp(-1.0 / (0.02 * fs_));
        const double inc = target_.speedHz / fs_;
        // The photocells light up faster (about 12 ms) than they fade (about 60 ms).
        const double kUp = 1.0 - std::exp(-1.0 / (0.012 * fs_)), kDown = 1.0 - std::exp(-1.0 / (0.060 * fs_));
        const double swing = 0.25 + 0.75 * std::clamp(target_.intensity, 0.0, 1.0);
        for (std::size_t i = 0; i < n; ++i) {
            amount_ += ((target_.on ? 1.0 : 0.0) - amount_) * kAmount;
            phase_ += inc;
            if (phase_ >= 1.0) phase_ -= 1.0;
            const double lampNow = 0.5 + 0.5 * std::sin(6.283185307179586 * phase_);  // 0 .. 1
            lamp_ += (lampNow - lamp_) * (lampNow > lamp_ ? kUp : kDown);
            // Resistance of a photocell falls as the lamp brightens: the centre frequency rises exponentially with it.
            const double x = 0.5 + (lamp_ - 0.5) * swing * 2.0;  // 0..1, wider with intensity
            std::array<float, 4> a;
            for (int k = 0; k < 4; ++k) {
                const double f = std::min(kBase[k] * std::pow(kSpan[k], std::clamp(x, 0.0, 1.0)), 0.45 * fs_);
                const double t = std::tan(3.14159265358979323846 * f / fs_);
                a[std::size_t(k)] = float((t - 1.0) / (t + 1.0));
            }
            const float wet = float(amount_);
            l[i] = run(0, l[i], a, wet);
            if (r) r[i] = run(1, r[i], a, wet);
        }
    }

private:
    float run(int ch, float x, const std::array<float, 4>& a, float wet) {
        float s = x;
        for (int k = 0; k < 4; ++k) {
            const float y = a[std::size_t(k)] * s + z_[std::size_t(ch)][std::size_t(k)];
            z_[std::size_t(ch)][std::size_t(k)] = s - a[std::size_t(k)] * y;
            s = y;
        }
        const float out = target_.vibrato ? s : 0.5f * (x + s);
        return x + (out - x) * wet;
    }

    // The four stages sit at different frequencies and sweep over different ranges (each capacitor is different).
    static constexpr std::array<double, 4> kBase{{80.0, 160.0, 420.0, 1100.0}};
    static constexpr std::array<double, 4> kSpan{{14.0, 12.0, 9.0, 6.0}};

    double fs_ = 48000.0;
    std::array<std::array<float, 4>, 2> z_{};
    double phase_ = 0.0, lamp_ = 0.5, amount_ = 0.0;
    VibeParams target_;
    bool ready_ = false;
};

}  // namespace sas
