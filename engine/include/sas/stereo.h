#pragma once
// Fake stereo for a mono amp: automatic double tracking.
//
// Each channel mixes the signal with a copy of itself delayed by a different, slowly modulated time (left about 0.45 T, right T), as a
// guitarist who plays a part twice never lands on exactly the same instant. The two channels become decorrelated, so the sound widens,
// while each channel keeps the level and (with the copy's treble rolled off a little, like a second take) the tone of the original.
// `amount` crossfades from the dry signal (0, bit-exact) to the full double (1).
//
// Mono compatibility: summing both channels adds two decorrelated delays, a mild comb, not the deep notches of a single short delay.

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <vector>

namespace sas {

class StereoDouble {
public:
    void prepare(double fs) {
        fs_ = fs;
        const std::size_t size = std::size_t(fs * 0.06) + 8;  // 60 ms of delay line
        for (auto& line : line_) line.assign(size, 0.0f);
        write_ = 0;
        phase_[0] = 0.0;
        phase_[1] = 1.9;
        for (auto& v : lp_) v = 0.0;
        lpA_ = 1.0 - std::exp(-2.0 * kPi * 7000.0 / fs);
        amountA_ = 1.0 - std::exp(-1.0 / (0.03 * fs));
        amountNow_ = amount_;
    }
    // amount 0..1; timeMs: the longer delay, 8..30 ms.
    void setAmount(double amount, double timeMs) {
        amount_ = std::clamp(amount, 0.0, 1.0);
        timeMs_ = std::clamp(timeMs, 8.0, 30.0);
    }

    // In place on a stereo pair (for a mono signal pass the same values in both).
    void process(float* l, float* r, std::size_t n) {
        const std::size_t size = line_[0].size();
        if (size == 0) return;
        for (std::size_t i = 0; i < n; ++i) {
            line_[0][write_] = l[i];
            line_[1][write_] = r[i];
            amountNow_ += amountA_ * (amount_ - amountNow_);
            if (amount_ == 0.0 && amountNow_ < 1e-5) {
                amountNow_ = 0.0;  // exact bypass
            } else {
                float* io[2] = {l + i, r + i};
                for (int c = 0; c < 2; ++c) {
                    // Delay: left 0.45 T, right T, each moved by about +-0.25 ms at 0.31 / 0.43 Hz (a fraction of a percent of pitch).
                    phase_[c] += 2.0 * kPi * (c == 0 ? 0.31 : 0.43) / fs_;
                    if (phase_[c] > 2.0 * kPi) phase_[c] -= 2.0 * kPi;
                    const double delaySamples = (timeMs_ * (c == 0 ? 0.45 : 1.0) + 0.25 * std::sin(phase_[c])) * 0.001 * fs_;
                    double pos = double(write_) - delaySamples;
                    if (pos < 0.0) pos += double(size);
                    const std::size_t i0 = std::size_t(pos) % size, i1 = (i0 + 1) % size;
                    const double frac = pos - std::floor(pos);
                    const double d = double(line_[std::size_t(c)][i0]) * (1.0 - frac) + double(line_[std::size_t(c)][i1]) * frac;
                    lp_[c] += lpA_ * (d - lp_[c]);  // the second take is a little duller
                    *io[c] = float((1.0 - amountNow_) * double(*io[c]) + amountNow_ * lp_[c]);
                }
            }
            if (++write_ >= size) write_ = 0;
        }
    }

private:
    static constexpr double kPi = 3.14159265358979323846;
    double fs_ = 48000.0, amount_ = 0.0, amountNow_ = 0.0, amountA_ = 0.0, timeMs_ = 20.0, lpA_ = 0.0;
    double phase_[2] = {0.0, 1.9}, lp_[2] = {0.0, 0.0};
    std::vector<float> line_[2];
    std::size_t write_ = 0;
};

}  // namespace sas
