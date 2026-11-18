#pragma once
// Efficient Super OverDrive style pedal (engine/data/sd1.net): the circuit solved analytically instead of by the nodal solver, the way the Tube Screamer is
// (sas/ts808_fast.h). The pedal is linear except for the three diodes in the feedback of its first op-amp (one against two in series), so with an ideal
// op-amp the clipper is one 1-D Newton equation for the feedback voltage; every other stage is a filter from the schematic's values:
//   input network (R1, C1 and the base bias) -> C2 / R4 high pass -> gain stage (the leg R6 + C3, the feedback R5 + DRIVE with the diodes across it, the
//   op-amp's finite bandwidth as one pole) -> the R7 / C4 low pass and the tone stage (a second order section and a pole, from R7, C4, R8, C5, R9, C6 and the TONE pot) ->
//   LEVEL (C7, R10 and the pot with the next stage's bias resistor).
// The signals are around the 4.5 V bias, so everything here is AC; the op-amps' soft limit (3.3 V around it) is a tanh.
// The circuit version stays in DriveCircuit (DriveSolve::Circuit) as the reference the tests compare it with.

#include <algorithm>
#include <cmath>

#include "sas/filters.h"
#include "sas/ts808_fast.h"

namespace sas {

class SuperOverdriveFast {
public:
    explicit SuperOverdriveFast(double fs) : fs_(fs), hpIn_(fs, 0.0, kInGain * kInTau, kInTau), hpC2_(fs, 0.0, kR4 * kC2, kR4 * kC2) {
        controls_ = {-1.0, -1.0, -1.0};  // force the first setControls to apply
        setControls(TsControls{});
    }

    const TsControls& controls() const { return controls_; }

    // drive, tone and level as the circuit's knobs: DRIVE 1M linear (in series with R5), TONE 20k (the wiper toward the - input as it goes clockwise), LEVEL 10k linear.
    void setControls(const TsControls& c) {
        if (c == controls_) return;
        controls_ = c;
        auto clamp01 = [](double v) { return std::clamp(v, 0.0, 1.0); };
        const double drive = clamp01(c.drive), tone = clamp01(c.tone), level = clamp01(c.level);

        rF_ = kR5 + std::max(1e6 * drive, kMinOhms);
        // The op-amp's gain-bandwidth product closes the loop at the high-frequency gain 1 + Rf / R6.
        const double closedLoopHz = kGbw / (1.0 + rF_ / kR6);
        loop_.retune(FirstOrder(fs_, 1.0, 0.0, 1.0 / (2.0 * kPi * closedLoopHz)));

        // The low pass R7 / C4 and the tone stage, together: the pot section Ra loads the low pass's node (the + input is ideal, but the pot hangs from it), so
        // they are one transfer function. With G = 1 / Ra + 1 / Rb, Yc = C5 in series with R8, Zf = R9 || C6:
        //   tone stage     1 + Zf / Rb * Yc / (G + Yc)  =  (D + A s) / D,   D = (1 + s t1) (1 + s t2),  t1 = R9 C6,  t2 = C5 (1 + G R8) / G,  A = R9 C5 / (Rb G)
        //   low pass       1 / (1 + s R7 C4 + R7 Ra^-1 Yc / (G + Yc)) = (1 + s t2) / P,   P = (1 + s t4) (1 + s t2) + s B,  t4 = R7 C4,  B = R7 C5 / (G Ra)
        // and (1 + s t2) cancels: (D + A s) / ((1 + s t1) P), one second order section and one pole.
        const double ra = std::max(20e3 * tone, kMinOhms), rb = std::max(20e3 * (1.0 - tone), kMinOhms);
        const double g = 1.0 / ra + 1.0 / rb;
        const double tau1 = kR9 * kC6, tau2 = kC5 * (1.0 + g * kR8) / g, tau4 = kR7 * kC4;
        const double a = (kR9 / rb) * (kC5 / g), b = kR7 * kC5 / (g * ra);
        tone_.retune(Second(fs_, 1.0, tau1 + tau2 + a, tau1 * tau2, tau4 + tau2 + b, tau4 * tau2));
        tonePole_.retune(FirstOrder(fs_, 1.0, 0.0, tau1));

        // Level: C7 and R10 into the pot (its lower part loaded by the next stage's 1M).
        const double bot = std::max(10e3 * level, kMinOhms), top = std::max(10e3 * (1.0 - level), kMinOhms);
        const double botLoaded = bot * kR17 / (bot + kR17);
        const double series = kR10 + top + botLoaded;
        levelHp_.retune(FirstOrder(fs_, 0.0, botLoaded / series * kC7 * series, kC7 * series));
    }

