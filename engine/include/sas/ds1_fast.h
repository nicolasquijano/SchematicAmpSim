#pragma once
// Efficient DS-1 style distortion (engine/data/ds1.net): the circuit solved by hand instead of by the nodal solver (the idea of sas/sd1_fast.h and sas/rat_fast.h).
//   Q1, an emitter follower, is linear here (its gain and its input resistance from its bias point).
//   The booster Q2 is the nonlinear part of the first half: a full Ebers-Moll transistor with its emitter resistor, the 470k / 250p feedback from collector to base,
//   the 10k load, the 100k load of the op-amp's input coupling and, in front of the base, the input network (C2, R4, R and C3) reduced to its Norton equivalent. Its three
//   node voltages are one 3 x 3 Newton equation per sample, the capacitors are trapezoidal companions, and the operating point is solved once for the DC.
//   The TA7136 stage is the op-amp as the circuit has it (its internal node, the output limit, the DIST network): one equation in the internal node per sample, so the
//   saturation behaves as the circuit's does (the internal node winds up while the output is limited and the edge comes late).
//   The clipper (R14, C9, the two diodes to the bias) has the tone network and the LEVEL pot hanging on its node: that linear network is solved with a 4 x 4 matrix
//   inverted when a knob moves, and the diode voltage is one more Newton equation. After it only a gain and a high pass are left.

#include <algorithm>
#include <array>
#include <cmath>

#include "sas/filters.h"
#include "sas/rational.h"

namespace sas {

class Ds1Fast {
public:
    explicit Ds1Fast(double fs) : fs_(fs) {
        const double h = 1.0 / (2.0 * fs_);
        hc2_ = h / kC2; hc3_ = h / kC3; hc5_ = h / kC5; hc9_ = h / kC9; hc8_ = h / kC8;
        hInt_ = h;
        gc4_ = 2.0 * kC4 * fs_;
        g10_ = 2.0 * kC10 * fs_; g11_ = 2.0 * kC11 * fs_; g12_ = 2.0 * kC12 * fs_;
        // Input: R1 and C1 into the follower's input resistance (beta (R3 + re) in parallel with R2), then the follower's gain.
        const double ie = 0.333e-3, re = kVt / ie, rin = 1.0 / (1.0 / kR2 + 1.0 / (kBeta1 * (kR3 + re)));
        const double tauIn = (kR1 + rin) * kC1, gainIn = rin / (kR1 + rin) * kR3 / (kR3 + re);
        hpIn_ = FirstOrder(fs_, 0.0, gainIn * tauIn, tauIn);
        rs_ = re + kR1 / kBeta1;
        hpOut_ = FirstOrder(fs_, 0.0, kOutGain / (2.0 * kPi * 1.6), 1.0 / (2.0 * kPi * 1.6));
        operatingPoint();
        setParts(50e3, 50e3, 10e3, 10e3, 50e3, 50e3);
    }

