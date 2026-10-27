#pragma once
// Table-driven triode evaluation for the cheap modes. Koren's triode is
//   ip = A(vpk) * B(x) / kg1,   A = (vpk/kp)^ex,   B = softplus(x)^ex,   x = kp (1/mu + vgk / sqrt(kvb + vpk^2))
// and the grid current is gg * softplus(cg vgk)^xi / cg^xi, so every transcendental sits in a
// one-variable function (A, B and the grid's G). Those three are tabulated with cubic Hermite
// interpolation (value and slope at the nodes), which is smooth (C1), accurate to ~1e-9 relative and
// returns the exact derivative of the interpolant, so Newton sees a consistent Jacobian. What is left
// per evaluation is one sqrt and a few table lookups instead of exp, log1p and two pow calls.

#include <cmath>
#include <vector>

#include "sas/devices.h"

namespace sas {

// A function on a uniform grid, Hermite-interpolated. Outside the grid `lo`/`hi` are used.
class HermiteTable {
public:
    template <class F, class DF>
    HermiteTable(double x0, double x1, std::size_t n, F&& f, DF&& df) : x0_(x0), h_((x1 - x0) / double(n - 1)), inv_(1.0 / h_) {
        y_.resize(n);
        d_.resize(n);
        for (std::size_t i = 0; i < n; ++i) {
            const double x = x0 + h_ * double(i);
            y_[i] = f(x);
            d_[i] = df(x);
        }
    }

    double lo() const { return x0_; }
    double hi() const { return x0_ + h_ * double(y_.size() - 1); }

    // Value and derivative at x, which must lie inside [lo, hi].
    void eval(double x, double& y, double& dy) const {
        double u = (x - x0_) * inv_;
        std::size_t i = std::size_t(u);
        if (i >= y_.size() - 1) i = y_.size() - 2;
        const double t = u - double(i), t2 = t * t, t3 = t2 * t;
        const double y0 = y_[i], y1 = y_[i + 1], m0 = d_[i] * h_, m1 = d_[i + 1] * h_;
        y = (2 * t3 - 3 * t2 + 1) * y0 + (t3 - 2 * t2 + t) * m0 + (-2 * t3 + 3 * t2) * y1 + (t3 - t2) * m1;
        dy = ((6 * t2 - 6 * t) * y0 + (3 * t2 - 4 * t + 1) * m0 + (-6 * t2 + 6 * t) * y1 + (3 * t2 - 2 * t) * m1) * inv_;
    }

private:
    double x0_, h_, inv_;
    std::vector<double> y_, d_;
};

class TriodeTable {
public:
    explicit TriodeTable(const TriodeModel& m)
        : m_(m),
          plateB_(-45.0, 70.0, 9201, [&](double x) { return std::pow(softplusValue(x), m.ex); },
                  [&](double x) { return m.ex * std::pow(softplusValue(x), m.ex - 1.0) * logistic(x); }),
          plateA_(0.0, 900.0, 9001, [&](double v) { return std::pow(v / m.kp, m.ex); },
                  [&](double v) { return v > 0 ? m.ex / m.kp * std::pow(v / m.kp, m.ex - 1.0) : 0.0; }),
          grid_(-16.0, 70.0, 6881, [&](double x) { return std::pow(softplusValue(x), m.xi); },
                [&](double x) { return m.xi * std::pow(softplusValue(x), m.xi - 1.0) * logistic(x); }),
          gridScale_(m.gg / std::pow(m.cg, m.xi)) {}

    TriodeModel::Plate plate(double vgk, double vpk) const {
        if (vpk <= 0.0) return {0, 0, 0};
        const double s = std::sqrt(m_.kvb + vpk * vpk);
        const double x = m_.kp * (1.0 / m_.mu + vgk / s);
        double b, db;
        if (x < plateB_.lo()) return {0, 0, 0};  // e^(ex x) < 1e-27: nothing flows
        if (x > plateB_.hi()) {
            const double sp = x;  // softplus(x) = x for large x
            b = std::pow(sp, m_.ex);
            db = m_.ex * std::pow(sp, m_.ex - 1.0);
        } else {
            plateB_.eval(x, b, db);
        }
        double a, da;
        if (vpk < plateA_.hi()) {
            plateA_.eval(vpk, a, da);
        } else {
            a = std::pow(vpk / m_.kp, m_.ex);
            da = m_.ex / m_.kp * std::pow(vpk / m_.kp, m_.ex - 1.0);
        }
        const double invKg = 1.0 / m_.kg1;
        const double dxDvgk = m_.kp / s;
        const double dxDvpk = -m_.kp * vgk * vpk / (s * s * s);
        return {a * b * invKg, a * db * dxDvgk * invKg, (da * b + a * db * dxDvpk) * invKg};
    }

    TriodeModel::Grid grid(double vgk) const {
        const double x = m_.cg * vgk;
        if (x < -16.0) return {0, 0};
        double g, dg;
        if (x > grid_.hi()) {
            g = std::pow(x, m_.xi);
            dg = m_.xi * std::pow(x, m_.xi - 1.0);
        } else {
            grid_.eval(x, g, dg);
        }
        return {gridScale_ * g, gridScale_ * dg * m_.cg};
    }

