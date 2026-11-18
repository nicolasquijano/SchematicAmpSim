#pragma once
// A linear stage of a circuit given as its transfer function H(s) = num(s) / den(s) (polynomials in s, lowest power first), run as a cascade of second order
// sections. The nodal solver integrates with the trapezoidal rule, which is the bilinear transform s = 2 fs (z - 1) / (z + 1) of the analog circuit, so a filter
// built here from the circuit's H(s) answers like the solver's linear circuit does (the same warping near the Nyquist frequency). The poles and zeros are found
// numerically (Durand-Kerner), mapped one by one to the z plane and paired into real sections, which keeps the coefficients well conditioned even when the
// poles are decades apart (a 3 Hz op-amp pole and a 160 kHz input filter).

#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <initializer_list>
#include <vector>

namespace sas {

struct Poly {
    std::vector<double> c;  // c[k] multiplies s^k

    Poly() = default;
    Poly(std::initializer_list<double> v) : c(v) {}
    std::size_t degree() const {
        std::size_t d = c.size();
        while (d > 1 && c[d - 1] == 0.0) --d;
        return d == 0 ? 0 : d - 1;
    }
    friend Poly operator*(const Poly& a, const Poly& b) {
        Poly r;
        r.c.assign(a.c.size() + b.c.size() - 1, 0.0);
        for (std::size_t i = 0; i < a.c.size(); ++i)
            for (std::size_t j = 0; j < b.c.size(); ++j) r.c[i + j] += a.c[i] * b.c[j];
        return r;
    }
    friend Poly operator+(const Poly& a, const Poly& b) {
        Poly r;
        r.c.assign(std::max(a.c.size(), b.c.size()), 0.0);
        for (std::size_t i = 0; i < a.c.size(); ++i) r.c[i] += a.c[i];
        for (std::size_t i = 0; i < b.c.size(); ++i) r.c[i] += b.c[i];
        return r;
    }
    friend Poly operator*(double k, const Poly& a) {
        Poly r = a;
        for (auto& v : r.c) v *= k;
        return r;
    }
    std::complex<double> at(std::complex<double> s) const {
        std::complex<double> v = 0.0;
        for (std::size_t i = c.size(); i-- > 0;) v = v * s + c[i];
        return v;
    }
};

class RationalFilter {
public:
    RationalFilter() = default;

