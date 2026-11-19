#pragma once
// The "Studio Compressor" (model 0 of the Compressor module) as a model of how the Keeley circuit of sas/compressor.h behaves, not as the circuit: the nodal solver cost about
// 12 % of a core for it, this costs a fraction of one. It was fitted to measurements of the circuit (sine waves of many levels through it, the response to a burst, the
// frequency response, the pots) and keeps what is heard of it:
//   - the wet path is a limiter in disguise: the OTA's gain (about +2 dB with Sustain at zero, +27 dB at full) is held down by an envelope so that the output stays at about
//     -19 dBV whatever the input (a ratio of 12:1 to 25:1 above that);
//   - the envelope is a capacitor that the detector discharges at once and the supply recharges through R20 + R58 (1.6 s: the long sustain) or R20 alone with the Attack switch
//     (0.1 s). The gain is proportional to its voltage, so after a loud note the gain comes back as 1 - (1 - v0) exp(-t / tau), not in dB;
//   - the wet path is a high pass near 165 Hz, a low pass near 8 kHz and Tone's high shelf (up to +7 dB); the dry path a high pass near 68 Hz at -8.5 dB;
//   - Blend weighs the two (the network of the pot is not linear), Level is the pot's taper after them, and the output stage limits softly at 3.3 V.
// Input and output are volts, like the pedals.

#include <algorithm>
#include <array>
#include <cmath>

#include "sas/compressor.h"
#include "sas/filters.h"

namespace sas {

class KeeleyComp {
public:
    explicit KeeleyComp(double fs) : fs_(fs) {
        const double tau = 1.0 / (2.0 * kPi * 165.0);
        hpWet_ = FirstOrder(fs_, 0.0, tau, tau);
        const double tauLp = 1.0 / (2.0 * kPi * 8000.0);
        lpWet_ = FirstOrder(fs_, 1.0, 0.0, tauLp);
        const double tauD = 1.0 / (2.0 * kPi * 68.0);
        hpDry_ = FirstOrder(fs_, 0.0, tauD, tauD);
        // The wet path's gain is 1 at 400 Hz (Tone at the middle) before the OTA's gain: what the high pass and the low pass leave there, and the shelf at its middle.
        wetNorm_ = 1.0 / (magHp(400.0) * magLp(400.0));
        kRelease_ = 1.0 - std::exp(-1.0 / (0.012 * fs_));
        controls_.sustain = -1.0;
        setControls(CompControls{});
    }

    void setControls(const CompControls& c) {
        if (c == controls_) return;
        controls_ = c;
        const double s = std::clamp(c.sustain, 0.0, 1.0), t = std::clamp(c.tone, 0.0, 1.0), b = std::clamp(c.blend, 0.0, 1.0), lv = std::clamp(c.level, 0.0, 1.0);
        g0_ = dbToLin(interp(kSustainDb, s));
        // Tone: a high shelf at 0.9 kHz whose boost is 0, 1.5 and 7.8 dB at the pot's ends and middle, normalised to 400 Hz at the middle.
        const double boost = dbToLin(interp(kToneDb, t)), tauZ = 1.0 / (2.0 * kPi * 900.0);
        shelf_ = FirstOrder(fs_, 1.0, tauZ, tauZ / boost);
        shelfNorm_ = 1.0 / magShelf(400.0, dbToLin(interp(kToneDb, 0.5)), tauZ);
        shelfG_ = shelfNorm_;
        wetWeight_ = interp(kWetWeight, b);
        dryWeight_ = interp(kDryWeight, b) * dbToLin(-8.3);
        levelGain_ = dbToLin(interp(kLevelDb, lv));
        kRecharge_ = 1.0 - std::exp(-1.0 / ((c.fastRelease ? 0.1 : 1.6) * fs_));
        fastOffsetDb_ = c.fastRelease ? 2.4 : 0.0;  // the faster the recharge, the more the detector has to be driven to hold the gain down
    }
    const CompControls& controls() const { return controls_; }
    void setSourceOhms(double) {}  // the circuit's input is loaded by the guitar; this model does not look at it

