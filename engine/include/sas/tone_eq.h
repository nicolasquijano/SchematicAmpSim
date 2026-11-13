#pragma once
// The active EQ of the MT-2 style distortion, as filters: a low shelf (LOW, about +-14 dB at 100 Hz), a peak (MIDDLE, +-15 dB, centred by MID FREQ from 200 Hz to 5 kHz) and a high shelf (HIGH, about +-14 dB at 10 kHz),
// the ranges of the Boss pedal's controls. Knobs are 0..10, 5 is flat. Coefficients follow the knobs smoothly because the caller moves them a little at a time.

#include <algorithm>
#include <cmath>

namespace sas {

class ToneEq {
public:
    void prepare(double fs) {
        fs_ = fs;
        reset();
    }
    void reset() {
        for (auto& b : s_) b = {};
    }
    // low, mid, midFreq, high on the 0..10 scale.
    void setKnobs(double low, double mid, double midFreq, double high) {
        const double db = 3.0;  // dB per knob unit: +-15 dB at the ends
        shelf(0, 250.0, (low - 5.0) * db, false);  // (the shelf reaches its full gain a little below its corner: about +-14 dB at 100 Hz)
        peak(1, 200.0 * std::pow(25.0, std::clamp(midFreq, 0.0, 10.0) / 10.0), (mid - 5.0) * db, 1.0);
        shelf(2, std::min(4000.0, 0.45 * fs_), (high - 5.0) * db, true);  // (about +-14 dB at 10 kHz)
    }
    float process(float x) {
        double y = x;
        for (auto& b : s_) {
            const double out = b.b0 * y + b.z1;
            b.z1 = b.b1 * y - b.a1 * out + b.z2;
            b.z2 = b.b2 * y - b.a2 * out;
            y = out;
        }
        return float(y);
    }

private:
    struct Biquad {
        double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
    };
    void set(int i, double b0, double b1, double b2, double a0, double a1, double a2) {
        Biquad& b = s_[i];
        b.b0 = b0 / a0;
        b.b1 = b1 / a0;
        b.b2 = b2 / a0;
        b.a1 = a1 / a0;
        b.a2 = a2 / a0;
    }
    // RBJ shelving filter with slope 1.
    void shelf(int i, double f, double gainDb, bool high) {
        const double A = std::pow(10.0, gainDb / 40.0), w = 6.283185307179586 * f / fs_, cw = std::cos(w), sw = std::sin(w);
        const double alpha = sw / 2.0 * std::sqrt(2.0), k = 2.0 * std::sqrt(A) * alpha;
        if (!high)
            set(i, A * ((A + 1) - (A - 1) * cw + k), 2 * A * ((A - 1) - (A + 1) * cw), A * ((A + 1) - (A - 1) * cw - k), (A + 1) + (A - 1) * cw + k, -2 * ((A - 1) + (A + 1) * cw), (A + 1) + (A - 1) * cw - k);
        else
            set(i, A * ((A + 1) + (A - 1) * cw + k), -2 * A * ((A - 1) + (A + 1) * cw), A * ((A + 1) + (A - 1) * cw - k), (A + 1) - (A - 1) * cw + k, 2 * ((A - 1) - (A + 1) * cw), (A + 1) - (A - 1) * cw - k);
    }
    void peak(int i, double f, double gainDb, double q) {
        const double A = std::pow(10.0, gainDb / 40.0), w = 6.283185307179586 * std::min(f, 0.45 * fs_) / fs_, alpha = std::sin(w) / (2.0 * q), cw = std::cos(w);
        set(i, 1 + alpha * A, -2 * cw, 1 - alpha * A, 1 + alpha / A, -2 * cw, 1 - alpha / A);
    }
    double fs_ = 48000.0;
    Biquad s_[3];
};

}  // namespace sas
