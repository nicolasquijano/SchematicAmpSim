#pragma once
// Stereo delay for the end of the chain: up to 1.5 s, feedback with a tone control (a low-pass in the loop so every repeat is darker than the
// last), optional ping-pong (the repeats bounce left, right, left...) and an "analog" mode (a steeper low-pass, a high-pass, soft saturation
// and a slow wobble in the delay time, like a tape or bucket-brigade unit). The time glides when it moves, so turning the knob bends the pitch
// of the repeats instead of clicking. Switching it off lets the repeats ring out: the input stops and the line empties by itself.

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <vector>

namespace sas {

struct DelayParams {
    bool on = false;
    double timeMs = 380.0;    // 20 .. 1500
    double feedback = 0.35;   // 0 .. 0.92
    double mix = 0.25;        // wet level added to the dry signal, 0 .. 1
    double toneHz = 6500.0;   // low-pass in the feedback loop
    bool pingPong = false;
    bool analog = false;
};

class StereoDelay {
public:
    static constexpr double kMaxMs = 1500.0;

    void prepare(double fs) {
        fs_ = fs;
        size_ = 1;
        while (size_ < std::size_t(fs * (kMaxMs / 1000.0 + 0.15)) + 8) size_ *= 2;
        bufL_.assign(size_, 0.0f);
        bufR_.assign(size_, 0.0f);
        mask_ = long(size_ - 1);
        reset();
    }
    void reset() {
        std::fill(bufL_.begin(), bufL_.end(), 0.0f);
        std::fill(bufR_.begin(), bufR_.end(), 0.0f);
        w_ = 0;
        lp1L_ = lp1R_ = lp2L_ = lp2R_ = hpL_ = hpR_ = 0.0f;
        inGain_ = target_.on ? 1.0 : 0.0;
        timeCur_ = target_.timeMs;
        fbCur_ = target_.feedback;
        mixCur_ = target_.mix;
        toneCur_ = target_.toneHz;
        tailLeft_ = 0;
        phase_ = 0.0;
    }

    void setParams(const DelayParams& p) {
        if (target_.on && !p.on) {
            // The repeats of whatever is in the line ring out for as long as they take to fall by 80 dB.
            const double fb = std::clamp(p.feedback, 0.01, 0.92);
            const double seconds = p.timeMs / 1000.0 * (std::log(1e-4) / std::log(fb)) + 0.3;
            tailLeft_ = long(std::min(seconds, 14.0) * fs_);
        }
        if (!target_.on && p.on) tailLeft_ = 0;
        target_ = p;
        if (!ready_) {  // first call: start from the target instead of gliding from the defaults
            timeCur_ = p.timeMs;
            fbCur_ = p.feedback;
            mixCur_ = p.mix;
            toneCur_ = p.toneHz;
            inGain_ = p.on ? 1.0 : 0.0;
            ready_ = true;
        }
    }
    bool active() const { return target_.on || tailLeft_ > 0; }

    void process(float* l, float* r, std::size_t n) {
        if (!active()) return;
        const double kTime = 1.0 - std::exp(-1.0 / (0.06 * fs_)), kParam = 1.0 - std::exp(-1.0 / (0.02 * fs_)), kIn = 1.0 - std::exp(-1.0 / (0.005 * fs_));
        const double inTarget = target_.on ? 1.0 : 0.0;
        const double lfoInc = 6.283185307179586 * 0.55 / fs_;
        for (std::size_t i = 0; i < n; ++i) {
            timeCur_ += (target_.timeMs - timeCur_) * kTime;
            fbCur_ += (target_.feedback - fbCur_) * kParam;
            mixCur_ += (target_.mix - mixCur_) * kParam;
            toneCur_ += (target_.toneHz - toneCur_) * kParam;
            inGain_ += (inTarget - inGain_) * kIn;
            phase_ += lfoInc;
            if (phase_ > 6.283185307179586) phase_ -= 6.283185307179586;

            const double wobble = target_.analog ? 0.0007 * fs_ * std::sin(phase_) : 0.0;
            const double d = std::clamp(timeCur_ * 0.001 * fs_ + wobble, 2.0, double(size_) - 4.0);
            const float dl = read(bufL_, double(w_) - d), dr = read(bufR_, double(w_) - d);

            // The loop filter: one low-pass at the tone setting; in analog mode two of them a little lower plus a high-pass.
            const double cut = target_.analog ? std::min(toneCur_, 4200.0) : toneCur_;
            const float a = float(1.0 - std::exp(-6.283185307179586 * cut / fs_));
            lp1L_ += a * (dl - lp1L_);
            lp1R_ += a * (dr - lp1R_);
            float wl = lp1L_, wr = lp1R_;
            if (target_.analog) {
                lp2L_ += a * (wl - lp2L_);
                lp2R_ += a * (wr - lp2R_);
                const float ah = float(1.0 - std::exp(-6.283185307179586 * 110.0 / fs_));
                hpL_ += ah * (lp2L_ - hpL_);
                hpR_ += ah * (lp2R_ - hpR_);
                wl = std::tanh(1.1f * (lp2L_ - hpL_));
                wr = std::tanh(1.1f * (lp2R_ - hpR_));
            }
            const float fb = float(fbCur_), g = float(inGain_);
            const float inL = l[i] * g, inR = r[i] * g;
            if (target_.pingPong) {
                const float m = 0.5f * (inL + inR);
                bufL_[std::size_t(w_ & mask_)] = m + wr * fb;  // the left line is fed by the input and by the right repeat
                bufR_[std::size_t(w_ & mask_)] = wl * fb;      // the right line only by the left repeat
            } else {
                bufL_[std::size_t(w_ & mask_)] = inL + wl * fb;
                bufR_[std::size_t(w_ & mask_)] = inR + wr * fb;
            }
            ++w_;
            const float mix = float(mixCur_);
            l[i] += wl * mix;
            r[i] += wr * mix;
        }
        if (!target_.on) {
            tailLeft_ -= long(n);
            if (tailLeft_ <= 0) {
                tailLeft_ = 0;
                std::fill(bufL_.begin(), bufL_.end(), 0.0f);
                std::fill(bufR_.begin(), bufR_.end(), 0.0f);
                lp1L_ = lp1R_ = lp2L_ = lp2R_ = hpL_ = hpR_ = 0.0f;
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
    long mask_ = 0, w_ = 0, tailLeft_ = 0;
    std::vector<float> bufL_, bufR_;
    DelayParams target_;
    bool ready_ = false;
    double timeCur_ = 380.0, fbCur_ = 0.35, mixCur_ = 0.25, toneCur_ = 6500.0, inGain_ = 0.0, phase_ = 0.0;
    float lp1L_ = 0, lp1R_ = 0, lp2L_ = 0, lp2R_ = 0, hpL_ = 0, hpR_ = 0;
};

}  // namespace sas
