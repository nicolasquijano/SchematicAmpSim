#pragma once
// A tape echo in the manner of the classic space units of the 1970s: one loop of tape, one record head and three playback heads at fixed distances
// (1, 2 and 3 times the Time). Heads chooses which of them are heard (any of the seven combinations), so one setting gives a single echo, a pair
// or a rhythmic triplet of them. Intensity is the feedback: the repeats go back through the record head, and past 100 % they build up until the
// tape saturates, which gives the self-oscillating swells (it stays bounded). What makes it tape and not a digital delay:
//  - each pass through the loop darkens the sound (Tone sets how far: the head cannot follow the highs) and thins the lows;
//  - the tape saturates softly at the record head (Saturation: how hard it is driven, which also compresses the repeats and rounds their attack);
//  - the speed is never steady: Wow is the slow drift (about half a cycle a second), Flutter the fast wobble (7 to 11 Hz), both bending the pitch of the
//    repeats a little; changing Time speeds the tape up or down, and the repeats glide in pitch as it does.
// Stereo mode spreads the heads across the picture (head 1 on the left, 2 in the middle, 3 on the right); mono puts them in the centre. The echo
// takes the sum of both channels as its input. Switching it on fades the input in over 5 ms; switching it off lets the repeats ring out.

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <vector>

namespace sas {

struct TapeParams {
    bool on = false;
    double timeMs = 180.0;     // 40 .. 800, the distance to the first head
    int heads = 3;             // 0 = 1, 1 = 2, 2 = 3, 3 = 1+2, 4 = 2+3, 5 = 1+3, 6 = 1+2+3
    double intensity = 0.45;   // feedback, 0 .. 1.1
    double mix = 0.3;          // 0 .. 1
    double toneHz = 5000.0;    // 1500 .. 10000, the highs the loop can carry
    double wow = 0.3;          // 0 .. 1
    double flutter = 0.3;      // 0 .. 1
    double saturation = 0.4;   // 0 .. 1
    bool stereo = false;
};

class TapeEcho {
public:
    static constexpr double kMaxMs = 800.0;