    // The pots as the circuit has them: DIST (VR1: top to the wiper, wiper to the bottom), TONE (VR3) and LEVEL (VR2).
    void setParts(double distTop, double distBottom, double toneTop, double toneBottom, double levelTop, double levelBottom) {
        auto lim = [](double v) { return std::max(v, 50.0); };
        distTop = lim(distTop); distBottom = lim(distBottom); toneTop = lim(toneTop); toneBottom = lim(toneBottom); levelTop = lim(levelTop); levelBottom = lim(levelBottom);
        if (have_ && distTop == p_[0] && distBottom == p_[1] && toneTop == p_[2] && toneBottom == p_[3] && levelTop == p_[4] && levelBottom == p_[5]) return;
        have_ = true;
        p_ = {distTop, distBottom, toneTop, toneBottom, levelTop, levelBottom};

        // The op-amp stage, as the circuit has it: the internal node x (gm into the parallel of a0 / gm and gm / (2 pi gbw)), the output vsat tanh(x / vsat), and the feedback
        // network, linear: the inverting node w = kb out + hb, with ZT = Rt || C7 from the output to w and ZB = Rb + R13 + C8 from w to the bias (trapezoidal companions).
        // Solved as one equation in x per sample, so the saturation behaves like the circuit's: the internal node winds up while the output is limited, and the edge is late.
        const double rbs = distBottom + kR13, g7 = 2.0 * kC7 * fs_;
        gB_ = 1.0 / (rbs + hc8_);
        const double gt = 1.0 / distTop + g7;
        kb_ = gt / (gt + gB_);
        g7_ = g7;
        gt_ = gt;

        // The tone and level network on the clipper's node, nodes t8, tw, b7, hp1. The level pot's wiper is loaded by what follows it.
        const double botLoaded = levelBottom * kRLwLoad / (levelBottom + kRLwLoad);
        const double rtw = levelTop + botLoaded;
        levelRatio_ = botLoaded / rtw;
        const double g16 = 1.0 / kR16, g3t = 1.0 / toneTop, g3b = 1.0 / toneBottom, g15 = 1.0 / kR15, g17 = 1.0 / kR17, gtw = 1.0 / rtw;
        double m[4][8] = {};
        m[0][0] = g16 + g12_ + g3t;  m[0][1] = -g3t;
        m[1][0] = -g3t;              m[1][1] = g3t + g3b + gtw;  m[1][2] = -g3b;
        m[2][1] = -g3b;              m[2][2] = g3b + g17 + g15;  m[2][3] = -g15;
        m[3][2] = -g15;              m[3][3] = g15 + g11_;
        for (int i = 0; i < 4; ++i) m[i][4 + i] = 1.0;
        for (int c = 0; c < 4; ++c) {  // Gauss-Jordan
            int piv = c;
            for (int r = c + 1; r < 4; ++r)
                if (std::abs(m[r][c]) > std::abs(m[piv][c])) piv = r;
            for (int k = 0; k < 8; ++k) std::swap(m[c][k], m[piv][k]);
            const double d = m[c][c];
            for (int k = 0; k < 8; ++k) m[c][k] /= d;
            for (int r = 0; r < 4; ++r)
                if (r != c) {
                    const double f = m[r][c];
                    for (int k = 0; k < 8; ++k) m[r][k] -= f * m[c][k];
                }
        }
        for (int i = 0; i < 4; ++i)
            for (int j = 0; j < 4; ++j) minv_[i][j] = m[i][4 + j];
        // y = Minv (b x + hist): the node equations' right side from x is b = [g16, 0, 0, g11].
        for (int i = 0; i < 4; ++i) q_[i] = minv_[i][0] * g16 + minv_[i][3] * g11_;
        gSum_ = g16 + g11_ + g10_ - (q_[0] * g16 + q_[3] * g11_);
    }

