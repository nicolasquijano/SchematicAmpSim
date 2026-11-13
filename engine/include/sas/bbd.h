#pragma once
// An analog delay built on a bucket-brigade chip (BBD), in the manner of the Boss DM-2, the Electro-Harmonix Memory Man and the MXR Carbon Copy.
// A BBD is a chain of capacitors that pass their charge on at each tick of a clock: the delay is stages / (2 x clock), so a slower clock gives a
// longer and darker delay. What makes it sound like one:
//  - the chip can only carry what the clock can sample, so the signal goes through a low-pass before it (to stop aliasing) and another after it
//    (to smooth the steps). Their cut-off follows the clock: the longer the delay, the duller the repeats. Tone caps it further;
//  - every repeat goes round the loop, so each one is a little darker than the one before (Feedback; past 100 % it builds up until the loop saturates,
//    which stays bounded);
//  - the chip and its compander are not clean: the loop saturates softly and the repeats carry a hiss that rises and falls with the signal (Grit);
//  - a low-frequency oscillator moves the clock, which bends the pitch of the repeats a little: a chorus inside the delay (Rate, Depth). Turning the
//    Time glides the pitch of what is already in the chip, as it does in the pedals.
// The echo takes the sum of both channels as its input. In stereo mode the right channel's modulation is a quarter of a cycle after the left's, which
// widens the repeats; in mono both are the same. Switching it on fades the input in over 5 ms; switching it off lets the repeats ring out.

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace sas {

struct BbdParams {
    bool on = false;
    double timeMs = 320.0;    // 40 .. 600
    double feedback = 0.4;    // 0 .. 1.1
    double mix = 0.35;        // 0 .. 1
    double toneHz = 5000.0;   // 1500 .. 10000, the most the loop can carry
    double rateHz = 0.8;      // 0.1 .. 6, the clock modulation
    double depth = 0.25;      // 0 .. 1
    double grit = 0.3;        // 0 .. 1, saturation and hiss
    bool stereo = false;
};

class BbdDelay {
public:
    static constexpr double kMaxMs = 600.0;
    static constexpr double kStages = 4096.0;  // an MN3005

    void prepare(double fs) {
        fs_ = fs;
        size_ = 1;
        while (size_ < std::size_t(fs * (kMaxMs / 1000.0 + 0.03)) + 8) size_ *= 2;
        buf_.assign(size_, 0.0f);
        mask_ = long(size_ - 1);
        reset();
    }
    void reset() {
        std::fill(buf_.begin(), buf_.end(), 0.0f);
        w_ = 0;
        for (auto& s : pre_) s = 0.0f;
        for (auto& c : post_)
            for (auto& s : c) s = 0.0f;
        hp_ = 0.0f;
        env_ = 0.0f;
        noiseLp_ = 0.0f;
        inGain_ = target_.on ? 1.0 : 0.0;
        timeCur_ = target_.timeMs;
        fbCur_ = target_.feedback;
        mixCur_ = target_.mix;
        toneCur_ = target_.toneHz;
        tailLeft_ = 0;
        lfoPh_ = 0.0;
    }
    void setParams(const BbdParams& p) {
        if (target_.on && !p.on) {
            // The repeats ring out for as long as they take to fall by 80 dB (the feedback is held under 80 % while it does).
            const double fb = std::clamp(std::min(p.feedback, 0.8), 0.05, 0.8);
            const double seconds = p.timeMs / 1000.0 * (std::log(1e-4) / std::log(fb)) + 0.3;
            tailLeft_ = long(std::min(seconds, 14.0) * fs_);
        }
        if (!target_.on && p.on) tailLeft_ = 0;
        target_ = p;
        if (!ready_) {
            timeCur_ = p.timeMs;
            fbCur_ = p.feedback;
            mixCur_ = p.mix;
            toneCur_ = p.toneHz;
            inGain_ = p.on ? 1.0 : 0.0;
            ready_ = true;
        }
    }
    bool active() const { return target_.on || tailLeft_ > 0; }

    // Stereo pair; the module before it makes a mono signal a pair.
    void process(float* l, float* r, std::size_t n) {
        if (!active()) return;
        constexpr double kTwoPi = 6.283185307179586;
        const double kTime = 1.0 - std::exp(-1.0 / (0.12 * fs_)), kParam = 1.0 - std::exp(-1.0 / (0.02 * fs_)), kIn = 1.0 - std::exp(-1.0 / (0.005 * fs_));
        const double inTarget = target_.on ? 1.0 : 0.0;
        const float aHp = float(1.0 - std::exp(-kTwoPi * 70.0 / fs_));
        const float aEnv = float(1.0 - std::exp(-1.0 / (0.03 * fs_)));
        const double grit = std::clamp(target_.grit, 0.0, 1.0);
        const double satGain = 1.0 + 2.5 * grit;
        const double depth = std::clamp(target_.depth, 0.0, 1.0);
        const double lfoStep = std::clamp(target_.rateHz, 0.05, 8.0) / fs_;
        const double rightOffset = target_.stereo ? 0.25 : 0.0;
        for (std::size_t i = 0; i < n; ++i) {
            timeCur_ += (target_.timeMs - timeCur_) * kTime;
            fbCur_ += ((target_.on ? target_.feedback : std::min(target_.feedback, 0.8)) - fbCur_) * kParam;
            mixCur_ += (target_.mix - mixCur_) * kParam;
            toneCur_ += (target_.toneHz - toneCur_) * kParam;
            inGain_ += (inTarget - inGain_) * kIn;
            lfoPh_ += lfoStep;
            lfoPh_ -= std::floor(lfoPh_);
            const double baseMs = std::clamp(timeCur_, 20.0, kMaxMs - 40.0);
            // The clock modulation moves the delay by up to a few ms (more for the longer delays); a triangle-ish sine, as the pedals' LFOs are.
            const double swingMs = depth * (1.0 + 0.012 * baseMs);
            const double msL = baseMs + swingMs * std::sin(kTwoPi * lfoPh_);
            const double msR = baseMs + swingMs * std::sin(kTwoPi * (lfoPh_ + rightOffset));
            // The chip's bandwidth follows its clock: delay = stages / (2 clock), the filters sit at about a fifth of the clock.
            const double clockHz = kStages / (2.0 * baseMs * 0.001);
            const float cut = float(std::clamp(std::min(clockHz / 2.4, toneCur_), 700.0, 12000.0));
            const float a = float(1.0 - std::exp(-kTwoPi * double(cut) / fs_));
            // The anti-aliasing filter in front of the chip (two poles), shared by the input and the loop.
            const float in = 0.5f * (l[i] + r[i]) * float(inGain_);
            const float tapL = read(double(w_) - clampDelay(msL));
            const float tapR = target_.stereo ? read(double(w_) - clampDelay(msR)) : tapL;
            // After the chip: the smoothing filter (two poles per channel).
            const float sL = smooth(post_[0], tapL, a);
            const float sR = target_.stereo ? smooth(post_[1], tapR, a) : sL;
            // The loop: the lows it cannot carry, then the compander's soft clip.
            hp_ += aHp * (sL - hp_);
            const float fed = sL - hp_;
            float x = in + float(fbCur_) * fed;
            pre_[0] += a * (x - pre_[0]);
            pre_[1] += a * (pre_[0] - pre_[1]);
            x = pre_[1];
            const float rec = std::tanh(float(satGain) * x) / float(satGain);
            buf_[std::size_t(w_ & mask_)] = std::fabs(rec) < 1e-20f ? 0.0f : rec;
            ++w_;
            // The hiss of the chip and its compander: it breathes with the signal, so a silent loop is silent.
            env_ += aEnv * (std::fabs(rec) - env_);
            noise_ = noise_ * 1664525u + 1013904223u;
            const float white = float(std::int32_t(noise_)) * (1.0f / 2147483648.0f);
            noiseLp_ += a * (white - noiseLp_);
            const float hiss = noiseLp_ * env_ * float(0.03 + 0.25 * grit);
            const float mix = float(mixCur_);
            l[i] += (sL + hiss) * mix;
            r[i] += ((target_.stereo ? sR : sL) + hiss) * mix;
        }
        if (!target_.on) {
            tailLeft_ -= long(n);
            if (tailLeft_ <= 0) {
                tailLeft_ = 0;
                std::fill(buf_.begin(), buf_.end(), 0.0f);
                for (auto& s : pre_) s = 0.0f;
                for (auto& c : post_)
                    for (auto& s : c) s = 0.0f;
                hp_ = env_ = noiseLp_ = 0.0f;
            }
        }
    }

private:
    double clampDelay(double ms) const { return std::clamp(ms * 0.001 * fs_, 4.0, double(size_) - 4.0); }
    static float smooth(float* s, float x, float a) {
        s[0] += a * (x - s[0]);
        s[1] += a * (s[0] - s[1]);
        return s[1];
    }
    float read(double pos) const {
        const long i0 = long(std::floor(pos));
        const float f = float(pos - double(i0));
        const float x0 = buf_[std::size_t(i0 & mask_)], x1 = buf_[std::size_t((i0 + 1) & mask_)];
        return x0 + (x1 - x0) * f;
    }

    double fs_ = 48000.0;
    std::size_t size_ = 0;
    long mask_ = 0, w_ = 0, tailLeft_ = 0;
    std::vector<float> buf_;
    BbdParams target_;
    bool ready_ = false;
    double timeCur_ = 320.0, fbCur_ = 0.4, mixCur_ = 0.35, toneCur_ = 5000.0, inGain_ = 0.0, lfoPh_ = 0.0;
    float pre_[2] = {}, post_[2][2] = {}, hp_ = 0.0f, env_ = 0.0f, noiseLp_ = 0.0f;
    std::uint32_t noise_ = 12345u;
};

}  // namespace sas
