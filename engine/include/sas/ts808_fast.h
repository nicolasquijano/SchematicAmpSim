#pragma once
// Efficient Ibanez TS808: the circuit solved analytically instead of by the nodal solver.
// Port of jcm800/src/ts808_fast.rs. The pedal is linear except for two MA150 diodes in the
// feedback of op-amp 1A; with an ideal op-amp the clipper is one 1-D Newton equation for the
// feedback voltage, and every other stage is a first-order filter from the schematic values.

#include <algorithm>
#include <cmath>

#include "sas/filters.h"

namespace sas {

struct TsControls {
    double drive = 0.5, tone = 0.5, level = 0.5;
    bool operator==(const TsControls&) const = default;
};

class TubeScreamerFast {
public:
    explicit TubeScreamerFast(double fs)
        : fs_(fs),
          hpA_(highPass(fs, 15.6)),
          hpB_(highPass(fs, 15.9)),
          // R107 into C105 with R109 as the load: K = G1/(G1+G9), tau = C/(G1+G9).
          lowPass_(section(fs, (1.0 / kR107) / (1.0 / kR107 + 1.0 / kR109), 0.0,
                           kC105 / (1.0 / kR107 + 1.0 / kR109))),
          hpOut_(highPass(fs, 1.6)) {
        controls_ = {-1.0, -1.0, -1.0};  // force the first setControls to apply
        setControls(TsControls{});
    }

    const TsControls& controls() const { return controls_; }

    void setControls(const TsControls& ctl) {
        if (ctl == controls_) return;
        controls_ = ctl;

        // Drive: R106 plus the 500k A pot as a variable resistor.
        rF_ = kR106 + std::max(500e3 * logTaper(ctl.drive), kMinOhms);

        // Tone: pot sections toward the - input (Rb) and toward the + input (Ra).
        const double rb = std::max(20e3 * std::pow(1.0 - std::clamp(ctl.tone, 0.0, 1.0), 2.0), kMinOhms);
        const double ra = std::max(20e3 - rb, kMinOhms);
        const double g = 1.0 / ra + 1.0 / rb;
        // H = 1 + (R110/Rb) Y/(G+Y) with Y = sC106/(1+s R108 C106).
        const double tauD = kC106 * (1.0 + g * kR108) / g;
        const double tauN = kC106 * (1.0 + g * kR108 + kR110 / rb) / g;
        tone_.retune(section(fs_, 1.0, tauN, tauD));

        // Level: the wiper divider, loaded by the next stage's bias resistor, then the buffer.
        const double bot = std::max(kLevelPot * std::clamp(ctl.level, 0.0, 1.0), kMinOhms);
        const double top = std::max(kLevelPot - bot, kMinOhms);
        const double botLoaded = bot * kR112 / (bot + kR112);
        levelGain_ = kOutputLoadLoss * kBufferGain * botLoaded / (kR111 + top + botLoaded);
    }

    // One sample: guitar volts in, pedal output volts (AC coupled) out.
    double process(double vin) {
        const double x = hpB_.process(hpA_.process(vin * kBufferGain));

        // Input leg R104 + C103 to ground, driven by the op-amp's virtual short (v- = x).
        const double h = 1.0 / (2.0 * fs_);
        const double k = h / kC103;
        const double ig = (x - c3State_ - k * igPrev_) / (kR104 + k);
        c3State_ = x - kR104 * ig;

        // Feedback network: Rf || C104 || antiparallel diodes must carry that same current.
        const double gc = 2.0 * kC104 * fs_;
        const double hist = gc * vfPrev_ + ic4Prev_;  // i_C4 = gc * v_f - hist
        const double gLin = 1.0 / rF_ + gc;
        double vf = vfPrev_;
        for (int it = 0; it < 30; ++it) {
            const double arg = std::clamp(vf / kNvt, -600.0, 600.0);
            const double ep = std::exp(arg), em = std::exp(-arg);
            const double f = gLin * vf - hist + kIs * (ep - em) - ig;
            const double df = gLin + kIs * (ep + em) / kNvt;
            const double step = std::clamp(f / df, -0.25, 0.25);
            vf -= step;
            if (std::abs(step) < 1e-9) break;
        }
        ic4Prev_ = gc * vf - hist;
        vfPrev_ = vf;
        igPrev_ = ig;

        // Op-amp output (the rails cap it far beyond what the diodes allow at low drive).
        const double vOut = kVSat * std::tanh((x + vf) / kVSat);
        const double y = tone_.process(lowPass_.process(vOut));
        return hpOut_.process(y * levelGain_);
    }

private:
    static constexpr double kPi = 3.14159265358979323846;
    static constexpr double kR104 = 4.7e3, kC103 = 0.047e-6, kR106 = 51e3, kC104 = 51e-12;
    static constexpr double kR107 = 1e3, kC105 = 0.22e-6, kR109 = 10e3, kR108 = 220.0, kC106 = 0.22e-6;
    static constexpr double kR110 = 1e3, kR111 = 1e3, kR112 = 510e3, kLevelPot = 100e3;
    static constexpr double kIs = 2.5e-9, kNvt = 0.045;  // MA150 (1N914-class)
    static constexpr double kBufferGain = 0.985;         // emitter followers Q101/Q103
    static constexpr double kMinOhms = 50.0;
    // The op-amps' ~75 ohm output resistance costs a few percent the ideal equations leave out.
    static constexpr double kOutputLoadLoss = 0.93;
    static constexpr double kVSat = 3.3;  // op-amp output limit around mid-rail on 9 V

    static double logTaper(double w) {
        constexpr double k = 3.47;
        return (std::exp(k * std::clamp(w, 0.0, 1.0)) - 1.0) / (std::exp(k) - 1.0);
    }
    // k (1 + s tauN) / (1 + s tauD).
    static FirstOrder section(double fs, double k, double tauN, double tauD) { return {fs, k, k * tauN, tauD}; }
    static FirstOrder highPass(double fs, double hz) {
        const double tau = 1.0 / (2.0 * kPi * hz);
        return {fs, 0.0, tau, tau};
    }

    double fs_;
    TsControls controls_;
    FirstOrder hpA_, hpB_;
    double rF_ = kR106, c3State_ = 0, igPrev_ = 0, vfPrev_ = 0, ic4Prev_ = 0;
    FirstOrder lowPass_, tone_, hpOut_;
    double levelGain_ = 0;
};

}  // namespace sas