    void prepare(double fs) {
        fs_ = fs;
        size_ = 1;
        while (size_ < std::size_t(fs * (3.0 * kMaxMs / 1000.0 + 0.1)) + 8) size_ *= 2;
        buf_.assign(size_, 0.0f);
        mask_ = long(size_ - 1);
        reset();
    }
    void reset() {
        std::fill(buf_.begin(), buf_.end(), 0.0f);
        w_ = 0;
        lp1_ = lp2_ = hp_ = 0.0f;
        inGain_ = target_.on ? 1.0 : 0.0;
        timeCur_ = target_.timeMs;
        fbCur_ = target_.intensity;
        mixCur_ = target_.mix;
        toneCur_ = target_.toneHz;
        tailLeft_ = 0;
        wowPh_ = flutPh1_ = flutPh2_ = driftPh_ = 0.0;
    }
    void setParams(const TapeParams& p) {
        if (target_.on && !p.on) {
            // The repeats ring out for as long as they take to fall by 80 dB (the feedback is held under 80 % while it does).
            const double fb = std::clamp(std::min(p.intensity, 0.8), 0.05, 0.8);
            const double seconds = 3.0 * p.timeMs / 1000.0 * (std::log(1e-4) / std::log(fb)) + 0.3;
            tailLeft_ = long(std::min(seconds, 14.0) * fs_);
        }
        if (!target_.on && p.on) tailLeft_ = 0;
        target_ = p;
        if (!ready_) {
            timeCur_ = p.timeMs;
            fbCur_ = p.intensity;
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
        const double kTime = 1.0 - std::exp(-1.0 / (0.18 * fs_)), kParam = 1.0 - std::exp(-1.0 / (0.02 * fs_)), kIn = 1.0 - std::exp(-1.0 / (0.005 * fs_));
        const double inTarget = target_.on ? 1.0 : 0.0;
        constexpr double kTwoPi = 6.283185307179586;
        static constexpr bool kHeads[7][3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}, {1, 1, 0}, {0, 1, 1}, {1, 0, 1}, {1, 1, 1}};
        const auto& use = kHeads[std::clamp(target_.heads, 0, 6)];
        int count = 0;
        for (bool u : use) count += u ? 1 : 0;
        const float headGain = 1.0f / std::sqrt(float(std::max(1, count)));
        // The pan of each head: left, middle, right (mono: all in the middle).
        const float panL[3] = {target_.stereo ? 1.0f : 0.7071f, 0.7071f, target_.stereo ? 0.25f : 0.7071f};
        const float panR[3] = {target_.stereo ? 0.25f : 0.7071f, 0.7071f, target_.stereo ? 1.0f : 0.7071f};
        const float aHp = float(1.0 - std::exp(-kTwoPi * 90.0 / fs_));
        const double wowMs = 1.7 * std::clamp(target_.wow, 0.0, 1.0), flutMs = 0.05 * std::clamp(target_.flutter, 0.0, 1.0);
        const double satGain = 1.0 + 3.0 * std::clamp(target_.saturation, 0.0, 1.0);
        for (std::size_t i = 0; i < n; ++i) {
            timeCur_ += (target_.timeMs - timeCur_) * kTime;
            fbCur_ += ((target_.on ? target_.intensity : std::min(target_.intensity, 0.8)) - fbCur_) * kParam;
            mixCur_ += (target_.mix - mixCur_) * kParam;
            toneCur_ += (target_.toneHz - toneCur_) * kParam;
            inGain_ += (inTarget - inGain_) * kIn;
            wowPh_ += 0.6 / fs_;
            flutPh1_ += 7.3 / fs_;
            flutPh2_ += 11.1 / fs_;
            driftPh_ += 0.17 / fs_;
            wowPh_ -= std::floor(wowPh_);
            flutPh1_ -= std::floor(flutPh1_);
            flutPh2_ -= std::floor(flutPh2_);
            driftPh_ -= std::floor(driftPh_);
            // The wobble of the tape speed, in ms of delay: the same for every head, since the tape is the same.
            const double wobbleMs = wowMs * (std::sin(kTwoPi * wowPh_) + 0.4 * std::sin(kTwoPi * driftPh_ + 1.3)) / 1.4 +
                                    flutMs * (std::sin(kTwoPi * flutPh1_) + 0.6 * std::sin(kTwoPi * flutPh2_ + 0.7)) / 1.6;
            const double baseMs = std::clamp(timeCur_, 20.0, kMaxMs);
            float wet = 0.0f, wetL = 0.0f, wetR = 0.0f;
            for (int h = 0; h < 3; ++h) {
                if (!use[h]) continue;
                const double d = std::clamp((baseMs * double(h + 1) + wobbleMs) * 0.001 * fs_, 4.0, double(size_) - 4.0);
                const float tap = read(double(w_) - d) * headGain;
                wet += tap;
                wetL += tap * panL[h];
                wetR += tap * panR[h];
            }
            // The loop: the highs the heads cannot follow, the lows they cannot carry, then the record head's saturation.
            const float aLp = float(1.0 - std::exp(-kTwoPi * std::clamp(toneCur_, 800.0, 12000.0) / fs_));
            lp1_ += aLp * (wet - lp1_);
            lp2_ += aLp * (lp1_ - lp2_);
            hp_ += aHp * (lp2_ - hp_);
            const float fed = lp2_ - hp_;
            const float in = 0.5f * (l[i] + r[i]) * float(inGain_);
            const float rec = std::tanh(float(satGain) * (in + float(fbCur_) * fed)) / float(satGain);
            buf_[std::size_t(w_ & mask_)] = std::fabs(rec) < 1e-20f ? 0.0f : rec;
            ++w_;
            const float mix = float(mixCur_);
            l[i] += wetL * mix;
            r[i] += wetR * mix;
        }
        if (!target_.on) {
            tailLeft_ -= long(n);
            if (tailLeft_ <= 0) {
                tailLeft_ = 0;
                std::fill(buf_.begin(), buf_.end(), 0.0f);
                lp1_ = lp2_ = hp_ = 0.0f;
            }
        }
    }

private:
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
    TapeParams target_;
    bool ready_ = false;
    double timeCur_ = 180.0, fbCur_ = 0.45, mixCur_ = 0.3, toneCur_ = 5000.0, inGain_ = 0.0;
    double wowPh_ = 0.0, flutPh1_ = 0.0, flutPh2_ = 0.0, driftPh_ = 0.0;
    float lp1_ = 0.0f, lp2_ = 0.0f, hp_ = 0.0f;
};

}  // namespace sas
