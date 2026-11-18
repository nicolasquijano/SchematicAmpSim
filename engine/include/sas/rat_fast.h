#pragma once
// Efficient RAT style distortion (engine/data/rat.net): the circuit solved analytically instead of by the nodal solver (see sas/sd1_fast.h for the same idea).
//   The first half is linear: the input network, the LM308 gain stage with the DISTORTION pot in its feedback and the op-amp's finite gain-bandwidth (which is
//   what limits the highs at full distortion, as in the real pedal). It is one transfer function H(s) built from the netlist's values and run as a cascade of
//   sections (sas/rational.h), so it answers like the solver's trapezoidal circuit. The op-amp's output limit is a tanh around the bias.
//   The second half is the clipper: R6 and C7 in series into two antiparallel diodes to ground, with the FILTER (the pot, R8 and C8) hanging on the same node.
//   Its unknown, the voltage on the diodes, is one Newton equation per sample, and the two capacitors are trapezoidal companions.
//   After it only filters are left: C9 / R9 (the JFET follower is ideal here), C10 and the VOLUME pot.

#include <algorithm>
#include <cmath>

#include "sas/filters.h"
#include "sas/rational.h"

namespace sas {

class RatFast {
public:
    explicit RatFast(double fs) : fs_(fs), hp9_(fs, 0.0, kR9 * kC9, kR9 * kC9) {
        have_ = false;
        setParts(100e3, 100e3, 50e3, 50e3);
    }

    // The pots as the circuit has them: DISTORTION (the feedback resistor), the FILTER pot in series with R8, and VOLUME's two sections.
    void setParts(double rDist, double rFilter, double rVolTop, double rVolBottom) {
        rDist = std::max(rDist, 50.0);
        rFilter = std::max(rFilter, 50.0);
        rVolTop = std::max(rVolTop, 50.0);
        rVolBottom = std::max(rVolBottom, 50.0);
        if (have_ && rDist == rDist_ && rFilter == rFilter_ && rVolTop == rVolTop_ && rVolBottom == rVolBottom_) return;
        have_ = true;
        rDist_ = rDist; rFilter_ = rFilter; rVolTop_ = rVolTop; rVolBottom_ = rVolBottom;

        // Input network and gain stage: H = sC1 Nz / (Din Dg).
        const Poly q1{1.0, kR4 * kC5}, q2{1.0, kR5 * kC6}, z3{1.0, rDist * kC3};
        const Poly dz = z3 * q1 * q2;
        const Poly nz = dz + Poly{0.0, rDist} * (kC5 * q2 + kC6 * q1);
        const double tauA = kA0 / (2.0 * kPi * kGbw);  // the op-amp: A0 / (1 + s tauA)
        const Poly dg = dz + (1.0 / kA0) * (Poly{1.0, tauA} * nz);
        const Poly p1{1.0, kR2 * kC2};
        const Poly din = Poly{0.0, kC1} * p1 + Poly{1.0 / kR1, kR2 * kC2 / kR1 + kC2};
        linear_.design(Poly{0.0, kC1} * nz, din * dg, fs_);

        // The clipper's load: the FILTER, as the series resistance to the capacitor C8.
        rf2_ = rFilter + kR8;
        gc8_ = 2.0 * kC8 * fs_;
        gf_ = gc8_ / (1.0 + gc8_ * rf2_);
        gc7_ = kC7 * 2.0 * fs_;

        // After the filter: C10 and the VOLUME pot (its lower part loaded by the 1M).
        const double botLoaded = rVolBottom * kRLoad / (rVolBottom + kRLoad);
        const double series = rVolTop + botLoaded;
        levelHp_ = FirstOrder(fs_, 0.0, botLoaded / series * kC10 * series, kC10 * series);
    }

    // One sample: guitar volts in, pedal output volts (AC coupled) out.
    double process(double vin) {
        const double lin = linear_.process(vin);
        const double e = kVSat * std::tanh(lin / kVSat);  // the op-amp's output limit, then R6 + its output resistance

        // Clipper: i_src = (e - vd - vc7) / (Rs + 1/gc7), the capacitor C7 as a trapezoidal companion; the filter and the diodes take the same current.
        const double rs = kR6 + kRout;
        const double h7 = 1.0 / gc7_;  // = h / C7 with h = 1 / (2 fs)
        const double hf = (gc8_ * vtPrev_ + icf_) / (1.0 + gc8_ * rf2_);
        double vd = vdPrev_;
        for (int it = 0; it < 40; ++it) {
            const double arg = std::clamp(vd / kNvt, -600.0, 600.0);
            const double ep = std::exp(arg), em = std::exp(-arg);
            const double isrc = (e - vd - vc7Prev_ - h7 * ic7Prev_) / (rs + h7);
            const double f = isrc - kIs * (ep - em) - gf_ * vd + hf - vd / kBleed;
            const double df = -1.0 / (rs + h7) - kIs * (ep + em) / kNvt - gf_ - 1.0 / kBleed;
            const double step = std::clamp(f / df, -0.2, 0.2);
            vd -= step;
            if (std::abs(step) < 1e-10) break;
        }
        const double isrc = (e - vd - vc7Prev_ - h7 * ic7Prev_) / (rs + h7);
        vc7Prev_ = vc7Prev_ + h7 * (isrc + ic7Prev_);
        ic7Prev_ = isrc;
        const double ifil = gf_ * vd - hf;  // the current into the filter
        const double vt = vd - rf2_ * ifil;
        icf_ = ifil;
        vtPrev_ = vt;
        vdPrev_ = vd;

        return levelHp_.process(hp9_.process(vt));
    }

private:
    static constexpr double kPi = 3.14159265358979323846;
    static constexpr double kC1 = 0.022e-6, kR1 = 1e6, kR2 = 1e3, kC2 = 0.001e-6;
    static constexpr double kR4 = 560.0, kC5 = 4.7e-6, kR5 = 47.0, kC6 = 2.2e-6, kC3 = 100e-12;
    static constexpr double kA0 = 3e5, kGbw = 1e6, kRout = 100.0, kVSat = 3.6;  // the LM308 model of the netlist
    static constexpr double kR6 = 1e3, kC7 = 4.7e-6, kBleed = 10e6;
    static constexpr double kR8 = 1.5e3, kC8 = 3.3e-9;
    static constexpr double kC9 = 0.022e-6, kR9 = 1e6, kC10 = 1e-6, kRLoad = 1e6;
    static constexpr double kIs = 7.5e-9, kNvt = 0.0259 * 1.9;  // the SIGNAL diode

    double fs_;
    bool have_ = false;
    double rDist_ = 0, rFilter_ = 0, rVolTop_ = 0, rVolBottom_ = 0;
    RationalFilter linear_;
    FirstOrder hp9_, levelHp_;
    double rf2_ = 0, gc8_ = 0, gf_ = 0, gc7_ = 0;
    double vdPrev_ = 0, vc7Prev_ = 0, ic7Prev_ = 0, vtPrev_ = 0, icf_ = 0;
};

}  // namespace sas
