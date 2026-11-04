#pragma once
// A fuzz pedal with three personalities (in DSP, run at twice the host rate so the harsh edges do not fold back as aliasing):
//  - Face: the two-transistor fuzz of the 1960s. One asymmetric soft clip that sounds ragged and furry; Bias moves the working point: high is
//    smooth, low starves it, which makes it gate and sputter as the note dies away. Tone tilts it from dark to bright.
//  - Muff: the big sustaining fuzz of the 1970s: two clipping stages one after the other, then a tone stack that blends a low-pass with a high-pass
//    (so the middle of the tone knob scoops the mids and the ends are dark or thin). Thick, smooth and endless sustain.
//  - Octave: the signal is full-wave rectified before the clip, which doubles its frequency: a fuzz with an octave above it, thin and ringing, for
//    leads high on the neck.
// Fuzz is the gain, Tone the colour, Level the loudness, Mix blends the dry guitar in (below 100 % the low notes keep their body, a trick for
// bass and for chords). The dry signal is delayed by the same latency as the oversampling so the blend stays in phase. Switching it on or off
// fades over 20 ms.

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <vector>

#include "sas/oversample.h"

namespace sas {

struct FuzzParams {
    enum Type { Face = 0, Muff = 1, Octave = 2 };
    bool on = false;
    int type = Face;
    double fuzz = 0.6;     // 0 .. 1
    double tone = 0.5;     // 0 .. 1
    double bias = 0.7;     // 0 .. 1 (Face and Octave): low starves the transistor
    double mix = 1.0;      // 0 .. 1
    double levelDb = -6.0; // -24 .. 24
};

class Fuzz {
public:
    void prepare(double fs) {
        fs_ = fs;
        os_.reset();
        const double fs2 = 2.0 * fs;
        aHpIn_ = float(1.0 - std::exp(-6.283185307179586 * 60.0 / fs));
        aHpMid_ = float(1.0 - std::exp(-6.283185307179586 * 90.0 / fs2));
        aDc_ = float(1.0 - std::exp(-6.283185307179586 * 25.0 / fs2));
        reset();
    }
    void reset() {
        os_.reset();
        hpIn_ = hpMid_ = dc_ = 0.0f;
        lp_ = hp_ = 0.0f;
        std::fill(dry_.begin(), dry_.end(), 0.0f);
        w_ = 0;
        amount_ = target_.on ? 1.0 : 0.0;
        fuzzCur_ = target_.fuzz;
        toneCur_ = target_.tone;
        biasCur_ = target_.bias;
        mixCur_ = target_.mix;
        levelCur_ = target_.levelDb;
    }
    void setParams(const FuzzParams& p) {
        if (!ready_) {
            amount_ = p.on ? 1.0 : 0.0;
            fuzzCur_ = p.fuzz;
            toneCur_ = p.tone;
            biasCur_ = p.bias;
            mixCur_ = p.mix;
            levelCur_ = p.levelDb;
            ready_ = true;
        }
        target_ = p;
    }
    bool active() const { return target_.on || amount_ > 1e-4; }
    double latencySamples() const { return double(os_.latencySamples()); }