    // One sample: guitar volts in, pedal output volts (AC coupled) out.
    double process(double vin) {
        const double e1 = hpIn_.process(vin);

        // ---- the booster: Newton on (vb, vc, ve)
        const double g2 = 1.0 / (rs_ + hc2_), g3 = 1.0 / (kRQ6 + hc3_), h2 = vC2_ + hc2_ * i2_, h3 = vC3_ + hc3_ * i3_;
        const double sSum = g2 + 1.0 / kR4 + g3;
        const double gin = g3 * (g2 + 1.0 / kR4) / sSum;
        const double ain = g3 * g2 * (e1 - h2) / sSum - gin * h3;
        const double hist4 = gc4_ * vcbPrev_ + i4_;
        const double h5 = vC5_ + hc5_ * i5_;
        transistor(ain, gin, gc4_, hist4, 1.0 / (kR10 + hc5_), h5, vb_, vc_, ve_);
        const double dvb = vb_ - vb0_, dvc = vc_ - vc0_;
        const double dn2 = (g2 * (e1 - h2) + g3 * (dvb + h3)) / sSum;
        const double i2 = g2 * (e1 - dn2 - h2);
        const double i3 = g3 * (dn2 - dvb - h3);
        vC2_ += hc2_ * (i2 + i2_); i2_ = i2;
        vC3_ += hc3_ * (i3 + i3_); i3_ = i3;
        i4_ = gc4_ * (vc_ - vb_) - hist4;
        vcbPrev_ = vc_ - vb_;
        const double i5 = (dvc - h5) / (kR10 + hc5_);
        vC5_ += hc5_ * (i5 + i5_); i5_ = i5;

        // ---- the op-amp stage
        const double vp = kR10 * i5;
        const double h7 = g7_ * v7_ + i7_, hB = vC8_ + hc8_ * i8_;
        const double hb = (gB_ * hB - h7) / (gt_ + gB_);
        const double cint = kGm / (2.0 * kPi * kGbw), rint = kA0 / kGm;
        const double a1 = 1.0 + hInt_ / (cint * rint), b1 = hInt_ * kGm / cint;
        const double rhs = xInt_ + hInt_ * dxPrev_ + b1 * vp;
        double xi = xInt_;
        for (int it = 0; it < 30; ++it) {
            const double t = std::tanh(xi / kVSat);
            const double g = a1 * xi + b1 * (kb_ * kVSat * t + hb) - rhs;
            const double dg = a1 + b1 * kb_ * (1.0 - t * t);
            const double step = std::clamp(g / dg, -2.0, 2.0);
            xi -= step;
            if (std::abs(step) < 1e-7) break;
        }
        const double e = kVSat * std::tanh(xi / kVSat);
        const double w = kb_ * e + hb;
        dxPrev_ = (kGm * (vp - w) - xi / rint) / cint;
        xInt_ = xi;
        {
            const double v7 = e - w;
            i7_ = g7_ * v7 - h7;
            v7_ = v7;
            const double iB = gB_ * (w - hB);
            vC8_ += hc8_ * (iB + i8_);
            i8_ = iB;
        }

        // ---- the clipper with the tone network on its node
        const double rs = kRout + kR14, hs = hc9_;
        std::array<double, 4> hist{g12_ * v12_ + i12_, 0.0, 0.0, -(g11_ * v11_ + i11_)};  // the capacitors' companion currents into t8 and into hp1
        std::array<double, 4> y0{};
        for (int i = 0; i < 4; ++i)
            for (int j = 0; j < 4; ++j) y0[i] += minv_[i][j] * hist[j];
        const double ih11 = g11_ * v11_ + i11_, ih10 = g10_ * v10_ + i10_;
        const double hx = y0[0] * (1.0 / kR16) + g11_ * y0[3] + ih11 + ih10;
        double x = xPrev_;
        for (int it = 0; it < 40; ++it) {
            const double arg = std::clamp(x / kNvt, -600.0, 600.0);
            const double ep = std::exp(arg), em = 1.0 / ep;
            const double isrc = (e - x - vC9_ - hs * i9_) / (rs + hs);
            const double f = isrc - kIs * (ep - em) - (gSum_ * x - hx);
            const double df = -1.0 / (rs + hs) - kIs * (ep + em) / kNvt - gSum_;
            const double step = std::clamp(f / df, -0.2, 0.2);
            x -= step;
            if (std::abs(step) < 1e-8) break;
        }
        const double isrc = (e - x - vC9_ - hs * i9_) / (rs + hs);
        vC9_ += hs * (isrc + i9_); i9_ = isrc;
        std::array<double, 4> y{};
        for (int i = 0; i < 4; ++i) y[i] = q_[i] * x + y0[i];
        i12_ = g12_ * y[0] - (g12_ * v12_ + i12_); v12_ = y[0];
        i11_ = g11_ * (x - y[3]) - ih11; v11_ = x - y[3];
        i10_ = g10_ * x - ih10; v10_ = x;
        xPrev_ = x;

        // The solver's op-amp returns its output-stage current (e / rout) through the bias node, which the netlist gives 1 ohm: the bias node moves by about 1 % of the op-amp's
        // output. Everything of the clipper, the tone network and the level pot hangs on the bias node, so it moves with it, and the output path (which is referenced to ground)
        // takes the movement as a signal that the LEVEL pot does not attenuate: up to 2.5 dB at full distortion. The model carries it as the circuit has it.
        const double bias = kRipple * e;
        probe[0] = bias; probe[1] = e; probe[2] = x + bias; probe[3] = y[1] + bias;
        return hpOut_.process(y[1] * levelRatio_ + kRippleToOut * bias);
    }

