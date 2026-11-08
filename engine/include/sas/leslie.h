#pragma once
// A rotary speaker (the Leslie kind): a crossover at 800 Hz splits the sound into the horn (the highs, a small speaker turning fast on a drum) and the rotor
// (the lows, a baffle with a drum that turns slower). Each one turns at its own rate, which glides to the speed that is chosen (the horn gets there in about a
// second, the heavy drum in several: the "ramp" of the real thing), and two microphones, 120 degrees apart, hear it: as the horn turns the distance to each
// microphone changes (a Doppler shift: the delay swings by about half a millisecond) and so does how much of the sound points at it (the tremolo), so the sound
// sways from one side to the other. Speed: Stop (the brake), Slow (chorale) or Fast (tremolo). Depth scales the Doppler and the tremolo of both. Balance sets horn
// against rotor. Mix is how much of the speaker is in the output. Mono in (a stereo pair is summed), stereo out. Switching it on or off fades over 20 ms.

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <vector>

namespace sas {

struct LeslieParams {
    bool on = false;
    int speed = 1;         // 0 stop, 1 slow, 2 fast
    double depth = 0.7;    // 0 .. 1
    double balance = 0.5;  // 0 .. 1: 0 all rotor, 1 all horn
    double mix = 1.0;      // 0 .. 1
};

class Leslie {
public:
    void prepare(double fs) {
        fs_ = fs;
        size_ = 1;
        while (size_ < std::size_t(fs * 0.005) + 8) size_ *= 2;
        for (auto& b : buf_) b.assign(size_, 0.0f);
        mask_ = long(size_ - 1);
        design();
        reset();
    }
    void reset() {
        for (auto& b : buf_) std::fill(b.begin(), b.end(), 0.0f);
        w_ = 0;
        for (auto& f : f_) f.z = {0.0, 0.0};
        phase_ = {0.0, 0.0};
        rate_ = {target_.speed == 1 ? 0.8 : target_.speed == 2 ? 6.7 : 0.0, target_.speed == 1 ? 0.67 : target_.speed == 2 ? 5.8 : 0.0};
        amount_ = target_.on ? 1.0 : 0.0;
    }
    void setParams(const LeslieParams& p) {
        if (!ready_) {
            target_ = p;
            reset();
            ready_ = true;
        }
        target_ = p;
    }
    bool active() const { return target_.on || amount_ > 1e-4; }