    // Replaces the coefficients, keeping the state of the sections that stay (a knob move does not click). `refHz` is where the gain is matched exactly.
    void design(const Poly& num, const Poly& den, double fs, double refHz = 1000.0) {
        using C = std::complex<double>;
        const double w0 = 2.0 * 3.14159265358979323846 * 1000.0;  // roots are found with s scaled by this, so the coefficients are of order one
        auto scaled = [&](const Poly& p) {
            std::vector<double> v(p.c.size());
            double f = 1.0;
            for (std::size_t k = 0; k < v.size(); ++k, f *= w0) v[k] = p.c[k] * f;
            return v;
        };
        const auto roots = [](std::vector<double> a) {
            while (a.size() > 1 && a.back() == 0.0) a.pop_back();
            const std::size_t n = a.size() - 1;
            std::vector<C> r(n);
            if (n == 0) return r;
            for (auto& v : a) v /= a.back();
            // zeros at the origin first (exact), the rest by Durand-Kerner
            std::size_t origin = 0;
            while (origin < n && a[origin] == 0.0) ++origin;
            const std::size_t m = n - origin;
            std::vector<C> z(m);
            for (std::size_t i = 0; i < m; ++i) z[i] = std::pow(C(0.4, 0.9), double(i)) * 1.2;
            for (int it = 0; it < 500; ++it) {
                double worst = 0.0;
                for (std::size_t i = 0; i < m; ++i) {
                    C p = 0.0, d = 1.0;
                    for (std::size_t k = a.size(); k-- > origin;) p = p * z[i] + a[k];
                    for (std::size_t j = 0; j < m; ++j)
                        if (j != i) d *= z[i] - z[j];
                    const C step = p / d;
                    z[i] -= step;
                    worst = std::max(worst, std::abs(step));
                }
                if (worst < 1e-13) break;
            }
            for (std::size_t i = 0; i < m; ++i) r[origin + i] = z[i];
            return r;
        };
        std::vector<C> poles = roots(scaled(den)), zeros = roots(scaled(num));
        const std::size_t nz = zeros.size(), np = poles.size();
        for (auto& p : poles) p *= w0;
        for (auto& z : zeros) z *= w0;
        const double k2 = 2.0 * fs;
        auto toZ = [&](C s) { return (1.0 + s / k2) / (1.0 - s / k2); };
        std::vector<C> pz, zz;
        for (const auto& p : poles) pz.push_back(toZ(p));
        for (const auto& z : zeros) zz.push_back(toZ(z));
        for (std::size_t i = nz; i < np; ++i) zz.push_back(C(-1.0, 0.0));  // the zeros at infinity
        // real sections: conjugate pairs together, the real roots two by two
        auto quad = [](std::vector<C>& r, std::vector<std::array<double, 3>>& out) {
            std::vector<C> reals;
            std::vector<C> comp;
            for (const auto& v : r) (std::abs(v.imag()) < 1e-9 ? reals : comp).push_back(v);
            std::sort(comp.begin(), comp.end(), [](C a, C b) { return a.imag() > b.imag(); });
            for (std::size_t i = 0; i < comp.size(); i += 2) out.push_back({1.0, -2.0 * comp[i].real(), std::norm(comp[i])});
            std::sort(reals.begin(), reals.end(), [](C a, C b) { return a.real() < b.real(); });
            for (std::size_t i = 0; i + 1 < reals.size(); i += 2) out.push_back({1.0, -(reals[i].real() + reals[i + 1].real()), reals[i].real() * reals[i + 1].real()});
            if (reals.size() % 2) out.push_back({1.0, -reals.back().real(), 0.0});
        };
        std::vector<std::array<double, 3>> pq, zq;
        quad(pz, pq);
        quad(zz, zq);
        const std::size_t n = std::max(pq.size(), zq.size());
        pq.resize(n, {1.0, 0.0, 0.0});
        zq.resize(n, {1.0, 0.0, 0.0});
        std::vector<Section> next(n);
        for (std::size_t i = 0; i < n; ++i) {
            next[i].b[0] = zq[i][0]; next[i].b[1] = zq[i][1]; next[i].b[2] = zq[i][2];
            next[i].a[0] = pq[i][1]; next[i].a[1] = pq[i][2];
        }
        // the gain that makes the digital response equal the analog one at refHz
        const double wr = 2.0 * 3.14159265358979323846 * refHz;
        const C sr(0.0, wr), zr = toZ(sr), zi = 1.0 / zr;
        C unit = 1.0;
        for (const auto& s : next) unit *= (s.b[0] + s.b[1] * zi + s.b[2] * zi * zi) / (1.0 + s.a[0] * zi + s.a[1] * zi * zi);
        const C target = num.at(sr) / den.at(sr);
        gain_ = std::abs(unit) > 0.0 ? (target / unit).real() : 1.0;
        if (sections_.size() != n) sections_.assign(n, Section{});
        for (std::size_t i = 0; i < n; ++i) {
            for (int k = 0; k < 3; ++k) sections_[i].b[k] = next[i].b[k];
            for (int k = 0; k < 2; ++k) sections_[i].a[k] = next[i].a[k];
        }
    }

    double process(double x) {
        double v = x * gain_;
        for (auto& s : sections_) {  // transposed direct form II
            const double y = s.b[0] * v + s.z1;
            s.z1 = s.b[1] * v - s.a[0] * y + s.z2;
            s.z2 = s.b[2] * v - s.a[1] * y;
            v = y;
        }
        return v;
    }
    void reset() {
        for (auto& s : sections_) s.z1 = s.z2 = 0.0;
    }

private:
    struct Section {
        double b[3] = {1, 0, 0}, a[2] = {0, 0}, z1 = 0, z2 = 0;
    };
    std::vector<Section> sections_;
    double gain_ = 1.0;
};

}  // namespace sas
