#pragma once
// Harmonic exciter for the bass and the treble, in the spirit of a "sonic maximizer": it adds harmonics of the lowest band (so the bass
// is heard on small speakers and keeps its definition) and of the highest band (air and presence without a harsh EQ boost), and
// tilts both ends up a little with a shelf. At 0 it is bit-exact bypass.
//
// The harmonics are made from a band of the signal run through a soft clipper whose drive follows the band's own level (so the
// amount of harmonic is the same at any playing level, as a constant percentage), with the original band taken out again by a
// steep high-pass, so only the harmonics are added back. Stereo pair, no latency.

#include <algorithm>
#include <cmath>
#include <cstddef>

#include "sas/eq.h"

namespace sas {

class HarmonicExciter {
public:
    void prepare(double fs) {
        fs_ = fs;
        const double kPi = 3.14159265358979323846;
        (void)kPi;
        for (auto& c : ch_) c = {};
        envA_ = 1.0 - std::exp(-1.0 / (0.004 * fs));
        envR_ = 1.0 - std::exp(-1.0 / (0.120 * fs));
        // Low band: the part under 140 Hz makes the harmonics; the part above 170 Hz of what comes out of the clipper is kept.
        lowLp_ = lowpass(fs, 140.0);
        lowHp_ = highpass(fs, 170.0);
        // High band: the part above 3.5 kHz makes the harmonics; only what is above 6 kHz is kept.
        highHp_ = highpass(fs, 3500.0);
        highKeep_ = highpass(fs, 6000.0);
        shelves_.prepare(fs);
        reset();
    }

    void reset() {
        for (auto& c : ch_) c = {};
        shelves_.reset();
    }

    // Both amounts are 0..1; 0 on both bypasses the stage.
    void setAmounts(double low, double high) {
        low_ = std::clamp(low, 0.0, 1.0);
        high_ = std::clamp(high, 0.0, 1.0);
        EqParams p;
        p.on = true;
        p.band[0] = {100.0, 5.0 * low_, 0.7};   // the bass end tilts up to +5 dB
        p.band[4] = {6500.0, 4.0 * high_, 0.7}; // and the top to +4 dB
        shelves_.setParams(p);
    }
    bool active() const { return low_ > 1e-4 || high_ > 1e-4; }

    void process(float* l, float* r, std::size_t n) {
        if (!active()) {
            // Keep the shelves' glide state moving toward flat, without touching the audio.
            return;
        }
        run(ch_[0], l, n);
        run(ch_[1], r, n);
        shelves_.process(l, r, n);
    }

private:
    struct Coef { double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0; };
    struct St { double z1 = 0, z2 = 0; };
    struct Biq {
        Coef c;
        St s;
        double tick(double x) {
            const double y = c.b0 * x + s.z1;
            s.z1 = c.b1 * x - c.a1 * y + s.z2;
            s.z2 = c.b2 * x - c.a2 * y;
            return y;
        }
    };
    struct Channel {
        Biq lowLp, lowHp1, lowHp2, highHp, highKeep1, highKeep2;
        double envLow = 1e-4, envHigh = 1e-4;
    };

    static Coef lowpass(double fs, double f) {
        const double w = 2.0 * 3.14159265358979323846 * f / fs, alpha = std::sin(w) / (2.0 * 0.70710678), cw = std::cos(w), a0 = 1.0 + alpha;
        return {(1 - cw) / 2 / a0, (1 - cw) / a0, (1 - cw) / 2 / a0, -2 * cw / a0, (1 - alpha) / a0};
    }
    static Coef highpass(double fs, double f) {
        const double w = 2.0 * 3.14159265358979323846 * f / fs, alpha = std::sin(w) / (2.0 * 0.70710678), cw = std::cos(w), a0 = 1.0 + alpha;
        return {(1 + cw) / 2 / a0, -(1 + cw) / a0, (1 + cw) / 2 / a0, -2 * cw / a0, (1 - alpha) / a0};
    }

    // A soft clipper with an offset (so it makes even harmonics too) whose drive follows the level of the band it is given.
    static double excite(double band, double& env, double envA, double envR) {
        const double a = std::fabs(band);
        env += (a > env ? envA : envR) * (a - env);
        const double e = std::max(env, 1e-5);
        const double u = band / e;
        return e * (std::tanh(2.2 * u + 0.35) - std::tanh(0.35));
    }

    void run(Channel& c, float* x, std::size_t n) {
        c.lowLp.c = lowLp_;
        c.lowHp1.c = c.lowHp2.c = lowHp_;
        c.highHp.c = highHp_;
        c.highKeep1.c = c.highKeep2.c = highKeep_;
        for (std::size_t i = 0; i < n; ++i) {
            const double in = double(x[i]);
            double add = 0.0;
            if (low_ > 1e-4) {
                const double band = c.lowLp.tick(in);
                const double h = excite(band, c.envLow, envA_, envR_);
                add += 1.6 * low_ * c.lowHp2.tick(c.lowHp1.tick(h));  // the harmonics only: 4th-order high-pass behind the clipper
            }
            if (high_ > 1e-4) {
                const double band = c.highHp.tick(in);
                const double h = excite(band, c.envHigh, envA_, envR_);
                add += 1.4 * high_ * c.highKeep2.tick(c.highKeep1.tick(h));
            }
            x[i] = float(in + add);
        }
    }

    double fs_ = 48000.0, low_ = 0.0, high_ = 0.0, envA_ = 0.01, envR_ = 0.001;
    Coef lowLp_, lowHp_, highHp_, highKeep_;
    Channel ch_[2];
    ParametricEq shelves_;
};

}  // namespace sas