    // One sample: guitar volts in, pedal output volts (AC coupled) out.
    double process(double vin) {
        const double x = hpC2_.process(hpIn_.process(vin));  // the gain stage's + input (and, with an ideal op-amp, its - input)

        // The leg R6 + C3 to the bias, driven by the op-amp's virtual short (v- = x).
        const double h = 1.0 / (2.0 * fs_);
        const double k = h / kC3;
        const double ig = (x - c3State_ - k * igPrev_) / (kR6 + k);
        c3State_ = x - kR6 * ig;

        // Feedback: Rf and the diodes across it carry that same current. Positive vf = the output above the - input: two diodes in series (D5, D6);
        // negative: one (D4), so the single diode clips first.
        const double gLin = 1.0 / rF_;
        double vf = vfPrev_;
        for (int it = 0; it < 30; ++it) {
            const double ap = std::clamp(vf / (2.0 * kNvt), -600.0, 600.0), an = std::clamp(-vf / kNvt, -600.0, 600.0);
            const double ep = std::exp(ap), en = std::exp(an);
            const double f = gLin * vf + kIs * (ep - en) - ig;
            const double df = gLin + kIs * (ep / (2.0 * kNvt) + en / kNvt);
            const double step = std::clamp(f / df, -0.25, 0.25);
            vf -= step;
            if (std::abs(step) < 1e-9) break;
        }
        igPrev_ = ig;
        vfPrev_ = vf;

        const double o1 = kVSat * std::tanh((x + vf) / kVSat);
        const double o2 = kVSat * std::tanh(tonePole_.process(tone_.process(loop_.process(o1))) / kVSat);
        return levelHp_.process(o2);
    }

private:
    static constexpr double kPi = 3.14159265358979323846;
    static constexpr double kR1 = 10e3, kC1 = 0.047e-6, kR2 = 470e3, kRQ5 = 1e6;
    static constexpr double kC2 = 0.018e-6, kR4 = 100e3, kR5 = 33e3, kR6 = 4.7e3, kC3 = 0.047e-6;
    static constexpr double kR7 = 10e3, kC4 = 0.018e-6;
    static constexpr double kR8 = 470.0, kC5 = 0.027e-6, kR9 = 10e3, kC6 = 0.01e-6;
    static constexpr double kC7 = 1e-6, kR10 = 4.7e3, kR17 = 1e6;
    static constexpr double kIs = 7.5e-9, kNvt = 0.0259 * 1.9;  // the netlist's SIGNAL diode
    static constexpr double kGbw = 3e6, kVSat = 3.3;           // the 4558 type
    static constexpr double kMinOhms = 50.0;
    // The input network: R1 and C1 into the base bias resistors (R2 || RQ5), then the ideal buffer.
    static constexpr double kRb = kR2 * kRQ5 / (kR2 + kRQ5);
    static constexpr double kInGain = kRb / (kR1 + kRb), kInTau = (kR1 + kRb) * kC1;

    // A second order section n0 + n1 s + n2 s^2 over 1 + d1 s + d2 s^2 (bilinear transform), keeping its state when it is retuned.
    class Second {
    public:
        Second() = default;
        Second(double fs, double n0, double n1, double n2, double d1, double d2) {
            const double k = 2.0 * fs, k2 = k * k;
            const double a0 = 1.0 + d1 * k + d2 * k2;
            b0_ = (n0 + n1 * k + n2 * k2) / a0;
            b1_ = 2.0 * (n0 - n2 * k2) / a0;
            b2_ = (n0 - n1 * k + n2 * k2) / a0;
            a1_ = 2.0 * (1.0 - d2 * k2) / a0;
            a2_ = (1.0 - d1 * k + d2 * k2) / a0;
        }
        void retune(const Second& o) { b0_ = o.b0_; b1_ = o.b1_; b2_ = o.b2_; a1_ = o.a1_; a2_ = o.a2_; }
        double process(double x) {
            const double y = b0_ * x + b1_ * x1_ + b2_ * x2_ - a1_ * y1_ - a2_ * y2_;
            x2_ = x1_; x1_ = x;
            y2_ = y1_; y1_ = y;
            return y;
        }

    private:
        double b0_ = 1, b1_ = 0, b2_ = 0, a1_ = 0, a2_ = 0, x1_ = 0, x2_ = 0, y1_ = 0, y2_ = 0;
    };

    double fs_;
    TsControls controls_;
    FirstOrder hpIn_, hpC2_, loop_, tonePole_, levelHp_;
    Second tone_;
    double rF_ = kR5, c3State_ = 0, igPrev_ = 0, vfPrev_ = 0;
};

}  // namespace sas