    // One sample: volts in, volts out (AC coupled).
    double process(double vin) {
        const double w = lpWet_.process(hpWet_.process(vin)) * wetNorm_;
        // The detector: the peak of the wet signal; the level it would come out at with the OTA's gain alone (L) is held down by a ceiling that rises a little with the input
        // (a soft minimum of the two, p = 5), and the gain that leaves is the most the envelope can have.
        env_ = std::max(std::fabs(w), env_ - env_ * kRelease_);
        double limit = 1.0;
        const double l = g0_ * env_;
        if (l > 1e-6) {
            const double inDb = 20.0 * std::log10(std::max(env_, 1e-9));
            const double ceiling = dbToLin(ceilingDb(inDb) + fastOffsetDb_);
            const double r = l / ceiling, r2 = r * r;
            limit = std::min(1.0, std::pow(1.0 + r2 * r2 * r, -0.2));
        }
        v_ += (1.0 - v_) * kRecharge_;
        v_ = std::min(v_, limit);
        const double wet = shelf_.process(w) * shelfG_ * g0_ * v_;
        const double dry = hpDry_.process(vin);
        const double mix = (wetWeight_ * wet + dryWeight_ * dry) * levelGain_;
        return kLimit * std::tanh(mix / kLimit);
    }

    // The gain left by the envelope (1 = none), for tests and meters.
    double gain() const { return v_; }

private:
    static constexpr double kPi = 3.14159265358979323846;
    static constexpr double kLimit = 3.3;
    static double dbToLin(double db) { return std::pow(10.0, db / 20.0); }
    // Piecewise linear through points spread evenly over 0..1.
    template <std::size_t N>
    static double interp(const std::array<double, N>& t, double x) {
        const double p = x * double(N - 1);
        const std::size_t i = std::min<std::size_t>(std::size_t(p), N - 2);
        return t[i] + (t[i + 1] - t[i]) * (p - double(i));
    }
    static double magHp(double f) { return f / std::sqrt(f * f + 165.0 * 165.0); }
    static double magLp(double f) { return 1.0 / std::sqrt(1.0 + (f / 8000.0) * (f / 8000.0)); }
    static double magShelf(double f, double boost, double tauZ) {
        const double w = 2.0 * kPi * f;
        return std::sqrt((1.0 + w * tauZ * w * tauZ) / (1.0 + (w * tauZ / boost) * (w * tauZ / boost)));
    }
    // The ceiling of the wet path's output (dBV) for an input of `inDb` (the wet signal's peak before the OTA's gain), measured on the circuit: about -19 dBV, creeping up to -18 dBV
    // for the loudest notes.
    static double ceilingDb(double inDb) {
        for (std::size_t i = 1; i < kCeiling.size(); ++i)
            if (inDb <= kCeiling[i][0]) return kCeiling[i - 1][1] + (kCeiling[i][1] - kCeiling[i - 1][1]) * (inDb - kCeiling[i - 1][0]) / (kCeiling[i][0] - kCeiling[i - 1][0]);
        const auto& a = kCeiling[kCeiling.size() - 2];
        const auto& b = kCeiling.back();
        return b[1] + (b[1] - a[1]) / (b[0] - a[0]) * (inDb - b[0]);
    }

    static constexpr std::array<double, 5> kSustainDb{2.07, 9.28, 16.1, 22.4, 27.5};      // the OTA's gain at 400 Hz with Sustain at 0, 1/4, 1/2, 3/4, 1
    static constexpr std::array<double, 3> kToneDb{0.0, 1.5, 7.8};                         // the shelf's boost at Tone 0, 1/2, 1
    static constexpr std::array<double, 5> kWetWeight{0.0, 0.44, 0.70, 0.878, 1.0};        // Blend 0, 1/4, 1/2, 3/4, 1
    static constexpr std::array<double, 5> kDryWeight{1.0, 0.833, 0.625, 0.357, 0.0};
    static constexpr std::array<double, 5> kLevelDb{-49.4, -10.5, 0.0, 8.45, 16.6};        // Level 0..1 relative to the middle (the pot's audio taper)
    static constexpr std::array<std::array<double, 2>, 5> kCeiling{{{-90.0, -19.0}, {-30.0, -19.0}, {-20.0, -18.86}, {-10.45, -18.68}, {-1.94, -18.0}}};

    double fs_;
    CompControls controls_;
    FirstOrder hpWet_, lpWet_, shelf_, hpDry_;
    double g0_ = 1.0, wetNorm_ = 1.0, shelfNorm_ = 1.0, shelfG_ = 1.0, wetWeight_ = 0.0, dryWeight_ = 1.0, levelGain_ = 1.0;
    double kRelease_ = 0.0, kRecharge_ = 0.0, fastOffsetDb_ = 0.0;
    double env_ = 0.0, v_ = 1.0;
};

}  // namespace sas
