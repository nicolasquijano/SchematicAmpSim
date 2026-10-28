// The post equaliser.
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <vector>

#include "sas/eq.h"

namespace {

constexpr double kFs = 48000.0, kPi = 3.14159265358979323846;

// Gain in dB of the EQ for a sine at `hz` (rms of the second half, in against out).
double measuredDb(sas::ParametricEq& eq, double hz) {
    eq.reset();
    const std::size_t n = std::size_t(0.5 * kFs), block = 256;
    std::vector<float> l(n), r(n);
    for (std::size_t i = 0; i < n; ++i) l[i] = r[i] = 0.3f * float(std::sin(2.0 * kPi * hz * double(i) / kFs));
    const std::vector<float> in = l;
    for (std::size_t s = 0; s < n; s += block) eq.process(l.data() + s, r.data() + s, std::min(block, n - s));
    double a = 0, b = 0;
    for (std::size_t i = n / 2; i < n; ++i) { a += double(in[i]) * in[i]; b += double(l[i]) * l[i]; }
    return 10.0 * std::log10(b / a);
}

sas::ParametricEq make(const sas::EqParams& p) {
    sas::ParametricEq eq;
    eq.setParams(p);
    eq.prepare(kFs);
    return eq;
}

}  // namespace

TEST_CASE("eq: a flat or switched-off EQ is bit-exact and does no work") {
    sas::EqParams p;
    p.on = true;  // on, but every gain is 0 and both filters are at their ends
    auto eq = make(p);
    CHECK(!eq.active());
    std::vector<float> l(512), r(512);
    for (std::size_t i = 0; i < l.size(); ++i) l[i] = r[i] = float(std::sin(0.1 * double(i)));
    const auto copy = l;
    eq.process(l.data(), r.data(), l.size());
    CHECK(l == copy);
    p.band[2].gainDb = 6.0;
    p.on = false;  // a boost, but the EQ is off
    eq.setParams(p);
    CHECK(!eq.active());
}

TEST_CASE("eq: a bell gives its gain at its centre and little far away, shelves give theirs at the extremes") {
    sas::EqParams p;
    p.on = true;
    p.band[2] = {1000.0, 9.0, 1.4};
    auto eq = make(p);
    CHECK(std::fabs(measuredDb(eq, 1000.0) - 9.0) < 0.3);
    CHECK(std::fabs(measuredDb(eq, 100.0)) < 0.5);
    CHECK(std::fabs(measuredDb(eq, 10000.0)) < 0.7);

    sas::EqParams s;
    s.on = true;
    s.band[0] = {200.0, 6.0, 0.7};
    s.band[4] = {4000.0, -8.0, 0.7};
    auto eq2 = make(s);
    CHECK(std::fabs(measuredDb(eq2, 30.0) - 6.0) < 0.5);       // the low shelf's plateau
    CHECK(std::fabs(measuredDb(eq2, 16000.0) + 8.0) < 0.7);    // the high shelf's
    CHECK(std::fabs(measuredDb(eq2, 1000.0)) < 2.0);           // the middle stays
}

TEST_CASE("eq: the high-pass and the low-pass cut where they should") {
    sas::EqParams p;
    p.on = true;
    p.hpfHz = 200.0;
    p.lpfHz = 5000.0;
    auto eq = make(p);
    CHECK(measuredDb(eq, 50.0) < -20.0);                      // two octaves under a 12 dB per octave corner
    CHECK(std::fabs(measuredDb(eq, 1000.0)) < 0.5);
    CHECK(measuredDb(eq, 15000.0) < -15.0);
    CHECK(std::fabs(measuredDb(eq, 200.0) + 3.0) < 1.0);      // -3 dB at the corner
}

TEST_CASE("eq: the drawn response matches what the filter does, and both channels get the same") {
    sas::EqParams p;
    p.on = true;
    p.hpfHz = 80.0;
    p.band[1] = {300.0, -5.0, 1.2};
    p.band[3] = {3000.0, 7.0, 2.0};
    auto eq = make(p);
    for (double hz : {60.0, 300.0, 1000.0, 3000.0, 8000.0}) {
        INFO(hz << " Hz");
        CHECK(std::fabs(eq.responseDb(hz) - measuredDb(eq, hz)) < 0.4);
    }
    std::vector<float> l(2048), r(2048);
    for (std::size_t i = 0; i < l.size(); ++i) l[i] = r[i] = float(std::sin(0.2 * double(i)));
    eq.reset();
    eq.process(l.data(), r.data(), l.size());
    CHECK(l == r);
}

TEST_CASE("eq: moving a parameter glides and stays finite and bounded") {
    sas::EqParams p;
    p.on = true;
    auto eq = make(p);
    std::vector<float> l(256), r(256);
    float worst = 0.0f;
    for (int blk = 0; blk < 400; ++blk) {
        p.band[2].gainDb = blk < 200 ? 12.0 : -12.0;
        p.band[2].freq = blk % 2 ? 500.0 : 4000.0;
        eq.setParams(p);
        for (std::size_t i = 0; i < l.size(); ++i) l[i] = r[i] = 0.3f * float(std::sin(0.13 * double(i + std::size_t(blk) * 256)));
        eq.process(l.data(), r.data(), l.size());
        for (float v : l) { REQUIRE(std::isfinite(v)); worst = std::max(worst, std::fabs(v)); }
    }
    CHECK(worst < 3.0f);
}