    const TriodeModel& model() const { return m_; }

private:
    static double softplusValue(double x) {
        double sp, sig;
        softplus(x, sp, sig);
        return sp;
    }
    static double logistic(double x) {
        double sp, sig;
        softplus(x, sp, sig);
        return sig;
    }

    TriodeModel m_;
    HermiteTable plateB_, plateA_, grid_;
    double gridScale_;
};

// The same idea for Koren's pentode / beam tetrode:
//   E = (vg2 / kp)^ex * softplus(x)^ex,  x = kp (1/mu + vg1 / vg2),   ig2 = E / kg2,   ip = 2 E atan(vpk / kvb) / kg1,
// and the grid current gg * softplus(cg vg1)^xi / cg^xi. The two powers, the softplus and the arctangent become four one-variable
// tables (A(vg2), B(x), the grid's G, atan), so an evaluation is a few lookups instead of two exp, two log1p, two pow and an atan.
class PentodeTable {
public:
    explicit PentodeTable(const PentodeModel& m)
        : m_(m),
          b_(-45.0, 70.0, 9201, [&](double x) { return std::pow(softplusValue(x), m.ex); },
             [&](double x) { return m.ex * std::pow(softplusValue(x), m.ex - 1.0) * logistic(x); }),
          a_(0.0, 900.0, 9001, [&](double v) { return std::pow(v / m.kp, m.ex); },
             [&](double v) { return v > 0 ? m.ex / m.kp * std::pow(v / m.kp, m.ex - 1.0) : 0.0; }),
          grid_(-16.0, 70.0, 6881, [&](double x) { return std::pow(softplusValue(x), m.xi); },
                [&](double x) { return m.xi * std::pow(softplusValue(x), m.xi - 1.0) * logistic(x); }),
          atan_(0.0, 100.0, 20001, [](double u) { return std::atan(u); }, [](double u) { return 1.0 / (1.0 + u * u); }),
          gridScale_(m.gg / std::pow(m.cg, m.xi)) {}

    PentodeModel::Eval eval(double vg1k, double vpk, double vg2k) const {
        PentodeModel::Eval r{};
        const double xg = m_.cg * vg1k;
        if (xg >= grid_.lo()) {
            double g, dg;
            if (xg > grid_.hi()) {
                g = std::pow(xg, m_.xi);
                dg = m_.xi * std::pow(xg, m_.xi - 1.0);
            } else {
                grid_.eval(xg, g, dg);
            }
            r.cur[0] = gridScale_ * g;
            r.d[0][0] = gridScale_ * dg * m_.cg;
        }
        if (vg2k <= 1e-6) return r;
        const double x = m_.kp * (1.0 / m_.mu + vg1k / vg2k);
        if (x < b_.lo()) return r;
        double b, db;
        if (x > b_.hi()) {
            b = std::pow(x, m_.ex);
            db = m_.ex * std::pow(x, m_.ex - 1.0);
        } else {
            b_.eval(x, b, db);
        }
        double a, da;
        if (vg2k < a_.hi()) {
            a_.eval(vg2k, a, da);
        } else {
            a = std::pow(vg2k / m_.kp, m_.ex);
            da = m_.ex / m_.kp * std::pow(vg2k / m_.kp, m_.ex - 1.0);
        }
        const double e = a * b;
        if (e <= 1e-14) return r;
        const double dxDg1 = m_.kp / vg2k;
        const double dEDg1 = a * db * dxDg1;
        const double dEDg2 = da * b - a * db * m_.kp * vg1k / (vg2k * vg2k);
        const double invKg2 = 1.0 / m_.kg2;
        r.cur[2] = e * invKg2;
        r.d[2][0] = dEDg1 * invKg2;
        r.d[2][2] = dEDg2 * invKg2;
        if (vpk > 0.0) {
            const double u = vpk / m_.kvb;
            double at, dat;  // atan(u) and its derivative with respect to u
            if (u < atan_.hi()) {
                atan_.eval(u, at, dat);
            } else {
                at = std::atan(u);
                dat = 1.0 / (1.0 + u * u);
            }
            const double k = 2.0 / m_.kg1;
            r.cur[1] = k * e * at;
            r.d[1][0] = k * dEDg1 * at;
            r.d[1][1] = k * e * dat / m_.kvb;
            r.d[1][2] = k * dEDg2 * at;
        }
        return r;
    }

    const PentodeModel& model() const { return m_; }

private:
    static double softplusValue(double x) {
        double sp, sig;
        softplus(x, sp, sig);
        return sp;
    }
    static double logistic(double x) {
        double sp, sig;
        softplus(x, sp, sig);
        return sig;
    }

    PentodeModel m_;
    HermiteTable b_, a_, grid_, atan_;
    double gridScale_;
};

}  // namespace sas
