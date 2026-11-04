#pragma once
// A tremolo: the level goes up and down at the speed of a low-frequency oscillator. Three waves: Sine (smooth, the tremolo of the old Fender and
// Vox amps), Triangle (a little more pointed) and Square (the choppy, almost on-off pulse of the "harmonic" and photocell units, rounded so it does not
// click). Depth is how far the level falls at the bottom of the wave (0 = no effect, 1 = silence). With Stereo on, a stereo pair is panned from side to
// side instead (the two channels move in opposite directions: an auto-pan). Works on one channel (before the amp) or on a stereo pair (after it).
// Switching it on or off fades over 20 ms.

#include <algorithm>
#include <cmath>
#include <cstddef>

namespace sas {

struct TremoloParams {
    bool on = false;
    double rateHz = 4.0;   // 0.5 .. 15
    double depth = 0.5;    // 0 .. 1
    int shape = 0;         // 0 = sine, 1 = triangle, 2 = square
    bool stereo = false;   // auto-pan on a stereo pair
};

class Tremolo {
public:
    void prepare(double fs) {
        fs_ = fs;
        reset();
    }
    void reset() {
        phase_ = 0.0;
        amount_ = target_.on ? 1.0 : 0.0;
    }
    void setParams(const TremoloParams& p) {
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
        const double inc = target_.rateHz / fs_;
        const double depth = std::clamp(target_.depth, 0.0, 1.0);
        const bool pan = target_.stereo && r;
        for (std::size_t i = 0; i < n; ++i) {
            amount_ += ((target_.on ? 1.0 : 0.0) - amount_) * kAmount;
            phase_ += inc;
            if (phase_ >= 1.0) phase_ -= 1.0;
            const double w = wave(phase_);  // -1 .. 1
            const double wet = amount_ * depth;
            const float gl = float(1.0 - wet * 0.5 * (1.0 - w));  // 1 at the top of the wave, 1 - depth at the bottom
            l[i] *= gl;
            if (r) r[i] *= pan ? float(1.0 - wet * 0.5 * (1.0 + w)) : gl;
        }
    }

private:
    double wave(double ph) const {
        switch (target_.shape) {
            case 1: return -1.0 + 4.0 * (ph < 0.5 ? ph : 1.0 - ph);  // triangle
            case 2: return std::tanh(5.0 * std::sin(6.283185307179586 * ph)) / std::tanh(5.0);                                // rounded square
            default: return std::sin(6.283185307179586 * ph);
        }
    }

    double fs_ = 48000.0, phase_ = 0.0, amount_ = 0.0;
    TremoloParams target_;
    bool ready_ = false;
};

}  // namespace sas
