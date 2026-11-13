#pragma once
// A ten-band graphic equaliser pedal, in the manner of the MXR ten-band EQ: ten sliders an octave apart (31.25 Hz to 16 kHz), each a boost or cut of up to
// +-12 dB, a GAIN that pushes the signal into the filters and a VOLUME at the output (both +-12 dB). Each band is a peaking filter about an octave wide
// (Q 1.41), the way the pedal's ten bandpass sections are, so neighbouring sliders overlap. With every slider flat it costs nothing but the two gains. Works
// on one channel or on a stereo pair (the same on both). Gains and sliders glide over 20 ms; switching it on or off fades over 20 ms.

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>

namespace sas {

struct TenBandEqParams {
    static constexpr int kBands = 10;
    bool on = false;
    std::array<double, kBands> bandsDb{};  // -12 .. +12 for each band
    double gainDb = 0.0;                   // -12 .. +12, in front of the filters
    double volumeDb = 0.0;                 // -12 .. +12, at the output
};

class TenBandEq {
public:
    static constexpr int kBands = TenBandEqParams::kBands;
    static constexpr double kHz[kBands] = {31.25, 62.5, 125.0, 250.0, 500.0, 1000.0, 2000.0, 4000.0, 8000.0, 16000.0};
    static constexpr double kQ = 1.41;

    void prepare(double fs) {
        fs_ = fs;
        ready_ = false;
        reset();
    }
    void reset() {
        for (auto& ch : z_)
            for (auto& s : ch) s = {0.0, 0.0};
        amount_ = target_.on ? 1.0 : 0.0;
        cur_ = target_.bandsDb;
        gainCur_ = target_.gainDb;
        volCur_ = target_.volumeDb;
        redesign();
    }
    void setParams(const TenBandEqParams& p) {
        if (!ready_) {
            target_ = p;
            ready_ = true;
            reset();
            return;
        }
        target_ = p;
    }
    bool active() const { return target_.on || amount_ > 1e-4; }

    void process(float* l, float* r, std::size_t n) {
        if (!active()) return;
        const double kAmount = 1.0 - std::exp(-1.0 / (0.02 * fs_)), kParam = 1.0 - std::exp(-1.0 / (0.02 * fs_));
        for (std::size_t i = 0; i < n; ++i) {
            amount_ += ((target_.on ? 1.0 : 0.0) - amount_) * kAmount;
            gainCur_ += (target_.gainDb - gainCur_) * kParam;
            volCur_ += (target_.volumeDb - volCur_) * kParam;
            if (++count_ >= 32) {
                count_ = 0;
                bool moved = false;
                for (int b = 0; b < kBands; ++b) {
                    const double d = target_.bandsDb[std::size_t(b)] - cur_[std::size_t(b)];
                    if (std::abs(d) > 1e-4) {
                        cur_[std::size_t(b)] += d * (1.0 - std::exp(-32.0 / (0.02 * fs_)));
                        moved = true;
                    }
                }
                if (moved) redesign();
            }
            const double gIn = std::pow(10.0, gainCur_ / 20.0), gOut = std::pow(10.0, volCur_ / 20.0);
            for (int c = 0; c < (r ? 2 : 1); ++c) {
                float* x = c == 0 ? l : r;
                const double dry = x[i];
                double y = dry * gIn;
                if (!flat_) y = filter(c, y);
                y *= gOut;
                x[i] = float(dry + (y - dry) * amount_);
            }
        }
    }

    // The gain in dB of the filters alone at a frequency, for the editor's curve and the tests.
    double responseDb(double hz) const {
        const double w = 2.0 * 3.14159265358979323846 * hz / fs_;
        double db = 0.0;
        for (int b = 0; b < kBands; ++b) {
            const Coef& k = coef_[std::size_t(b)];
            const double c1 = std::cos(w), s1 = std::sin(w), c2 = std::cos(2 * w), s2 = std::sin(2 * w);
            const double nr = k.b0 + k.b1 * c1 + k.b2 * c2, ni = -(k.b1 * s1 + k.b2 * s2);
            const double dr = 1.0 + k.a1 * c1 + k.a2 * c2, di = -(k.a1 * s1 + k.a2 * s2);
            db += 10.0 * std::log10((nr * nr + ni * ni) / (dr * dr + di * di));
        }
        return db;
    }

private:
    struct Coef {
        double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
    };

    void redesign() {
        flat_ = true;
        for (int b = 0; b < kBands; ++b) {
            const double g = cur_[std::size_t(b)];
            if (std::abs(g) < 0.01) {
                coef_[std::size_t(b)] = {};
                continue;
            }
            flat_ = false;
            const double A = std::pow(10.0, g / 40.0), w = 2.0 * 3.14159265358979323846 * std::min(kHz[b], 0.45 * fs_) / fs_;
            const double alpha = std::sin(w) / (2.0 * kQ), c = std::cos(w), a0 = 1.0 + alpha / A;
            coef_[std::size_t(b)] = {(1.0 + alpha * A) / a0, -2.0 * c / a0, (1.0 - alpha * A) / a0, -2.0 * c / a0, (1.0 - alpha / A) / a0};
        }
    }

    double filter(int ch, double x) {
        for (int b = 0; b < kBands; ++b) {
            const Coef& k = coef_[std::size_t(b)];
            auto& z = z_[std::size_t(ch)][std::size_t(b)];
            const double y = k.b0 * x + z[0];
            z[0] = k.b1 * x - k.a1 * y + z[1];
            z[1] = k.b2 * x - k.a2 * y;
            x = y;
        }
        return x;
    }

    double fs_ = 48000.0, amount_ = 0.0, gainCur_ = 0.0, volCur_ = 0.0;
    bool ready_ = false, flat_ = true;
    int count_ = 0;
    TenBandEqParams target_;
    std::array<double, kBands> cur_{};
    std::array<Coef, kBands> coef_{};
    std::array<std::array<std::array<double, 2>, kBands>, 2> z_{};
};

}  // namespace sas