    void process(float* x, float*, std::size_t n) { process(x, n); }  // mono: a right channel is not used
    void process(float* x, std::size_t n) {
        if (!active()) return;
        const double kAmount = 1.0 - std::exp(-1.0 / (0.02 * fs_)), kParam = 1.0 - std::exp(-1.0 / (0.02 * fs_));
        const int delay = int(std::lround(os_.latencySamples()));
        const int type = std::clamp(target_.type, 0, 2);
        for (std::size_t i = 0; i < n; ++i) {
            amount_ += ((target_.on ? 1.0 : 0.0) - amount_) * kAmount;
            fuzzCur_ += (target_.fuzz - fuzzCur_) * kParam;
            toneCur_ += (target_.tone - toneCur_) * kParam;
            biasCur_ += (target_.bias - biasCur_) * kParam;
            mixCur_ += (target_.mix - mixCur_) * kParam;
            levelCur_ += (target_.levelDb - levelCur_) * kParam;

            // The input coupling capacitor, then the gain and shape at twice the rate.
            const float in = x[i];
            hpIn_ += aHpIn_ * (in - hpIn_);
            const float fed = in - hpIn_;
            const float f = float(std::clamp(fuzzCur_, 0.0, 1.0));
            gain_ = type == FuzzParams::Muff ? 6.0f + 70.0f * f * f : 2.0f + 90.0f * f * f;
            offset_ = float(0.08 + 0.62 * (1.0 - std::clamp(biasCur_, 0.0, 1.0)));
            float y = os_.process(fed, [&](float s) { return shape(s, type); });

            // Tone, at the host rate.
            const float t = float(std::clamp(toneCur_, 0.0, 1.0));
            if (type == FuzzParams::Muff) {
                const float aLp = float(1.0 - std::exp(-6.283185307179586 * 380.0 / fs_)), aHp = float(1.0 - std::exp(-6.283185307179586 * 1700.0 / fs_));
                lp_ += aLp * (y - lp_);
                hp_ += aHp * (y - hp_);
                y = (1.0f - t) * lp_ * 1.6f + t * (y - hp_) * 1.3f;  // the scoop is in the middle of the knob
            } else {
                const double fc = 700.0 * std::pow(11.0, double(t));  // 700 Hz .. 7.7 kHz
                const float a = float(1.0 - std::exp(-6.283185307179586 * std::min(fc, 0.45 * fs_) / fs_));
                lp_ += a * (y - lp_);
                y = lp_;
            }
            y *= float(std::pow(10.0, levelCur_ / 20.0));
            // The dry signal, late by the latency of the oversampling.
            dry_[std::size_t(w_ & kMask)] = in;
            const float d = dry_[std::size_t((w_ - delay) & kMask)];
            ++w_;
            const float mix = float(std::clamp(mixCur_, 0.0, 1.0));
            const float out = d * (1.0f - mix) + y * mix;
            x[i] = d + (out - d) * float(amount_);
        }
    }

private:
    float shape(float s, int type) {
        float y;
        switch (type) {
            case FuzzParams::Muff: {
                const float a = std::tanh(gain_ * s);             // first clipping stage
                hpMid_ += aHpMid_ * (a - hpMid_);                 // the coupling between the stages
                y = std::tanh(5.0f * (a - hpMid_)) * 0.55f;       // second stage, harder
                break;
            }
            case FuzzParams::Octave: {
                const float u = gain_ * s;
                y = (std::tanh(std::fabs(u) + offset_) - std::tanh(offset_)) * 0.9f + std::tanh(0.4f * u) * 0.25f;  // the rectified signal and a little of the original
                break;
            }
            default:
                y = (std::tanh(gain_ * s + offset_) - std::tanh(offset_)) * 0.55f;  // asymmetric soft clip
                break;
        }
        dc_ += aDc_ * (y - dc_);  // the rectifying and the asymmetry leave a DC offset
        return y - dc_;
    }

    static constexpr long kMask = 255;  // the dry delay (the oversampling latency is under 30 samples)
    double fs_ = 48000.0;
    Oversampler os_{1};
    float aHpIn_ = 0.0f, aHpMid_ = 0.0f, aDc_ = 0.0f;
    float hpIn_ = 0.0f, hpMid_ = 0.0f, dc_ = 0.0f, lp_ = 0.0f, hp_ = 0.0f;
    float gain_ = 1.0f, offset_ = 0.1f;
    std::array<float, 256> dry_{};
    long w_ = 0;
    double amount_ = 0.0, fuzzCur_ = 0.6, toneCur_ = 0.5, biasCur_ = 0.7, mixCur_ = 1.0, levelCur_ = -6.0;
    FuzzParams target_;
    bool ready_ = false;
};

}  // namespace sas
