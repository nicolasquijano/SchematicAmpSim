#pragma once
// A clean boost: a plain gain stage (0 to 30 dB) that pushes whatever comes after it (an amp into breakup, a drive into a harder clip) without
// colouring the sound itself. Tone tilts the spectrum around 800 Hz (up to 6 dB either way: below 5 the boost is darker, above it brighter). Soft clip
// rounds the loudest peaks with a gentle curve instead of letting them through (a boost running out of headroom). Works on one channel or on a
// stereo pair (the same on both). Switching it on or off fades over 20 ms.

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>

namespace sas {

struct BoostParams {
    bool on = false;
    double gainDb = 12.0;  // 0 .. 30
    double tone = 0.5;     // 0 .. 1; 0.5 is flat
    bool softClip = false;
};

class Boost {
public:
    void prepare(double fs) {
        fs_ = fs;
        reset();
    }
    void reset() {
        lp_.fill(0.0f);
        amount_ = target_.on ? 1.0 : 0.0;
        gainCur_ = target_.gainDb;
        toneCur_ = target_.tone;
    }
    void setParams(const BoostParams& p) {
        if (!ready_) {
            amount_ = p.on ? 1.0 : 0.0;
            gainCur_ = p.gainDb;
            toneCur_ = p.tone;
            ready_ = true;
        }
        target_ = p;
    }
    bool active() const { return target_.on || amount_ > 1e-4; }

    void process(float* l, float* r, std::size_t n) {
        if (!active()) return;
        const double kAmount = 1.0 - std::exp(-1.0 / (0.02 * fs_)), kParam = 1.0 - std::exp(-1.0 / (0.02 * fs_));
        const float a = float(1.0 - std::exp(-6.283185307179586 * 800.0 / fs_));
        for (std::size_t i = 0; i < n; ++i) {
            amount_ += ((target_.on ? 1.0 : 0.0) - amount_) * kAmount;
            gainCur_ += (target_.gainDb - gainCur_) * kParam;
            toneCur_ += (target_.tone - toneCur_) * kParam;
            const float g = float(std::pow(10.0, gainCur_ / 20.0));
            const float tilt = float((std::clamp(toneCur_, 0.0, 1.0) - 0.5) * 2.0 * 6.0);  // dB, -6 .. 6
            const float gLow = std::pow(10.0f, -tilt / 40.0f), gHigh = std::pow(10.0f, tilt / 40.0f);
            for (int c = 0; c < (r ? 2 : 1); ++c) {
                float* x = c == 0 ? l : r;
                const float in = x[i];
                lp_[std::size_t(c)] += a * (in - lp_[std::size_t(c)]);
                float y = (lp_[std::size_t(c)] * gLow + (in - lp_[std::size_t(c)]) * gHigh) * g;
                if (target_.softClip) y = std::tanh(y * 0.8f) * 1.25f;  // unity for small signals, a soft ceiling near 1.25
                x[i] = in + (y - in) * float(amount_);
            }
        }
    }

private:
    double fs_ = 48000.0;
    std::array<float, 2> lp_{};
    double amount_ = 0.0, gainCur_ = 12.0, toneCur_ = 0.5;
    BoostParams target_;
    bool ready_ = false;
};

}  // namespace sas
