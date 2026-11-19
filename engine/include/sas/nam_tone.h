#pragma once
// The tone controls of the NAM loader amp, after the model or in front of it (the PRE / POST switch): a low shelf, a bell and a high shelf, each with its own
// frequency (and the bell's Q), then two fixed finishing bands, a PRESENCE high shelf and a DEPTH bell in the lows (RBJ biquads, mono). The model is a capture of a
// whole amp, so its own tone stack cannot be moved; this one shapes the result. Flat bands cost nothing.
//
// Two instances are used: one for the three tone bands (run before or after the model) and one for presence and depth (always after).

#include <algorithm>
#include <cmath>

namespace sas {

class NamTone {
public:
    static constexpr double kBassHz = 110.0, kMidHz = 750.0, kMidQ = 0.8, kTrebleHz = 3200.0;
    static constexpr double kPresenceHz = 4500.0, kDepthHz = 80.0, kDepthQ = 1.1;

    void prepare(double fs) {
        fs_ = fs;
        for (auto& b : band_) b.z1 = b.z2 = 0.0;
        setTone(0.0, 0.0, 0.0, kBassHz, kMidHz, kMidQ, kTrebleHz);
    }
    // The three tone bands: gains in dB, frequencies in Hz and the bell's Q; a band under 0.05 dB is skipped.
    void setTone(double bassDb, double midDb, double trebleDb, double bassHz, double midHz, double midQ, double trebleHz) {
        design(band_[0], Kind::LowShelf, bassHz, 0.0, bassDb);
        design(band_[1], Kind::Bell, midHz, midQ, midDb);
        design(band_[2], Kind::HighShelf, trebleHz, 0.0, trebleDb);
    }
    void setGains(double bassDb, double midDb, double trebleDb) { setTone(bassDb, midDb, trebleDb, kBassHz, kMidHz, kMidQ, kTrebleHz); }
    // The two finishing bands.
    void setFinish(double presenceDb, double depthDb) {
        design(band_[0], Kind::HighShelf, kPresenceHz, 0.0, presenceDb);
        design(band_[1], Kind::Bell, kDepthHz, kDepthQ, depthDb);
        band_[2].used = false;
    }
    bool flat() const { return !band_[0].used && !band_[1].used && !band_[2].used; }

    float process(float x) {
        double v = x;
        for (auto& b : band_) {
            if (!b.used) continue;
            const double y = b.b0 * v + b.z1;
            b.z1 = b.b1 * v - b.a1 * y + b.z2;
            b.z2 = b.b2 * v - b.a2 * y;
            v = y;
        }
        return float(v);
    }

private:
    enum class Kind { LowShelf, Bell, HighShelf };
    struct Band {
        double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
        bool used = false;
    };

    void design(Band& b, Kind kind, double f, double q, double db) {
        b.used = std::fabs(db) > 0.05;
        if (!b.used) return;
        const double pi = 3.14159265358979323846;
        f = std::clamp(f, 20.0, 0.45 * fs_);
        const double w0 = 2.0 * pi * f / fs_, cw = std::cos(w0), sw = std::sin(w0);
        const double A = std::pow(10.0, db / 40.0);
        double b0, b1, b2, a0, a1, a2;
        if (kind == Kind::Bell) {
            const double alpha = sw / (2.0 * std::max(q, 0.1));
            b0 = 1 + alpha * A;  b1 = -2 * cw;  b2 = 1 - alpha * A;
            a0 = 1 + alpha / A;  a1 = -2 * cw;  a2 = 1 - alpha / A;
        } else {
            const double alpha = sw / 2.0 * std::sqrt(2.0);  // shelf slope 1
            const double tsa = 2.0 * std::sqrt(A) * alpha;
            if (kind == Kind::LowShelf) {
                b0 = A * ((A + 1) - (A - 1) * cw + tsa);  b1 = 2 * A * ((A - 1) - (A + 1) * cw);  b2 = A * ((A + 1) - (A - 1) * cw - tsa);
                a0 = (A + 1) + (A - 1) * cw + tsa;        a1 = -2 * ((A - 1) + (A + 1) * cw);     a2 = (A + 1) + (A - 1) * cw - tsa;
            } else {
                b0 = A * ((A + 1) + (A - 1) * cw + tsa);  b1 = -2 * A * ((A - 1) + (A + 1) * cw); b2 = A * ((A + 1) + (A - 1) * cw - tsa);
                a0 = (A + 1) - (A - 1) * cw + tsa;        a1 = 2 * ((A - 1) - (A + 1) * cw);      a2 = (A + 1) - (A - 1) * cw - tsa;
            }
        }
        b.b0 = b0 / a0;  b.b1 = b1 / a0;  b.b2 = b2 / a0;  b.a1 = a1 / a0;  b.a2 = a2 / a0;
    }

    double fs_ = 48000.0;
    Band band_[3];
};

}  // namespace sas
