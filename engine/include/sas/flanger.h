#pragma once
// A flanger: the signal is mixed with a copy of itself delayed by a very short time (under 5 ms) that a sine oscillator sweeps up and down, which
// moves a comb of notches through the spectrum (the "jet" sound). The delayed copy goes back to the input by Feedback, which sharpens the notches
// (positive: metallic, ringing; negative: hollow, the odd notches become peaks). Manual sets the centre of the sweep (a low value sweeps the highs,
// a high one the mids), Depth how far it goes either side, Rate how fast. On a stereo pair each channel has its own delay line and, in stereo mode,
// the right one is a quarter of a cycle behind, for width. Works on one channel (before the amp) or on a stereo pair (after it). Switching it on or off
// fades over 20 ms.

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <vector>

namespace sas {

struct FlangerParams {
    bool on = false;
    double rateHz = 0.4;     // 0.05 .. 5
    double depth = 0.7;      // 0 .. 1
    double manual = 0.4;     // 0 .. 1, centre of the sweep
    double feedback = 0.5;   // -0.9 .. 0.9
    bool stereo = false;     // on a stereo pair, the right channel is a quarter of a cycle behind (false: both together)
    double mix = 1.0;      // 0 .. 1: how much of the effect is in the output (1: the effect as it is, 0: the dry signal); applied by the rack module
};

class Flanger {
public:
    void prepare(double fs) {
        fs_ = fs;
        size_ = 1;
        while (size_ < std::size_t(fs * 0.012) + 8) size_ *= 2;
        for (auto& b : buf_) b.assign(size_, 0.0f);
        mask_ = long(size_ - 1);
        reset();
    }
    void reset() {
        for (auto& b : buf_) std::fill(b.begin(), b.end(), 0.0f);
        w_ = 0;
        phase_ = 0.0;
        fb_.fill(0.0f);
        amount_ = target_.on ? 1.0 : 0.0;
    }
    void setParams(const FlangerParams& p) {
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
        const double centreMs = 0.4 + 3.6 * std::clamp(target_.manual, 0.0, 1.0);
        const double swing = 0.8 * std::clamp(target_.depth, 0.0, 1.0);
        const float fb = float(std::clamp(target_.feedback, -0.9, 0.9));
        const int channels = r ? 2 : 1;
        for (std::size_t i = 0; i < n; ++i) {
            amount_ += ((target_.on ? 1.0 : 0.0) - amount_) * kAmount;
            phase_ += inc;
            if (phase_ >= 1.0) phase_ -= 1.0;
            const float wet = float(amount_);
            for (int c = 0; c < channels; ++c) {
                float* x = c == 0 ? l : r;
                double ph = phase_ + (c == 0 || !target_.stereo ? 0.0 : 0.25);
                if (ph >= 1.0) ph -= 1.0;
                const double lfo = std::sin(6.283185307179586 * ph);
                const double d = std::max(1.0, centreMs * (1.0 + swing * lfo) * 0.001 * fs_);
                auto& b = buf_[std::size_t(c)];
                const float in = x[i];
                const float y = read(b, double(w_) - d);
                // Soft limit on the loop so a strong feedback cannot run away.
                fb_[std::size_t(c)] = std::tanh(in + fb * y);
                b[std::size_t(w_ & mask_)] = fb_[std::size_t(c)];
                x[i] = in + (0.5f * (in + y) - in) * wet;
            }
            ++w_;
        }
    }

private:
    float read(const std::vector<float>& b, double pos) const {
        const long i0 = long(std::floor(pos));
        const float f = float(pos - double(i0));
        const float x0 = b[std::size_t(i0 & mask_)], x1 = b[std::size_t((i0 + 1) & mask_)];
        return x0 + (x1 - x0) * f;
    }

    double fs_ = 48000.0;
    std::size_t size_ = 0;
    long mask_ = 0, w_ = 0;
    std::array<std::vector<float>, 2> buf_;
    std::array<float, 2> fb_{};
    double phase_ = 0.0, amount_ = 0.0;
    FlangerParams target_;
    bool ready_ = false;
};

}  // namespace sas
