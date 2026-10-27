#pragma once
// Device models with analytic derivatives. Port of jcm800/src/tube.rs.
// Volts, amps, farads. Every evaluation returns the partials the Newton solver needs.

#include <array>
#include <cmath>

namespace sas {

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

    // 12AX7 fitted to the average plate characteristics of the GE data sheet ET-T509B (tools/tubefit): 1.17 mA,
    // gm 1.84 mA/V, ra 55k at 250 V / -2 V (data sheet: 1.2 mA, 1.6 mA/V, 62.5k). The stock Koren values above give
    // 0.48 mA, 0.83 mA/V, 107k there. The Dempwolf grid current and the capacitances are the same.
    static constexpr TriodeModel ecc83Fit() {
        return {104.7, 1.3, 474.0, 745.0, 13963.0, 6.177e-4, 9.901, 1.314, 1.6e-12, 1.7e-12, 0.46e-12};
    }

    // 12AT7 fitted to the Mullard ECC81 average plate characteristics (tools/tubefit): 8.1 mA, gm 4.4 mA/V, ra 12.5k
    // at 250 V / -2 V (the data sheet gives 7 to 14 mA and 4.5 to 6.5 mA/V), 8% rms against the curves.
    static constexpr TriodeModel ecc81Fit() {
        return {69.4, 1.4, 318.0, 189.0, 1981.0, 6.177e-4, 9.901, 1.314, 1.6e-12, 1.7e-12, 0.46e-12};
    }

    // 12AY7 (the Fender 5E3's input tube) fitted to the typical operating point: 250 V / -4 V gives 3.0 mA, gm 1.75 mA/V,
    // ra 25k (mu about 44; the tube is sold with wide limits: 2.2 to 3.75 mA, 1.3 to 2.1 mA/V).
    static constexpr TriodeModel ay7Fit() {
        return {45.5, 1.3, 744.8, 300.0, 9221.4, 6.177e-4, 9.901, 1.314, 1.6e-12, 1.7e-12, 0.46e-12};
    }

    // ECC81 / 12AT7 with the Koren parameters: the medium-mu twin triode of the Fender phase inverter.
    static constexpr TriodeModel ecc81() {
        return {60.0, 1.35, 460.0, 300.0, 300.0, 6.177e-4, 9.901, 1.314, 1.6e-12, 1.7e-12, 0.46e-12};
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

    // EL34 with kg1/kg2 scaled 1.3x so four of them make ~110 W. Checked against the JJ data sheet: at Ug2 375 / 425 V (where
    // the amps run) its transfer curves are within 6% rms; a fit to the Ug2 = 250 V plate curves alone fits those worse (8 to 10%)
    // and makes a JCM800 give 170 W, so it was kept.
    static constexpr PentodeModel el34() {
        return {11.0, 1.35, 845.0, 2600.0, 60.0, 24.0, 1.5e-3, 8.0, 1.3, 15e-12, 1.0e-12, 8e-12};
    }

    // KT88 / 6550 fitted to the JJ plate curves at Ug2 300 V and the typical point (mu 8, 140 mA, 11.5 mA/V at 250 / 250 /
    // -15 V; about 5.5 mA of screen current, the data sheet says at most 7): 8% rms, the stock one 17%.
    static constexpr PentodeModel el6550Fit() {
        return {8.0, 1.35, 977.0, 8500.0, 37.3, 19.8, 1.5e-3, 8.0, 1.3, 14e-12, 0.8e-12, 12e-12};
    }

    // 6550 / KT88 family: Koren's KT88 shape (kg1 730, kg2 4200, kp 32, kvb 16, mu 8.8). The
    // current scale is set per amp by the `scale` argument of the netlist's P element, so this is
    // the stock curve, not a fit to one amp.
    static constexpr PentodeModel el6550() {
        return {8.8, 1.35, 730.0, 4200.0, 32.0, 16.0, 1.5e-3, 8.0, 1.3, 14e-12, 0.8e-12, 12e-12};
    }

    // 6L6GC beam tetrode with the usual Koren parameters. As for the other power tubes the current
    // scale is set per amp by the `scale` argument of the netlist's P element.
    static constexpr PentodeModel el6l6gc() {
        return {8.7, 1.35, 1460.0, 4500.0, 48.0, 12.0, 1.5e-3, 8.0, 1.3, 11e-12, 0.9e-12, 6e-12};
    }

    // EL84 fitted to the Philips/Mullard class A point (Ua = Ug2 = 250 V, Ug1 = -7.3 V: 48 mA, gm 11.3 mS, ra 38 k, Ig2 5.5 mA);
    // mu and kp are the usual Koren values, kg1/kvb/kg2 solve the three data points.
    static constexpr PentodeModel el84() {
        return {19.56, 1.35, 578.6, 1833.0, 152.4, 49.0, 1.5e-3, 8.0, 1.3, 10.8e-12, 0.5e-12, 7e-12};
    }

    // 6V6GT fitted to the class A points (250/250/-12.5 V: 45 mA, 4.1 mA/V; 315/225/-13 V: 34 mA, 3.75 mA/V) and the
    // push-pull idle (285/285/-19 V: 35 mA per tube); plate resistance comes out 41k (data sheet 52k) and 88k (77k).
    static constexpr PentodeModel el6v6() {
        return {9.15, 1.35, 2334.1, 10300.0, 76.1, 48.9, 1.5e-3, 8.0, 1.3, 9.5e-12, 0.7e-12, 7.5e-12};
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

}  // namespace sas
