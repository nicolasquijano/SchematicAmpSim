#pragma once
// A brick-wall limiter: nothing that comes out is louder than the Ceiling. It looks 1 ms ahead, so it turns the gain down smoothly before a peak
// arrives (no clipping, no click) and lets it come back over the Release time. Gain pushes the signal into it first (0 to 24 dB), which makes
// things louder and denser without ever going over. On a stereo pair both channels get the same gain (the loudest one decides), so the image does not
// move. It delays the signal by the lookahead (reported to the host as latency). Switching it on or off fades over 20 ms.

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <vector>

namespace sas {

struct LimiterParams {
    bool on = false;
    double gainDb = 0.0;     // 0 .. 24
    double ceilingDb = -1.0; // -12 .. 0
    double releaseMs = 120.0;// 10 .. 1000
};

class Limiter {
public:
    void prepare(double fs) {
        fs_ = fs;
        look_ = std::max(1, int(std::lround(0.001 * fs)));
        ringSize_ = 1;
        while (ringSize_ < std::size_t(2 * look_ + 8)) ringSize_ *= 2;
        mask_ = long(ringSize_ - 1);
        reset();
    }
    void reset() {
        for (auto& b : dly_) b.assign(ringSize_, 0.0f);
        need_.assign(ringSize_, 1.0f);
        min_.assign(ringSize_, 1.0f);
        w_ = 0;
        sum_ = double(look_ + 1);
        gs_ = 1.0;
        amount_ = target_.on ? 1.0 : 0.0;
        gainCur_ = target_.gainDb;
    }
    void setParams(const LimiterParams& p) {
        if (!ready_) {
            amount_ = p.on ? 1.0 : 0.0;
            gainCur_ = p.gainDb;
            ready_ = true;
        }
        target_ = p;
    }
    bool active() const { return target_.on || amount_ > 1e-4; }
    double latencySamples() const { return double(look_); }
    // How far the gain is down right now, in dB (0 = not limiting); for a meter.
    double reductionDb() const { return -20.0 * std::log10(std::max(gs_, 1e-6)); }

    void process(float* l, float* r, std::size_t n) {
        if (!active()) return;
        const double kAmount = 1.0 - std::exp(-1.0 / (0.02 * fs_)), kParam = 1.0 - std::exp(-1.0 / (0.02 * fs_));
        const double kRel = 1.0 - std::exp(-1.0 / (std::max(5.0, target_.releaseMs) * 0.001 * fs_));
        const float ceiling = float(std::pow(10.0, std::clamp(target_.ceilingDb, -24.0, 0.0) / 20.0));
        const int win = look_ + 1;
        for (std::size_t i = 0; i < n; ++i) {
            amount_ += ((target_.on ? 1.0 : 0.0) - amount_) * kAmount;
            gainCur_ += (target_.gainDb - gainCur_) * kParam;
            const float g = float(std::pow(10.0, gainCur_ / 20.0));
            const float sl = l[i] * g, sr = r ? r[i] * g : 0.0f;
            const float peak = std::max(std::fabs(sl), std::fabs(sr));
            dly_[0][std::size_t(w_ & mask_)] = sl;
            dly_[1][std::size_t(w_ & mask_)] = sr;
            need_[std::size_t(w_ & mask_)] = peak > ceiling ? ceiling / peak : 1.0f;
            // The lowest gain asked for over the last `win` samples, averaged over `win` of those minima: a ramp that is down in time for every peak.
            float m = 1.0f;
            for (int k = 0; k < win; ++k) m = std::min(m, need_[std::size_t((w_ - k) & mask_)]);
            sum_ += double(m) - double(min_[std::size_t((w_ - win) & mask_)]);
            min_[std::size_t(w_ & mask_)] = m;
            const double target = std::min(1.0, sum_ / double(win));
            gs_ = target < gs_ ? target : gs_ + (target - gs_) * kRel;
            const std::size_t at = std::size_t((w_ - look_) & mask_);
            const float wet = float(amount_);
            // Fully on: the limited signal; fading in or out: it moves to the raw signal, delayed the same.
            const float factor = (1.0f - wet) / g + wet * float(gs_);
            l[i] = dly_[0][at] * factor;
            if (r) r[i] = dly_[1][at] * factor;
            ++w_;
        }
    }

private:
    double fs_ = 48000.0;
    int look_ = 48;
    std::size_t ringSize_ = 256;
    long mask_ = 255, w_ = 0;
    std::array<std::vector<float>, 2> dly_;
    std::vector<float> need_, min_;
    double sum_ = 0.0, gs_ = 1.0, amount_ = 0.0, gainCur_ = 0.0;
    LimiterParams target_;
    bool ready_ = false;
};

}  // namespace sas
