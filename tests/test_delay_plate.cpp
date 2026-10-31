// The end-of-chain delay and plate reverb.
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <random>
#include <vector>

#include "sas/delay.h"
#include "sas/plate.h"

namespace {

constexpr double kFs = 48000.0;

template <class Fx>
void run(Fx& fx, std::vector<float>& l, std::vector<float>& r, std::size_t block = 256) {
    for (std::size_t s = 0; s < l.size(); s += block) fx.process(l.data() + s, r.data() + s, std::min(block, l.size() - s));
}

double rms(const std::vector<float>& v, std::size_t a, std::size_t b) {
    double e = 0;
    for (std::size_t i = a; i < b; ++i) e += double(v[i]) * v[i];
    return std::sqrt(e / double(b - a));
}

}  // namespace

TEST_CASE("delay: one repeat arrives after the set time and each one is quieter") {
    sas::StereoDelay d;
    sas::DelayParams p;
    p.on = true;
    p.timeMs = 100.0;
    p.feedback = 0.5;
    p.mix = 1.0;
    p.toneHz = 12000.0;
    d.setParams(p);
    d.prepare(kFs);
    std::vector<float> l(static_cast<std::size_t>(kFs)), r(l.size());
    l[10] = r[10] = 1.0f;
    run(d, l, r);
    const std::size_t t1 = 10 + std::size_t(0.1 * kFs), t2 = 10 + std::size_t(0.2 * kFs);
    auto peak = [&](std::size_t c) { float m = 0; for (std::size_t i = c - 40; i < c + 40; ++i) m = std::max(m, std::fabs(l[i])); return m; };
    CHECK(peak(t1) > 0.3f);
    CHECK(peak(t2) > 0.05f);
    CHECK(peak(t2) < peak(t1));
    CHECK(rms(l, 20, t1 - 100) < 1e-3);  // silence between the dry hit and the first repeat
}

TEST_CASE("delay: ping-pong sends the first repeat left and the second right") {
    sas::StereoDelay d;
    sas::DelayParams p;
    p.on = true;
    p.timeMs = 100.0;
    p.feedback = 0.6;
    p.mix = 1.0;
    p.toneHz = 12000.0;
    p.pingPong = true;
    d.setParams(p);
    d.prepare(kFs);
    std::vector<float> l(static_cast<std::size_t>(kFs)), r(l.size());
    l[10] = r[10] = 1.0f;
    run(d, l, r);
    const std::size_t t1 = 10 + std::size_t(0.1 * kFs), t2 = 10 + std::size_t(0.2 * kFs);
    auto peak = [&](const std::vector<float>& v, std::size_t c) { float m = 0; for (std::size_t i = c - 40; i < c + 40; ++i) m = std::max(m, std::fabs(v[i])); return m; };
    CHECK(peak(l, t1) > 0.2f);
    CHECK(peak(r, t1) < 0.02f);
    CHECK(peak(r, t2) > 0.1f);
    CHECK(peak(l, t2) < 0.02f);
}

TEST_CASE("delay: off lets the repeats ring out, then it is silent and leaves the signal alone") {
    sas::StereoDelay d;
    sas::DelayParams p;
    p.on = true;
    p.timeMs = 200.0;
    p.feedback = 0.5;
    p.mix = 1.0;
    d.setParams(p);
    d.prepare(kFs);
    std::vector<float> l(static_cast<std::size_t>(kFs * 0.5)), r(l.size());
    l[5] = r[5] = 1.0f;
    run(d, l, r);
    p.on = false;
    d.setParams(p);
    std::vector<float> l2(static_cast<std::size_t>(kFs * 0.6)), r2(l2.size());
    run(d, l2, r2);
    CHECK(rms(l2, 0, l2.size()) > 1e-4);  // the repeat already in the line still plays
    std::vector<float> l3(static_cast<std::size_t>(kFs * 12.0)), r3(l3.size());
    run(d, l3, r3);
    CHECK_FALSE(d.active());
    std::vector<float> x(512, 0.25f), y(512, 0.25f);
    d.process(x.data(), y.data(), x.size());
    CHECK(x[100] == 0.25f);
}