    double probe[4] = {};  // for the tests: the collector, the op-amp's output, the clipper node and the tone pot's wiper (deviations from their DC)

private:
    static constexpr double kPi = 3.14159265358979323846;
    // input and follower
    static constexpr double kR1 = 1e3, kC1 = 0.047e-6, kR2 = 470e3, kR3 = 10e3, kBeta1 = 300.0, kVt = 0.02585;
    // the booster
    static constexpr double kC2 = 0.47e-6, kR4 = 100e3, kRQ6 = 300.0, kC3 = 0.047e-6, kR6 = 100e3, kR7 = 470e3, kC4 = 250e-12, kR9 = 22.0, kR8 = 10e3, kVa = 9.0;
    static constexpr double kIsQ = 1e-14, kBf = 300.0, kBr = 4.0;
    static constexpr double kC5 = 0.47e-6, kR10 = 100e3;
    // the op-amp (TA7136) and the DIST network
    static constexpr double kA0 = 1e5, kGm = 1e-3, kGbw = 3e5, kRout = 100.0, kVSat = 3.6, kC7 = 250e-12, kR13 = 4.7e3, kC8 = 1e-6;
    // the clipper, tone and level
    static constexpr double kR14 = 2.2e3, kC9 = 0.47e-6, kC10 = 0.01e-6, kR16 = 6.8e3, kC12 = 0.1e-6, kC11 = 0.022e-6, kR15 = 2.2e3, kR17 = 6.8e3;
    static constexpr double kRLwLoad = 440e3;  // what the LEVEL wiper sees: R18, the switch and the follower's bias network
    static constexpr double kRipple = 0.0101;      // the bias node's movement per volt of the op-amp's output (1 ohm of the bias source over the output resistance)
    static constexpr double kRippleToOut = 1.010;  // and how much of it reaches the output relative to the level path's gain
    static constexpr double kOutGain = 0.972;   // the rest of the path to the output: bias network, the follower Q3, R22 / R23
    static constexpr double kIs = 7.5e-9, kNvt = 0.0259 * 1.9;  // the SIGNAL diode

