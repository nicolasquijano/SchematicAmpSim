// The two-mode chorus.
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <vector>

#include "sas/chorus.h"

namespace {
constexpr double kFs = 48000.0, kPi = 3.14159265358979323846;

std::vector<float> tone(double hz, std::size_t n, double amp = 0.4) {
    std::vector<float> v(n);
    for (std::size_t i = 0; i < n; ++i) v[i] = float(amp * std::sin(2.0 * kPi * hz * double(i) / kFs));
    return v;
}

double diff(const std::vector<float>& a, const std::vector<float>& b, std::size_t from) {
    double d = 0;
    for (std::size_t i = from; i < a.size(); ++i) d += std::fabs(double(a[i]) - double(b[i]));
    return d;
}

void run(sas::Chorus& c, std::vector<float>& l, std::vector<float>* r) {
    for (std::size_t s = 0; s < l.size(); s += 256) {
        const std::size_t n = std::min<std::size_t>(256, l.size() - s);
        c.process(l.data() + s, r ? r->data() + s : nullptr, n);
    }
}
}  // namespace

TEST_CASE("chorus: off leaves the signal alone; CE-2 on changes it and stays bounded") {
    sas::Chorus c;
    sas::ChorusParams p;
    p.on = false;
    c.setParams(p);
    c.prepare(kFs);
    auto x = tone(330.0, 48000), y = x;
    run(c, y, nullptr);
    for (std::size_t i = 0; i < x.size(); ++i) REQUIRE(y[i] == x[i]);
    p.on = true;
    p.rateHz = 2.0;
    p.depth = 0.8;
    c.setParams(p);
    y = x;
    run(c, y, nullptr);
    CHECK(diff(x, y, 5000) > 50.0);
    for (float v : y) {
        REQUIRE(std::isfinite(v));
        REQUIRE(std::fabs(v) < 1.0f);
    }
}

TEST_CASE("chorus: depth and rate change what the CE-2 does") {
    auto render = [](double rate, double depth) {
        sas::Chorus c;
        sas::ChorusParams p;
        p.on = true;
        p.rateHz = rate;
        p.depth = depth;
        c.setParams(p);
        c.prepare(kFs);
        auto y = tone(1000.0, 96000);
        run(c, y, nullptr);
        return y;
    };
    const auto base = render(1.0, 0.5);
    CHECK(diff(base, render(1.0, 0.05), 10000) > 100.0);
    CHECK(diff(base, render(4.0, 0.5), 10000) > 100.0);
}

TEST_CASE("chorus: Juno gives two different ears from a mono input, and its three buttons differ") {
    std::vector<std::vector<float>> lefts;
    for (int button = 0; button < 3; ++button) {
        sas::Chorus c;
        sas::ChorusParams p;
        p.on = true;
        p.juno = true;
        p.junoButton = button;
        c.setParams(p);
        c.prepare(kFs);
        auto l = tone(700.0, 96000), r = l;
        run(c, l, &r);
        CHECK(diff(l, r, 10000) > 100.0);  // the two voices sweep in opposite directions
        for (float v : l) REQUIRE(std::isfinite(v));
        lefts.push_back(l);
    }
    CHECK(diff(lefts[0], lefts[1], 10000) > 50.0);
    CHECK(diff(lefts[1], lefts[2], 10000) > 50.0);
    // On one channel (before the amp) it still works with the first voice.
    sas::Chorus c;
    sas::ChorusParams p;
    p.on = true;
    p.juno = true;
    c.setParams(p);
    c.prepare(kFs);
    auto x = tone(700.0, 48000), y = x;
    run(c, y, nullptr);
    CHECK(diff(x, y, 10000) > 50.0);
}
