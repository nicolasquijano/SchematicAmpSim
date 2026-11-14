#pragma once
// An analog octaver in the manner of the classic analog octave pedals: the guitar goes through a low-pass filter that keeps the fundamental, a Schmitt comparator
// squares it, two flip-flops divide the square wave by 2 and by 4 (one and two octaves down), and each divided square wave is given the
// loudness of the playing (an envelope follower, so it answers the pick) and rounded by a low-pass filter. The result is mixed with the direct
// signal. It is monophonic like the pedal: it tracks one note at a time, and chords or the highest notes (above about 1 kHz) make it
// warble, which is part of the sound.
//
// The filters follow the pitch the comparator measures: the input filter sits just above the fundamental (so the harmonics of the low
// strings do not trigger the comparator twice) and each octave's filter sits a little above its own note (TONE opens it: 0 is nearly a sine,
// 1 keeps the square's buzz). Works on one channel; the module sums a stereo pair to mono first. Switching it on or off fades over 20 ms.

#include <algorithm>
#include <cmath>
#include <cstddef>

namespace sas {

struct OctaverParams {
    bool on = false;
    double direct = 1.0;  // 0 .. 1, the guitar as it is
    double oct1 = 0.7;    // 0 .. 1, one octave down
    double oct2 = 0.0;    // 0 .. 1, two octaves down
    double tone = 0.3;    // 0 .. 1, how much of the square wave's buzz the octaves keep
};

class Octaver {
public:
    void prepare(double fs) {
        fs_ = fs;
        reset();
    }
    void reset() {
        hp_ = 0.0f;
        for (auto& v : pre_) v = 0.0f;
        for (auto& v : lp1_) v = 0.0f;
        for (auto& v : lp2_) v = 0.0f;
        envPeak_ = envAmp_ = 0.0f;
        high_ = false;
        flipA_ = flipB_ = -1.0f;
        sinceEdge_ = 0.0;
        sinceFlip_ = 1e9;
        freq_ = 220.0;
        amount_ = target_.on ? 1.0 : 0.0;
    }
    void setParams(const OctaverParams& p) {
        if (!ready_) {
            amount_ = p.on ? 1.0 : 0.0;
            ready_ = true;
        }
        target_ = p;
    }
    bool active() const { return target_.on || amount_ > 1e-4; }

    void process(float* l, float* /*r*/, std::size_t n) {
        if (!active()) return;
        const double kAmount = 1.0 - std::exp(-1.0 / (0.02 * fs_));
        const float peakDecay = float(std::exp(-1.0 / (0.05 * fs_)));
        const float ampAttack = float(1.0 - std::exp(-1.0 / (0.002 * fs_))), ampRelease = float(1.0 - std::exp(-1.0 / (0.06 * fs_)));
        const float hpA = float(1.0 - std::exp(-6.283185307179586 * 45.0 / fs_));
        const double minGap = 0.0004 * fs_;  // the comparator cannot fire again within 0.4 ms
        for (std::size_t i = 0; i < n; ++i) {
            amount_ += ((target_.on ? 1.0 : 0.0) - amount_) * kAmount;
            const float in = l[i];

            // The tracking input filter: a 45 Hz high-pass, then four poles just above the measured fundamental.
            hp_ += hpA * (in - hp_);
            float x = in - hp_;
            const float aPre = coef(std::clamp(1.4 * freq_, 100.0, 1000.0));
            for (int k = 0; k < 4; ++k) {
                pre_[k] += aPre * (x - pre_[k]);
                x = pre_[k];
            }

            // The comparator: a Schmitt trigger whose thresholds follow the level, so the noise between notes does not fire it.
            envPeak_ = std::max(std::fabs(x), envPeak_ * peakDecay);
            const float thr = std::max(0.002f, 0.2f * envPeak_);
            sinceEdge_ += 1.0;
            sinceFlip_ += 1.0;
            if (!high_ && x > thr) {
                high_ = true;
                if (sinceFlip_ > minGap) {
                    sinceFlip_ = 0.0;
                    const double f = fs_ / std::max(1.0, sinceEdge_);
                    if (f > 60.0 && f < 1400.0) freq_ += 0.25 * (f - freq_);
                    sinceEdge_ = 0.0;
                    flipA_ = -flipA_;
                    if (flipA_ > 0.0f) flipB_ = -flipB_;  // the second flip-flop toggles on the first one's rising edge
                }
            } else if (high_ && x < -thr) {
                high_ = false;
            }
            if (sinceEdge_ > 0.15 * fs_) freq_ += (220.0 - freq_) * 0.0005;  // no note: drift back to a middle value

            // The loudness of the playing gives the divided square waves their size (and silences them when the guitar stops).
            const float level = std::fabs(in - hp_);
            envAmp_ += (level > envAmp_ ? ampAttack : ampRelease) * (level - envAmp_);
            const float amp = envPeak_ < 0.0005f ? 0.0f : envAmp_ * 1.5f;

            // Each octave: its square wave, rounded by two poles that sit a little above its own note.
            const double mult = 0.6 + 4.0 * std::clamp(target_.tone, 0.0, 1.0);
            const float a1 = coef(std::clamp(mult * freq_ * 0.5, 40.0, 900.0));
            const float a2 = coef(std::clamp(mult * freq_ * 0.25, 30.0, 700.0));
            const float s1 = flipA_ * amp * 0.5f, s2 = flipB_ * amp * 0.5f;
            lp1_[0] += a1 * (s1 - lp1_[0]);
            lp1_[1] += a1 * (lp1_[0] - lp1_[1]);
            lp2_[0] += a2 * (s2 - lp2_[0]);
            lp2_[1] += a2 * (lp2_[0] - lp2_[1]);

            const float wet = float(target_.oct1) * lp1_[1] + float(target_.oct2) * lp2_[1];
            const float direct = 1.0f + (float(target_.direct) - 1.0f) * float(amount_);
            l[i] = in * direct + wet * float(amount_);
        }
    }

private:
    // One-pole smoothing coefficient for a corner frequency.
    float coef(double fc) const {
        const double g = 6.283185307179586 * fc / fs_;
        return float(g / (1.0 + g));
    }

    double fs_ = 48000.0;
    float hp_ = 0.0f;
    float pre_[4] = {};
    float lp1_[2] = {}, lp2_[2] = {};
    float envPeak_ = 0.0f, envAmp_ = 0.0f;
    bool high_ = false;
    float flipA_ = -1.0f, flipB_ = -1.0f;
    double sinceEdge_ = 0.0, sinceFlip_ = 1e9, freq_ = 220.0;
    double amount_ = 0.0;
    OctaverParams target_;
    bool ready_ = false;
};

}  // namespace sas