    // l holds the input (a stereo pair is summed to mono first by the rack); r is a buffer to fill.
    void process(float* l, float* r, std::size_t n) {
        if (!active()) return;
        static constexpr double kPi = 3.14159265358979323846;
        const double kAmount = 1.0 - std::exp(-1.0 / (0.02 * fs_));
        const double hornTarget = target_.speed == 1 ? 0.8 : target_.speed == 2 ? 6.7 : 0.0;
        const double drumTarget = target_.speed == 1 ? 0.67 : target_.speed == 2 ? 5.8 : 0.0;
        const double kHorn = 1.0 - std::exp(-1.0 / (0.8 * fs_)), kDrum = 1.0 - std::exp(-1.0 / (3.0 * fs_));
        const double depth = std::clamp(target_.depth, 0.0, 1.0);
        const double hornA = 0.00045 * depth * fs_, drumA = 0.0003 * depth * fs_;  // Doppler swing in samples
        const double hornAm = 0.65 * depth, drumAm = 0.4 * depth;                  // tremolo depth
        const double hornGain = 2.0 * std::clamp(target_.balance, 0.0, 1.0) / (1.0 - 0.5 * hornAm);
        const double drumGain = 2.0 * (1.0 - std::clamp(target_.balance, 0.0, 1.0)) / (1.0 - 0.5 * drumAm);
        const double mix = std::clamp(target_.mix, 0.0, 1.0);
        const double centreH = 0.0012 * fs_, centreD = 0.0012 * fs_;  // rest delay, so the swing never goes negative
        const double micOffset = kPi / 3.0;                           // the two microphones are 120 degrees apart
        for (std::size_t i = 0; i < n; ++i) {
            amount_ += ((target_.on ? 1.0 : 0.0) - amount_) * kAmount;
            rate_[0] += (hornTarget - rate_[0]) * kHorn;
            rate_[1] += (drumTarget - rate_[1]) * kDrum;
            for (int k = 0; k < 2; ++k) {
                phase_[std::size_t(k)] += 2.0 * kPi * rate_[std::size_t(k)] / fs_;
                if (phase_[std::size_t(k)] > 2.0 * kPi) phase_[std::size_t(k)] -= 2.0 * kPi;
            }
            const double x = l[i];
            // The crossover (a 4th-order Linkwitz-Riley: two Butterworth sections each way).
            const double lo = f_[1].tick(f_[0].tick(x, 0), 1), hi = f_[3].tick(f_[2].tick(x, 2), 3);
            buf_[0][std::size_t(w_ & mask_)] = float(hi);
            buf_[1][std::size_t(w_ & mask_)] = float(lo);
            const double ph = phase_[0], pd = phase_[1];
            const double hL = read(buf_[0], double(w_) - (centreH + hornA * std::sin(ph + micOffset)));
            const double hR = read(buf_[0], double(w_) - (centreH + hornA * std::sin(ph - micOffset)));
            const double dL = read(buf_[1], double(w_) - (centreD + drumA * std::sin(pd + micOffset)));
            const double dR = read(buf_[1], double(w_) - (centreD + drumA * std::sin(pd - micOffset)));
            ++w_;
            const double amHL = 1.0 - hornAm + hornAm * (0.5 + 0.5 * std::cos(ph + micOffset));
            const double amHR = 1.0 - hornAm + hornAm * (0.5 + 0.5 * std::cos(ph - micOffset));
            const double amDL = 1.0 - drumAm + drumAm * (0.5 + 0.5 * std::cos(pd + micOffset));
            const double amDR = 1.0 - drumAm + drumAm * (0.5 + 0.5 * std::cos(pd - micOffset));
            const double wl = hL * amHL * hornGain + dL * amDL * drumGain;
            const double wr = hR * amHR * hornGain + dR * amDR * drumGain;
            const double m = mix * amount_;
            l[i] = float(x + (wl - x) * m);
            if (r) r[i] = float(x + (wr - x) * m);
        }
    }

private:
    struct Biquad {
        double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
        std::array<double, 2> z{};
        double tick(double x, int) {
            const double y = b0 * x + z[0];
            z[0] = b1 * x - a1 * y + z[1];
            z[1] = b2 * x - a2 * y;
            return y;
        }
    };
    void design() {
        const double w = 2.0 * 3.14159265358979323846 * 800.0 / fs_, alpha = std::sin(w) / (2.0 * 0.70710678), c = std::cos(w), a0 = 1.0 + alpha;
        for (int k = 0; k < 4; ++k) {
            Biquad b;
            if (k < 2) {  // low-pass
                b.b0 = (1.0 - c) / 2.0 / a0;
                b.b1 = (1.0 - c) / a0;
                b.b2 = b.b0;
            } else {  // high-pass
                b.b0 = (1.0 + c) / 2.0 / a0;
                b.b1 = -(1.0 + c) / a0;
                b.b2 = b.b0;
            }
            b.a1 = -2.0 * c / a0;
            b.a2 = (1.0 - alpha) / a0;
            f_[std::size_t(k)] = b;
        }
    }
    float read(const std::vector<float>& b, double pos) const {
        const long i0 = long(std::floor(pos));
        const float f = float(pos - double(i0));
        const float x0 = b[std::size_t(i0 & mask_)], x1 = b[std::size_t((i0 + 1) & mask_)];
        return x0 + (x1 - x0) * f;
    }

    double fs_ = 48000.0, amount_ = 0.0;
    bool ready_ = false;
    std::size_t size_ = 0;
    long mask_ = 0, w_ = 0;
    std::array<std::vector<float>, 2> buf_;
    std::array<Biquad, 4> f_{};
    std::array<double, 2> phase_{}, rate_{};
    LeslieParams target_;
};

}  // namespace sas