    // The booster's equations. `ain`, `gin`: the input network as a Norton source into the base; the collector sees the feedback capacitor (gc4, hist4) and the op-amp's coupling
    // (g5, h5, zero for the DC). Solves (vb, vc, ve) from the given guesses.
    void transistor(double ain, double gin, double gc4, double hist4, double g5, double h5, double& vb, double& vc, double& ve) const {
        const double gfb = 1.0 / kR7 + gc4;
        for (int it = 0; it < 40; ++it) {
            const double vbe = std::min(vb - ve, 0.9), vbc = std::min(vb - vc, 0.9);
            const double ef = std::exp(vbe / kVt), er = vbc < -1.0 ? 0.0 : std::exp(vbc / kVt);  // the collector junction conducts only in saturation
            const double jf = kIsQ * (ef - 1.0), jr = kIsQ * (er - 1.0), gf = kIsQ * ef / kVt, gr = kIsQ * er / kVt;
            const double ib = jf / kBf + jr / kBr, ic = jf - jr * (1.0 + 1.0 / kBr);
            const double dibb = gf / kBf + gr / kBr, dibc = -gr / kBr, dibe = -gf / kBf;
            const double dicb = gf - gr * (1.0 + 1.0 / kBr), dicc = gr * (1.0 + 1.0 / kBr), dice = -gf;
            const double ifb = gfb * (vc - vb) - hist4;
            const double i5 = g5 * (vc - vc0_ - h5);
            const double f1 = ain - gin * (vb - vb0_) + ifb - ib - vb / kR6;
            const double f2 = (kVa - vc) / kR8 - ic - ifb - i5;
            const double f3 = ve - kR9 * (ib + ic);
            double j[3][4] = {
                {-gin - gfb - dibb - 1.0 / kR6, gfb - dibc, -dibe, -f1},
                {gfb - dicb, -1.0 / kR8 - dicc - gfb - g5, -dice, -f2},
                {-kR9 * (dibb + dicb), -kR9 * (dibc + dicc), 1.0 - kR9 * (dibe + dice), -f3}};
            for (int c = 0; c < 3; ++c) {
                int piv = c;
                for (int r = c + 1; r < 3; ++r)
                    if (std::abs(j[r][c]) > std::abs(j[piv][c])) piv = r;
                for (int k = 0; k < 4; ++k) std::swap(j[c][k], j[piv][k]);
                for (int r = c + 1; r < 3; ++r) {
                    const double f = j[r][c] / j[c][c];
                    for (int k = c; k < 4; ++k) j[r][k] -= f * j[c][k];
                }
            }
            double d[3];
            for (int r = 2; r >= 0; --r) {
                double s = j[r][3];
                for (int k = r + 1; k < 3; ++k) s -= j[r][k] * d[k];
                d[r] = s / j[r][r];
            }
            const double lim = 0.5;
            for (double& v : d) v = std::clamp(v, -lim, lim);
            vb += d[0]; vc += d[1]; ve += d[2];
            if (std::abs(d[0]) + std::abs(d[1]) + std::abs(d[2]) < 1e-8) break;
        }
    }

    void operatingPoint() {
        double vb = 0.65, vc = 4.0, ve = 0.01;
        vb0_ = vb; vc0_ = vc;
        transistor(0.0, 0.0, 0.0, 0.0, 0.0, 0.0, vb, vc, ve);
        vb_ = vb0_ = vb; vc_ = vc0_ = vc; ve_ = ve;
        vcbPrev_ = vc - vb;  // the feedback capacitor starts charged to the DC collector-base voltage
    }

    double fs_;
    bool have_ = false;
    std::array<double, 6> p_{};
    FirstOrder hpIn_, hpOut_;
    double hc8_ = 0, hInt_ = 0, gB_ = 0, kb_ = 0, g7_ = 0, gt_ = 0;
    double xInt_ = 0, dxPrev_ = 0, v7_ = 0, i7_ = 0, vC8_ = 0, i8_ = 0;  // the op-amp stage: its internal node and the feedback network
    double hc2_ = 0, hc3_ = 0, hc5_ = 0, hc9_ = 0, gc4_ = 0, g10_ = 0, g11_ = 0, g12_ = 0, rs_ = 80.0;
    double minv_[4][4] = {};
    double q_[4] = {}, gSum_ = 0, levelRatio_ = 0;
    // the booster's state
    double vb0_ = 0, vc0_ = 0, vb_ = 0, vc_ = 0, ve_ = 0;
    double vC2_ = 0, i2_ = 0, vC3_ = 0, i3_ = 0, i4_ = 0, vcbPrev_ = 0, vC5_ = 0, i5_ = 0;
    // the clipper's
    double vC9_ = 0, i9_ = 0, xPrev_ = 0, v12_ = 0, i12_ = 0, v11_ = 0, i11_ = 0, v10_ = 0, i10_ = 0;
};

}  // namespace sas
