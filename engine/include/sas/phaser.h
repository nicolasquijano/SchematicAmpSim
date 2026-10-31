#pragma once
// A four-stage phaser in the manner of the classic orange pedal with a single Speed knob: four first-order all-pass stages with the same
// centre frequency, swept slowly by a triangle LFO (exponentially, so the sweep sounds even), and mixed half and half with the dry signal, which
// puts two notches in the spectrum. "Intense" feeds the output of the chain back to its input (the later, more resonant version of the pedal).
// It works on one channel (before the amp) or on a stereo pair (after it) with the same sweep in both ears. Switching it on or off fades over
// 20 ms. The pedal's own circuit uses JFETs as the variable resistors, which the circuit solver does not have: this is the same structure
// in DSP, not the solved circuit.

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>

namespace sas {

struct PhaserParams {
    bool on = false;
    double speedHz = 0.8;  // 0.1 .. 8
    bool intense = false;  // feedback
};

class Phaser {
public:
    void prepare(double fs) {
        fs_ = fs;
        reset();
    }
    void reset() {
        for (auto& ch : z_) ch.fill(0.0f);
        fbState_.fill(0.0f);
        phase_ = 0.0;
        amount_ = target_.on ? 1.0 : 0.0;
    }
    void setParams(const PhaserParams& p) {
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
        const float fb = target_.intense ? 0.5f : 0.0f;
        for (std::size_t i = 0; i < n; ++i) {
            amount_ += ((target_.on ? 1.0 : 0.0) - amount_) * kAmount;
            phase_ += inc;
            if (phase_ >= 1.0) phase_ -= 1.0;
            // Triangle, rounded a little at the turns, in 0..1; the frequency follows it exponentially between 120 Hz and 3 kHz.
            const double tri = 1.0 - std::fabs(2.0 * phase_ - 1.0);  // 0 .. 1 .. 0 over one cycle
            const double shaped = tri * tri * (3.0 - 2.0 * tri);
            const double f = 120.0 * std::pow(25.0, shaped);
            const double t = std::tan(3.14159265358979323846 * std::min(f, 0.45 * fs_) / fs_);
            const float a = float((t - 1.0) / (t + 1.0));
            const float wet = float(amount_);
            l[i] = stage(0, l[i], a, fb, wet);
            if (r) r[i] = stage(1, r[i], a, fb, wet);
        }
    }

private:
    // The chain of four all-passes for one channel; y[n] = a x[n] + x[n-1] - a y[n-1] for each stage.
    float stage(int ch, float x, float a, float fb, float wet) {
        float s = x + fb * fbState_[std::size_t(ch)];
        for (int k = 0; k < 4; ++k) {
            const float y = a * s + z_[std::size_t(ch)][std::size_t(k)];
            z_[std::size_t(ch)][std::size_t(k)] = s - a * y;
            s = y;
        }
        fbState_[std::size_t(ch)] = std::fabs(s) < 1e-20f ? 0.0f : s;
        const float mixed = 0.5f * (x + s);
        return x + (mixed - x) * wet;
    }

    double fs_ = 48000.0;
    std::array<std::array<float, 4>, 2> z_{};
    std::array<float, 2> fbState_{};
    double phase_ = 0.0, amount_ = 0.0;
    PhaserParams target_;
    bool ready_ = false;
};

}  // namespace sas
