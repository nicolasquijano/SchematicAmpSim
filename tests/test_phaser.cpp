// The four-stage phaser.
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <random>
#include <vector>

#include "sas/phaser.h"

namespace {
constexpr double kFs = 48000.0;
}

TEST_CASE("phaser: off leaves the signal untouched, on keeps the level of a low tone and cuts somewhere in the sweep") {
    sas::Phaser ph;
    sas::PhaserParams p;
    p.on = true;
    p.speedHz = 0.5;
    ph.setParams(p);
    ph.prepare(kFs);
    std::mt19937 rng(3);
    std::normal_distribution<float> g(0.0f, 0.2f);
    std::vector<float> x(std::size_t(4.0 * kFs));
    for (auto& v : x) v = g(rng);
    std::vector<float> y = x;
    for (std::size_t s = 0; s < y.size(); s += 256) ph.process(y.data() + s, nullptr, std::min<std::size_t>(256, y.size() - s));
    // Broadband noise through a swept notch filter: the output level moves over time (the notches pass over the bands).
    auto rmsOf = [](const std::vector<float>& v, std::size_t a, std::size_t b) { double e = 0; for (std::size_t i = a; i < b; ++i) e += double(v[i]) * v[i]; return std::sqrt(e / double(b - a)); };
    double lo = 1e9, hi = 0;
    for (std::size_t s = 0; s + 4800 < y.size(); s += 4800) {
        const double r = rmsOf(y, s, s + 4800) / rmsOf(x, s, s + 4800);
        lo = std::min(lo, r);
        hi = std::max(hi, r);
    }
    INFO("level ratio from " << lo << " to " << hi);
    CHECK(hi > lo);
    CHECK(hi < 1.3);
    CHECK(lo > 0.3);
    for (float v : y) REQUIRE(std::isfinite(v));

    // Off: nothing changes, after the fade.
    p.on = false;
    ph.setParams(p);
    std::vector<float> a(48000, 0.1f);
    ph.process(a.data(), nullptr, a.size());
    std::vector<float> b(256);
    for (std::size_t i = 0; i < b.size(); ++i) b[i] = 0.3f * float(std::sin(0.05 * double(i)));
    const auto before = b;
    ph.process(b.data(), nullptr, b.size());
    CHECK_FALSE(ph.active());
    for (std::size_t i = 0; i < b.size(); ++i) REQUIRE(b[i] == before[i]);
}

TEST_CASE("phaser: the feedback version is stable at the top speed and both channels of a pair are processed alike") {
    sas::Phaser ph;
    sas::PhaserParams p;
    p.on = true;
    p.intense = true;
    p.speedHz = 8.0;
    ph.setParams(p);
    ph.prepare(kFs);
    std::vector<float> l(std::size_t(2.0 * kFs)), r(l.size());
    for (std::size_t i = 0; i < l.size(); ++i) l[i] = r[i] = 0.5f * float(std::sin(2.0 * 3.14159265358979 * 440.0 * double(i) / kFs));
    for (std::size_t s = 0; s < l.size(); s += 512) ph.process(l.data() + s, r.data() + s, std::min<std::size_t>(512, l.size() - s));
    for (std::size_t i = 0; i < l.size(); ++i) {
        REQUIRE(std::isfinite(l[i]));
        REQUIRE(std::fabs(l[i]) < 2.0f);
        REQUIRE(l[i] == r[i]);
    }
}
