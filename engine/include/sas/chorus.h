#pragma once
// A chorus with two modes in the manner of two classics, both built from short delay lines swept by a triangle LFO and mixed with the dry
// signal; the delayed signal goes through a low-pass and a touch of saturation, like a bucket-brigade chip would leave it.
//  - CE-2: one voice, Rate and Depth free. Warm, with a clear wobble.
//  - Juno: two voices swept in opposite directions, one to each ear (on a single channel, only the first voice), with the three fixed buttons of
//    that synthesiser's chorus: I (slow), II (a little faster) and I+II (fast and shallow, a shimmer).
// Works on one channel (before the amp) or a stereo pair (after it). Switching it on or off fades over 20 ms. The BBD chips are not part of
// the circuit solver: this is the behaviour in DSP, not the solved circuit.

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <vector>

namespace sas {

struct ChorusParams {
    bool on = false;
    bool juno = false;     // false: CE-2, true: Juno
    double rateHz = 1.2;   // CE-2: 0.1 .. 8
    double depth = 0.5;    // CE-2: 0 .. 1
    int junoButton = 0;    // Juno: 0 = I, 1 = II, 2 = I+II
};

class Chorus {
public:
    void prepare(double fs) {
        fs_ = fs;
        size_ = 1;
        while (size_ < std::size_t(fs * 0.03) + 8) size_ *= 2;
        for (auto& b : buf_) b.assign(size_, 0.0f);
        mask_ = long(size_ - 1);
        reset();
    }
    void reset() {
        for (auto& b : buf_) std::fill(b.begin(), b.end(), 0.0f);
        w_ = 0;
        phase_ = 0.0;
        lp_.fill(0.0f);
        amount_ = target_.on ? 1.0 : 0.0;
    }
    void setParams(const ChorusParams& p) {
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
        double rate, centreMs, excursionMs;
        if (target_.juno) {
            static constexpr double kRates[3] = {0.513, 0.863, 9.75};
            static constexpr double kExcursion[3] = {1.25, 1.5, 0.55};
            const int b = std::clamp(target_.junoButton, 0, 2);
            rate = kRates[b];
            centreMs = 3.5;
            excursionMs = kExcursion[b];
        } else {
            rate = target_.rateHz;
            centreMs = 6.5;
            excursionMs = 0.4 + 3.2 * std::clamp(target_.depth, 0.0, 1.0);
        }
        const double inc = rate / fs_;
        const float aLp = float(1.0 - std::exp(-6.283185307179586 * (target_.juno ? 9000.0 : 6000.0) / fs_));
        for (std::size_t i = 0; i < n; ++i) {
            amount_ += ((target_.on ? 1.0 : 0.0) - amount_) * kAmount;
            phase_ += inc;
            if (phase_ >= 1.0) phase_ -= 1.0;
            const double tri = 2.0 * (1.0 - std::fabs(2.0 * phase_ - 1.0)) - 1.0;  // -1 .. 1, triangle
            const float wet = float(amount_);
            const float inL = l[i];
            const float inR = r ? r[i] : inL;
            const double dA = (centreMs + excursionMs * tri) * 0.001 * fs_;
            const double dB = (centreMs - excursionMs * tri) * 0.001 * fs_;
            float vA, vB = 0.0f;
            if (target_.juno) {
                // Both voices hear the mono sum; voice A goes to the left, voice B to the right.
                buf_[0][std::size_t(w_ & mask_)] = r ? 0.5f * (inL + inR) : inL;
                vA = read(buf_[0], double(w_) - dA);
                vB = read(buf_[0], double(w_) - dB);
            } else {
                // Each channel has its own delay line, with the same sweep.
                buf_[0][std::size_t(w_ & mask_)] = inL;
                buf_[1][std::size_t(w_ & mask_)] = inR;
                vA = read(buf_[0], double(w_) - dA);
                if (r) vB = read(buf_[1], double(w_) - dA);
            }
            ++w_;
            // The "chip": a low-pass and a little soft clipping on what was delayed.
            lp_[0] += aLp * (vA - lp_[0]);
            vA = std::tanh(1.2f * lp_[0]) / 1.2f;
            if (target_.juno) {
                if (r) {
                    lp_[1] += aLp * (vB - lp_[1]);
                    vB = std::tanh(1.2f * lp_[1]) / 1.2f;
                    l[i] = inL + (0.5f * (inL + vA) - inL) * wet;
                    r[i] = inR + (0.5f * (inR + vB) - inR) * wet;
                } else {
                    l[i] = inL + (0.5f * (inL + vA) - inL) * wet;  // on one channel: only the first voice
                }
            } else {
                l[i] = inL + (0.5f * (inL + vA) - inL) * wet;
                if (r) {
                    lp_[1] += aLp * (vB - lp_[1]);
                    vB = std::tanh(1.2f * lp_[1]) / 1.2f;
                    r[i] = inR + (0.5f * (inR + vB) - inR) * wet;
                }
            }
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
    std::array<float, 2> lp_{};
    double phase_ = 0.0, amount_ = 0.0;
    ChorusParams target_;
    bool ready_ = false;
};

}  // namespace sas
