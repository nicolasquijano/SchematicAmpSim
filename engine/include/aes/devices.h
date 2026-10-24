#pragma once
// Device models with analytic derivatives. Port of jcm800/src/tube.rs.
// Volts, amps, farads. Every evaluation returns the partials the Newton solver needs.

#include <array>
#include <cmath>

namespace aes {

// ln(1 + e^x) and its derivative (the logistic function), numerically stable.
inline void softplus(double x, double& sp, double& sig) {
    if (x > 35.0) {
        sp = x;
        sig = 1.0;
    } else if (x < -35.0) {
        const double e = std::exp(x);
        sp = e;
        sig = e;
    } else {
        const double e = std::exp(x);
        sp = std::log1p(e);
        sig = e / (1.0 + e);
    }
}

// Koren plate current plus a Dempwolf-style grid current.
struct TriodeModel {
    double mu, ex, kg1, kp, kvb;
    double gg, cg, xi;        // Ig = gg * (ln(1+exp(cg*vgk))/cg)^xi
    double cgk, cgp, cpk;     // interelectrode capacitances

    struct Plate { double ip, dvgk, dvpk; };
    struct Grid { double ig, dvgk; };

    // ECC83 / 12AX7 with the standard Koren parameters.
    static constexpr TriodeModel ecc83() {
        return {100.0, 1.4, 1060.0, 600.0, 300.0, 6.177e-4, 9.901, 1.314, 1.6e-12, 1.7e-12, 0.46e-12};
    }

    Plate plate(double vgk, double vpk) const {
        if (vpk <= 0.0) return {0, 0, 0};
        const double s = std::sqrt(kvb + vpk * vpk);
        const double x = kp * (1.0 / mu + vgk / s);
        double sp, sig;
        softplus(x, sp, sig);
        const double e1 = vpk / kp * sp;
        if (e1 <= 0.0) return {0, 0, 0};
        const double pw = std::pow(e1, ex - 1.0);
        const double ip = e1 * pw / kg1;
        const double de1Dvgk = vpk * sig / s;
        const double de1Dvpk = sp / kp - vpk * vpk * vgk * sig / (s * s * s);
        const double k = ex * pw / kg1;
        return {ip, k * de1Dvgk, k * de1Dvpk};
    }

    Grid grid(double vgk) const {
        const double x = cg * vgk;
        // Far below conduction the grid current is < 1e-12 A: skip the transcendentals.
        if (x < -16.0) return {0, 0};
        double sp, sig;
        softplus(x, sp, sig);
        const double base = sp / cg;
        const double pw = std::pow(base, xi - 1.0);
        return {gg * base * pw, gg * xi * pw * sig};
    }
};

// Koren pentode; suppressor tied to cathode (EL34).
struct PentodeModel {
    double mu, ex, kg1, kg2, kp, kvb;
    double gg, cg, xi;
    double cg1k, cg1p, cpk;

    struct Eval {
        std::array<double, 3> cur;                  // [ig1, ip, ig2]
        std::array<std::array<double, 3>, 3> d;     // d[row][col], col = [vg1k, vpk, vg2k]
    };

    // EL34 with kg1/kg2 scaled 1.3x so four of them make ~110 W.
    static constexpr PentodeModel el34() {
        return {11.0, 1.35, 845.0, 2600.0, 60.0, 24.0, 1.5e-3, 8.0, 1.3, 15e-12, 1.0e-12, 8e-12};
    }

    Eval eval(double vg1k, double vpk, double vg2k) const {
        Eval r{};
        const double x = cg * vg1k;
        if (x >= -30.0) {
            double sp, sig;
            softplus(x, sp, sig);
            const double base = sp / cg;
            const double pw = std::pow(base, xi - 1.0);
            r.cur[0] = gg * base * pw;
            r.d[0][0] = gg * xi * pw * sig;
        }
        if (vg2k <= 1e-6) return r;
        const double xe = kp * (1.0 / mu + vg1k / vg2k);
        double sp, sig;
        softplus(xe, sp, sig);
        const double e1 = vg2k / kp * sp;
        if (e1 <= 1e-14) return r;
        const double pw = std::pow(e1, ex - 1.0);
        const double e1ex = e1 * pw;
        const double de1Dg1 = sig;
        const double de1Dg2 = sp / kp - sig * vg1k / vg2k;

        r.cur[2] = e1ex / kg2;
        r.d[2][0] = ex * pw / kg2 * de1Dg1;
        r.d[2][2] = ex * pw / kg2 * de1Dg2;

        if (vpk > 0.0) {
            const double at = std::atan(vpk / kvb);
            const double dat = kvb / (kvb * kvb + vpk * vpk);
            const double k = 2.0 / kg1;
            r.cur[1] = k * e1ex * at;
            r.d[1][0] = k * ex * pw * at * de1Dg1;
            r.d[1][1] = k * e1ex * dat;
            r.d[1][2] = k * ex * pw * at * de1Dg2;
        }
        return r;
    }
};

// i = is * (exp(v / nvt) - 1), continued linearly above vcrit so Newton cannot overflow.
struct DiodeModel {
    double is, nvt;

    struct Eval { double i, g; };

    static constexpr DiodeModel rectifierBridge() { return {7.5e-9, 0.0517}; }
    static constexpr DiodeModel signal() { return {7.5e-9, 0.0259 * 1.9}; }

    Eval eval(double v) const {
        const double vcrit = 25.0 * nvt;
        if (v > vcrit) {
            const double e = std::exp(vcrit / nvt);
            const double g = is * e / nvt;
            return {is * (e - 1.0) + g * (v - vcrit), g};
        }
        const double e = std::exp(v / nvt);
        return {is * (e - 1.0), is * e / nvt};
    }
};

// NPN in forward-active operation: the base-emitter junction is a diode, Ic = beta * Ib.
struct BjtModel {
    double is, beta, nvt;

    struct Eval { double ib, g; };

    static constexpr BjtModel c1815() { return {1e-14, 350.0, 0.02585}; }

    Eval baseCurrent(double vbe) const {
        const double ibs = is / beta;
        const double vcrit = 30.0 * nvt;
        if (vbe > vcrit) {
            const double e = std::exp(vcrit / nvt);
            const double g = ibs * e / nvt;
            return {ibs * (e - 1.0) + g * (vbe - vcrit), g};
        }
        const double e = std::exp(vbe / nvt);
        return {ibs * (e - 1.0), ibs * e / nvt};
    }
};

// Op-amp as gm into a dominant-pole node, then a tanh output stage around the reference node.
struct OpampModel {
    double gm, a0, gbw, rout, vsat;

    struct Eval { double i, g; };

    static constexpr OpampModel rc4558() { return {1e-3, 3e4, 3e6, 75.0, 3.3}; }

    Eval stage(double x) const {
        const double t = std::tanh(x / vsat);
        return {vsat / rout * t, (1.0 - t * t) / rout};
    }
};

}  // namespace aes
