#pragma once
// Efficient BD-2 style overdrive (engine/data/bd2.net): the circuit solved by hand instead of by the nodal solver (the idea of sas/sd1_fast.h, sas/rat_fast.h and
// sas/ds1_fast.h), built from a few small linear networks (sas/trapnet.h) and the three nonlinear things in the path:
//   - the op-amps (the two gain stages S1 and S2, "BD2_STAGE": a gain of 700, a 2.2k output resistance, a 7 MHz gain-bandwidth, and the third op-amp, a 4558): each one is solved
//     as the circuit has it, the internal node, the output limit and the feedback network together (one Newton equation in the internal node per sample, as in sas/ds1_fast.h);
//   - the clipper between the two gain stages (two diodes in series each way to ground), with the network around it (100k, 100n, 220p, 330k, 47n, 1M, 15k and the next
//     stage's 2.2n coupling): the diodes take a current out of one node of that network, which is one Newton equation in that node's voltage;
//   - nothing else: the input and output buffers are ideal followers, the couplings in front of the first stage and after the last are high passes.
// The second gain stage drives the tone, LEVEL and the third stage's input, all in one network, since the stage's 2.2k output resistance feels them.

#include <algorithm>
#include <array>
#include <cmath>

#include "sas/filters.h"
#include "sas/trapnet.h"

namespace sas {

class Bd2Fast {
public:
    explicit Bd2Fast(double fs) : fs_(fs) {
        hpA_ = FirstOrder(fs_, 0.0, kInGain * kInTau, kInTau);
        hpB_ = FirstOrder(fs_, 0.0, 0.1527, 0.1527);
        hpC_ = FirstOrder(fs_, 0.0, 220e3 * 100e-9, 220e3 * 100e-9);
        hpD_ = FirstOrder(fs_, 0.0, 0.997 * 0.1003, 0.1003);
        hpE_ = FirstOrder(fs_, 0.0, 0.990 * 0.091, 0.091);
        // The network between the stages does not depend on any pot.
        net2_.init(6, fs_);  // 0 o1 (driven), 1 p1, 2 c22, 3 m, 4 q1, 5 n4
        net2_.addR(0, 1, 100e3);
        net2_.addC(1, 3, 100e-9);
        net2_.addC(0, 2, 220e-12);
        net2_.addR(2, 3, 330e3);
        net2_.addC(1, 4, 47e-9);
        net2_.addR(3, 4, 1e6);
        net2_.addR(4, -1, 15e3);
        net2_.addC(3, 5, 2.2e-9);
        net2_.addR(5, -1, 1e6);
        net2_.setDrive(0, 1e-3);  // the first stage's output taken as an ideal source
        net2_.finish();
        // The third stage with its gyrator.
        net4_.init(4, fs_);  // 0 o3 (driven), 1 f3, 2 gy, 3 gb
        net4_.addR(0, 1, 6.8e3);
        net4_.addC(0, 1, 2.2e-9);
        net4_.addC(1, 2, 56e-9);
        net4_.addDependentR(2, 3, 1.2e3);  // R12K goes to the follower's output, which is the voltage of gb
        net4_.addC(2, 3, 5.6e-9);
        net4_.addR(3, -1, 470e3);
        net4_.setDrive(0, kS3.rout);
        net4_.finish();
        setParts(125e3, 125e3, 5e3, 5e3, 50e3, 50e3);
    }

    // The pots as the circuit has them: the two gangs of GAIN (the resistance in the feedback of each stage), TONE (top and bottom) and LEVEL (top and bottom).
    void setParts(double gain1, double gain2, double toneTop, double toneBottom, double levelTop, double levelBottom) {
        auto lim = [](double v) { return std::max(v, 50.0); };
        gain1 = lim(gain1); gain2 = lim(gain2); toneTop = lim(toneTop); toneBottom = lim(toneBottom); levelTop = lim(levelTop); levelBottom = lim(levelBottom);
        if (have_ && gain1 == p_[0] && gain2 == p_[1] && toneTop == p_[2] && toneBottom == p_[3] && levelTop == p_[4] && levelBottom == p_[5]) return;
        have_ = true;
        p_ = {gain1, gain2, toneTop, toneBottom, levelTop, levelBottom};
        net1_.init(3, fs_);  // 0 o1 (driven), 1 f1, 2 g1
        net1_.addR(0, 1, gain1 + 22e3);
        net1_.addC(0, 1, 47e-12);
        net1_.addR(1, 2, 1.5e3);
        net1_.addC(2, -1, 150e-9);
        net1_.setDrive(0, kS12.rout);
        net1_.finish();
        net3_.init(9, fs_);  // 0 o2 (driven), 1 f2, 2 g2, 3 ta, 4 tb, 5 tw, 6 tg, 7 lv, 8 l (the third stage's + input)
        net3_.addR(0, 1, gain2 + 33e3);
        net3_.addC(0, 1, 100e-12);
        net3_.addR(1, 2, 2.2e3);
        net3_.addC(2, -1, 1e-6);
        net3_.addR(0, 3, 5.6e3);
        net3_.addC(0, 3, 5.6e-9);
        net3_.addC(3, 4, 18e-9);
        net3_.addR(4, 5, toneTop);
        net3_.addR(5, 6, toneBottom);
        net3_.addC(6, -1, 18e-9);
        net3_.addR(5, 7, levelTop);
        net3_.addR(7, -1, levelBottom);
        net3_.addC(7, 8, 47e-9);
        net3_.addR(8, -1, 10e6);
        net3_.setDrive(0, kS12.rout);
        net3_.finish();
    }

