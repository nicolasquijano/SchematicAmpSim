#pragma once
// A three-band tone control for the NAM loader amp, after the model: a low shelf, a bell and a high shelf (RBJ biquads, mono). The model
// is a capture of a whole amp, so its own tone stack cannot be moved; this one shapes the result. Flat bands cost nothing.

#include <cmath>

namespace sas {

class NamTone {
public:
    static constexpr double kBassHz = 110.0, kMidHz = 750.0, kMidQ = 0.8, kTrebleHz = 3200.0;

    void prepare(double fs) {
        fs_ = fs;
        for (auto& b : band_) b.z1 = b.z2 = 0.0;
        setGains(0.0, 0.0, 0.0);
    }
    // Gains in dB; a band under 0.05 dB is skipped.
    void setGains(double bassDb, double midDb, double trebleDb) {
        design(band_[0], 0, bassDb);
        design(band_[1], 1, midDb);
        design(band_[2], 2, trebleDb);
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
    struct Band {
        double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
        bool used = false;
    };

    void design(Band& b, int kind, double db) {
        b.used = std::fabs(db) > 0.05;
        if (!b.used) return;
        const double pi = 3.14159265358979323846;
        const double f = kind == 0 ? kBassHz : kind == 1 ? kMidHz : kTrebleHz;
        const double w0 = 2.0 * pi * f / fs_, cw = std::cos(w0), sw = std::sin(w0);
        const double A = std::pow(10.0, db / 40.0);
        double b0, b1, b2, a0, a1, a2;
        if (kind == 1) {
            const double alpha = sw / (2.0 * kMidQ);
            b0 = 1 + alpha * A;  b1 = -2 * cw;  b2 = 1 - alpha * A;
            a0 = 1 + alpha / A;  a1 = -2 * cw;  a2 = 1 - alpha / A;
        } else {
            const double alpha = sw / 2.0 * std::sqrt(2.0);  // shelf slope 1
            const double tsa = 2.0 * std::sqrt(A) * alpha;
            if (kind == 0) {
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
