#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <random>

#include "sas/tables.h"

using namespace sas;

TEST_CASE("triode tables reproduce Koren's formulas") {
    const auto m = TriodeModel::ecc83();
    const TriodeTable t(m);
    std::mt19937 rng(7);
    std::uniform_real_distribution<double> vg(-12.0, 2.0), vp(0.5, 500.0);
    double worstI = 0, worstDg = 0, worstDp = 0;
    for (int i = 0; i < 20000; ++i) {
        const double g = vg(rng), p = vp(rng);
        const auto a = m.plate(g, p), b = t.plate(g, p);
        const double scale = std::max(std::abs(a.ip), 1e-9);
        worstI = std::max(worstI, std::abs(a.ip - b.ip) / scale);
        worstDg = std::max(worstDg, std::abs(a.dvgk - b.dvgk) / std::max(std::abs(a.dvgk), 1e-7));
        worstDp = std::max(worstDp, std::abs(a.dvpk - b.dvpk) / std::max(std::abs(a.dvpk), 1e-7));
    }
    INFO("worst relative error: ip " << worstI << ", d/dvgk " << worstDg << ", d/dvpk " << worstDp);
    CHECK(worstI < 1e-6);
    CHECK(worstDg < 1e-5);
    CHECK(worstDp < 1e-5);

    double worstG = 0;
    for (double g = -3.0; g <= 4.0; g += 0.0137) {
        const auto a = m.grid(g), b = t.grid(g);
        worstG = std::max(worstG, std::abs(a.ig - b.ig) / std::max(std::abs(a.ig), 1e-12));
        worstG = std::max(worstG, std::abs(a.dvgk - b.dvgk) / std::max(std::abs(a.dvgk), 1e-10));
    }
    CHECK(worstG < 1e-5);
}

TEST_CASE("table derivatives are consistent with the tables' own values") {
    const TriodeTable t(TriodeModel::ecc83());
    for (const auto& [g, p] : {std::pair{-2.0, 250.0}, {-0.4, 120.0}, {0.3, 80.0}, {-6.0, 300.0}, {0.0, 10.0}}) {
        const double h = 1e-5;
        const auto c = t.plate(g, p);
        const double ng = (t.plate(g + h, p).ip - t.plate(g - h, p).ip) / (2 * h);
        const double np = (t.plate(g, p + h).ip - t.plate(g, p - h).ip) / (2 * h);
        CHECK(std::abs(c.dvgk - ng) <= 1e-8 + 1e-5 * std::abs(ng));
        CHECK(std::abs(c.dvpk - np) <= 1e-8 + 1e-5 * std::abs(np));
    }
}

TEST_CASE("table edge cases match the formulas") {
    const auto m = TriodeModel::ecc83();
    const TriodeTable t(m);
    CHECK(t.plate(-1.0, 0.0).ip == 0.0);
    CHECK(t.plate(-1.0, -5.0).ip == 0.0);
    CHECK(t.plate(-80.0, 200.0).ip < 1e-12);  // far below cutoff
    CHECK(t.grid(-5.0).ig == 0.0);            // the grid does not conduct there
    const auto hi = t.plate(5.0, 800.0), ref = m.plate(5.0, 800.0);  // outside the A table's range
    CHECK(std::abs(hi.ip - ref.ip) / ref.ip < 1e-6);
}

TEST_CASE("pentode tables reproduce Koren's formulas and their derivatives") {
    using namespace sas;
    const PentodeModel models[] = {PentodeModel::el34(), PentodeModel::el6l6gcFit(), PentodeModel::el6550Fit(), PentodeModel::el84(), PentodeModel::el6v6()};
    for (const auto& m : models) {
        const PentodeTable t(m);
        double worst = 0.0, scale = 0.0;
        for (double vg1 : {-90.0, -60.0, -40.0, -25.0, -12.0, -3.0, 0.0, 2.0, 8.0})
            for (double vpk : {-5.0, 0.5, 5.0, 40.0, 150.0, 300.0, 480.0, 700.0})
                for (double vg2 : {0.0, 0.5, 20.0, 150.0, 250.0, 400.0, 520.0, 650.0}) {
                    const auto a = m.eval(vg1, vpk, vg2), b = t.eval(vg1, vpk, vg2);
                    for (int i = 0; i < 3; ++i) {
                        scale = std::max(scale, std::abs(a.cur[std::size_t(i)]));
                        worst = std::max(worst, std::abs(a.cur[std::size_t(i)] - b.cur[std::size_t(i)]));
                        for (int j = 0; j < 3; ++j) worst = std::max(worst, std::abs(a.d[std::size_t(i)][std::size_t(j)] - b.d[std::size_t(i)][std::size_t(j)]) * 1e-2);
                    }
                }
        INFO("worst " << worst << " of scale " << scale);
        CHECK(worst < 1e-7 * scale + 1e-12);
    }
}