TEST_CASE("delay: stays finite with feedback at the top and a moving time") {
    sas::StereoDelay d;
    sas::DelayParams p;
    p.on = true;
    p.feedback = 0.92;
    p.mix = 1.0;
    p.analog = true;
    d.setParams(p);
    d.prepare(kFs);
    std::mt19937 rng(1);
    std::uniform_real_distribution<float> u(-0.5f, 0.5f);
    std::vector<float> l(256), r(256);
    for (int b = 0; b < 600; ++b) {
        for (auto& x : l) x = u(rng);
        r = l;
        p.timeMs = 30.0 + 1400.0 * std::fabs(std::sin(double(b) * 0.05));
        d.setParams(p);
        d.process(l.data(), r.data(), l.size());
        for (float x : l) REQUIRE(std::isfinite(x));
        for (float x : l) REQUIRE(std::fabs(x) < 20.0f);
    }
}

TEST_CASE("plate: the tail decays at about the set time and is about as loud as the dry signal at full mix") {
    sas::PlateReverb rv;
    sas::PlateParams p;
    p.on = true;
    p.decaySec = 1.5;
    p.mix = 1.0;
    p.preMs = 0.0;
    p.lowCutHz = 20.0;
    rv.setParams(p);
    rv.prepare(kFs);
    std::mt19937 rng(7);
    std::normal_distribution<float> g(0.0f, 0.1f);
    const std::size_t noiseLen = std::size_t(2.0 * kFs);
    std::vector<float> l(static_cast<std::size_t>(8.0 * kFs)), r(l.size());
    for (std::size_t i = 0; i < noiseLen; ++i) l[i] = r[i] = g(rng);
    const std::vector<float> dry = l;
    run(rv, l, r);
    // Steady-state level of the wet part against the dry noise.
    std::vector<float> wet(noiseLen);
    for (std::size_t i = 0; i < noiseLen; ++i) wet[i] = l[i] - dry[i];
    const double ratioDb = 20.0 * std::log10(rms(wet, noiseLen / 2, noiseLen) / rms(dry, noiseLen / 2, noiseLen));
    INFO("wet against dry " << ratioDb << " dB");
    CHECK(ratioDb > -9.0);
    CHECK(ratioDb < 3.0);
    // After the noise stops the tail falls: 60 dB per decay time, so 1.5 s later it is far below.
    const double a = rms(l, noiseLen + std::size_t(0.1 * kFs), noiseLen + std::size_t(0.3 * kFs));
    const double b = rms(l, noiseLen + std::size_t(1.6 * kFs), noiseLen + std::size_t(1.8 * kFs));
    INFO("tail " << 20.0 * std::log10(b / a) << " dB after 1.5 s");
    CHECK(20.0 * std::log10(b / a) < -35.0);
    CHECK(20.0 * std::log10(b / a) > -90.0);
}

TEST_CASE("plate: both rooms are stable and finite, the hall rings longer, off lets the tail go and leaves the signal alone") {
    for (bool hall : {false, true}) {
        sas::PlateReverb rv;
        sas::PlateParams p;
        p.on = true;
        p.hall = hall;
        p.decaySec = 8.0;
        p.mix = 1.0;
        rv.setParams(p);
        rv.prepare(kFs);
        std::vector<float> l(static_cast<std::size_t>(kFs * 3.0)), r(l.size());
        l[3] = r[3] = 1.0f;
        run(rv, l, r);
        for (float x : l) REQUIRE(std::isfinite(x));
        for (float x : r) REQUIRE(std::fabs(x) < 5.0f);
        CHECK(rms(l, std::size_t(kFs * 2.0), std::size_t(kFs * 3.0)) > 1e-5);
        p.on = false;
        rv.setParams(p);
        std::vector<float> l2(static_cast<std::size_t>(kFs * 14.0)), r2(l2.size());
        run(rv, l2, r2);
        CHECK_FALSE(rv.active());
        std::vector<float> x(256, 0.5f), y(256, 0.5f);
        rv.process(x.data(), y.data(), x.size());
        CHECK(x[10] == 0.5f);
    }
}
