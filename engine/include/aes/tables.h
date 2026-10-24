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

#include "aes/devices.h"

namespace aes {

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

}  // namespace aes
