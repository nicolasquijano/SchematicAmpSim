#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <cmath>

#include "sas/devices.h"

using namespace sas;

static bool close(double d, double n, double abs, double rel) { return std::abs(d - n) <= abs + rel * std::abs(n); }

TEST_CASE("triode derivatives match finite differences") {
    const auto m = TriodeModel::ecc83();
    const double pts[][2] = {{-2, 250}, {-0.5, 120}, {0.3, 80}, {-4, 300}, {0, 10}};
    for (auto& p : pts) {
        const auto r = m.plate(p[0], p[1]);
        const double h = 1e-5;
        const double ng = (m.plate(p[0] + h, p[1]).ip - m.plate(p[0] - h, p[1]).ip) / (2 * h);
        const double np = (m.plate(p[0], p[1] + h).ip - m.plate(p[0], p[1] - h).ip) / (2 * h);
        CHECK(close(r.dvgk, ng, 1e-6, 1e-4));
        CHECK(close(r.dvpk, np, 1e-6, 1e-4));
    }
    for (double vg : {-1.0, -0.2, 0.0, 0.3, 1.0}) {
        const double h = 1e-6;
        const double n = (m.grid(vg + h).ig - m.grid(vg - h).ig) / (2 * h);
        CHECK(close(m.grid(vg).dvgk, n, 1e-9, 1e-4));
    }
    CHECK(m.grid(-1.5).ig < 1e-9);
    CHECK(m.grid(1.0).ig > 1e-5);
}

TEST_CASE("pentode jacobian matches finite differences") {
    const auto m = PentodeModel::el34();
    const double pts[][3] = {{-40, 450, 440}, {-20, 200, 440}, {-5, 80, 420}, {0.5, 300, 400}};
    for (auto& p : pts) {
        const auto d = m.eval(p[0], p[1], p[2]).d;
        const double h = 1e-5;
        auto at = [&](double a, double b, double c) { return m.eval(a, b, c).cur; };
        for (int row = 0; row < 3; ++row) {
            const double n[3] = {
                (at(p[0] + h, p[1], p[2])[row] - at(p[0] - h, p[1], p[2])[row]) / (2 * h),
                (at(p[0], p[1] + h, p[2])[row] - at(p[0], p[1] - h, p[2])[row]) / (2 * h),
                (at(p[0], p[1], p[2] + h)[row] - at(p[0], p[1], p[2] - h)[row]) / (2 * h)};
            for (int col = 0; col < 3; ++col) CHECK(close(d[row][col], n[col], 1e-7, 2e-4));
        }
    }
    const auto c = m.eval(-38, 450, 440).cur;
    const double ip = c[1] * 1e3, ig2 = c[2] * 1e3;
    CHECK((ip > 10 && ip < 80));
    CHECK((ig2 > 0.5 && ig2 < 0.3 * ip));
}

TEST_CASE("diode, BJT and op-amp stage") {
    const auto d = DiodeModel::rectifierBridge();
    CHECK(d.eval(1.5).i > 0.01);
    CHECK(std::abs(d.eval(-100).i) < 1e-7);
    const double g = d.eval(0.9).g;
    CHECK(std::abs((d.eval(0.9 + 1e-6).i - d.eval(0.9).i) / 1e-6 - g) < 1e-3 * g);

    const auto b = BjtModel::c1815();
    for (double v : {0.3, 0.55, 0.62, 0.7, 0.9}) {
        const double h = 1e-7;
        const double n = (b.baseCurrent(v + h).ib - b.baseCurrent(v - h).ib) / (2 * h);
        CHECK(close(b.baseCurrent(v).g, n, 1e-12, 1e-4));
    }
    const auto o = OpampModel::rc4558();
    for (double x : {-4.0, -1.0, 0.0, 0.5, 3.0}) {
        const double h = 1e-6;
        const double n = (o.stage(x + h).i - o.stage(x - h).i) / (2 * h);
        CHECK(close(o.stage(x).g, n, 1e-9, 1e-4));
    }
    // 0.39 mA in an emitter follower puts Vbe near 0.6 V.
    const double ib = 0.39e-3 / (b.beta + 1.0);
    double v = 0.6;
    for (int i = 0; i < 40; ++i) {
        const auto e = b.baseCurrent(v);
        v -= (e.ib - ib) / e.g;
    }
    CHECK((v > 0.55 && v < 0.68));
}