    // One sample: guitar volts in, pedal output volts (AC coupled) out.
    double process(double vin) {
        const double vp1 = hpC_.process(hpB_.process(hpA_.process(vin)));
        stage(net1_, 1, kS12, lp1_, vp1);
        const double o1 = net1_.voltage(0);

        // The clipper: the diodes take their current out of node m of the network.
        net2_.history();
        const double vth = net2_.node(3, o1), rth = net2_.rth(3);
        double m = mPrev_;
        for (int it = 0; it < 40; ++it) {
            const double arg = std::clamp(m / (2.0 * kNvt), -600.0, 600.0);
            const double ep = std::exp(arg), em = 1.0 / ep;
            const double f = m - vth + rth * kIs * (ep - em);
            const double df = 1.0 + rth * kIs * (ep + em) / (2.0 * kNvt);
            const double step = std::clamp(f / df, -0.3, 0.3);
            m -= step;
            if (std::abs(step) < 1e-8) break;
        }
        {
            const double arg = std::clamp(m / (2.0 * kNvt), -600.0, 600.0);
            const double ep = std::exp(arg);
            net2_.commit(o1, 3, kIs * (ep - 1.0 / ep));
        }
        mPrev_ = m;
        const double vp2 = net2_.voltage(5);

        stage(net3_, 1, kS12, lp2_, vp2);
        const double l = net3_.voltage(8);
        stage(net4_, 1, kS3, lp3_, l);
        const double o3 = net4_.voltage(0);
        return hpE_.process(hpD_.process(o3));
    }

private:
    static constexpr double kPi = 3.14159265358979323846;
    struct Amp {
        double gm, a0, gbw, rout, vsat;
    };
    static constexpr Amp kS12{1e-3, 700.0, 7e6, 2200.0, 3.3};  // BD2_STAGE
    static constexpr Amp kS3{1e-3, 3e4, 3e6, 75.0, 3.3};       // RC4558
    static constexpr double kIs = 7.5e-9, kNvt = 0.0259 * 1.9;  // the SIGNAL diode
    // The input: R2 and R3 with C1 (the JFET follower is ideal).
    static constexpr double kInTau = (10e3 + 1e6) * 47e-9, kInGain = 1e6 / (10e3 + 1e6);

    struct Loop {
        double x = 0.0, dx = 0.0;
    };

    // One op-amp with its feedback network: the internal node is the unknown (trapezoidal integration of gm (v+ - v-) into the parallel of a0 / gm and gm / (2 pi gbw)),
    // the output is vsat tanh(x / vsat) behind the output resistance, which is part of the network (its driven node); the network gives the inverting node (node f).
    void stage(TrapNet& net, int f, const Amp& a, Loop& lp, double vp) const {
        net.history();
        const double h = 1.0 / (2.0 * fs_);
        const double cint = a.gm / (2.0 * kPi * a.gbw), rint = a.a0 / a.gm;
        const double a1 = 1.0 + h / (cint * rint), b1 = h * a.gm / cint;
        const double rhs = lp.x + h * lp.dx + b1 * vp;
        const double qf = net.q(f), yf = net.y0(f);
        double x = lp.x;
        for (int it = 0; it < 30; ++it) {
            const double t = std::tanh(x / a.vsat);
            const double g = a1 * x + b1 * (qf * a.vsat * t + yf) - rhs;
            const double dg = a1 + b1 * qf * (1.0 - t * t);
            const double step = std::clamp(g / dg, -2.0, 2.0);
            x -= step;
            if (std::abs(step) < 1e-7) break;
        }
        const double e = a.vsat * std::tanh(x / a.vsat);
        lp.dx = (a.gm * (vp - (qf * e + yf)) - x / rint) / cint;
        lp.x = x;
        net.commit(e);
    }

    double fs_;
    bool have_ = false;
    std::array<double, 6> p_{};
    FirstOrder hpA_, hpB_, hpC_, hpD_, hpE_;
    TrapNet net1_, net2_, net3_, net4_;
    Loop lp1_, lp2_, lp3_;
    double mPrev_ = 0.0;
};

}  // namespace sas
