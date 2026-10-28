// The bass and treble harmonic exciter.
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <vector>

#include "sas/exciter.h"

namespace {

constexpr double kFs = 48000.0, kPi = 3.14159265358979323846;

// Signal power at `freq` over the second half of the signal (Goertzel).
double powerAt(const std::vector<float>& s, double freq) {
    const std::size_t from = s.size() / 2;
    const double w = 2.0 * kPi * freq / kFs, coeff = 2.0 * std::cos(w);
    double s1 = 0.0, s2 = 0.0;
    for (std::size_t i = from; i < s.size(); ++i) {
        const double s0 = double(s[i]) + coeff * s1 - s2;
        s2 = s1;
        s1 = s0;
    }
    return (s1 * s1 + s2 * s2 - coeff * s1 * s2) / double(s.size() - from);
}

std::vector<float> run(double low, double high, double hz, double amp) {
    sas::HarmonicExciter ex;
    ex.prepare(kFs);
    ex.setAmounts(low, high);
    const std::size_t n = std::size_t(kFs);
    std::vector<float> l(n), r(n);
    for (std::size_t i = 0; i < n; ++i) l[i] = r[i] = float(amp * std::sin(2.0 * kPi * hz * double(i) / kFs));
    for (std::size_t s = 0; s < n; s += 256) ex.process(l.data() + s, r.data() + s, std::min<std::size_t>(256, n - s));
    return l;
}

}  // namespace

TEST_CASE("exciter: at 0 it is bit-exact bypass") {
    const auto off = run(0.0, 0.0, 440.0, 0.3);
    for (std::size_t i = 0; i < off.size(); ++i) REQUIRE(off[i] == float(0.3 * std::sin(2.0 * kPi * 440.0 * double(i) / kFs)));
}

TEST_CASE("exciter: the bass side adds harmonics above a low note, the treble side above a high one") {
    const auto flatBass = run(0.0, 0.0, 80.0, 0.2), bass = run(1.0, 0.0, 80.0, 0.2);
    // 80 Hz: the third harmonic (240 Hz) and the fifth (400 Hz) appear.
    CHECK(powerAt(bass, 240.0) > 30.0 * powerAt(flatBass, 240.0) + 1e-12);
    CHECK(powerAt(bass, 400.0) > 10.0 * powerAt(flatBass, 400.0) + 1e-12);
    const auto flatHigh = run(0.0, 0.0, 5000.0, 0.2), high = run(0.0, 1.0, 5000.0, 0.2);
    CHECK(powerAt(high, 10000.0) > 30.0 * powerAt(flatHigh, 10000.0) + 1e-12);
    // The bass side does not touch a treble note, nor the other way round (within the shelves' small tilt).
    const auto bassOnHigh = run(1.0, 0.0, 5000.0, 0.2);
    CHECK(std::fabs(10.0 * std::log10(powerAt(bassOnHigh, 5000.0) / powerAt(flatHigh, 5000.0))) < 1.0);
}

TEST_CASE("exciter: the amount of harmonic does not depend on the playing level, and the output stays bounded") {
    auto ratio = [&](double amp) {
        const auto out = run(1.0, 0.0, 80.0, amp);
        return powerAt(out, 240.0) / powerAt(out, 80.0);
    };
    const double quiet = ratio(0.02), loud = ratio(0.5);
    INFO("harmonic / fundamental: quiet " << quiet << ", loud " << loud);
    CHECK(loud / quiet > 0.5);
    CHECK(loud / quiet < 2.0);
    for (float v : run(1.0, 1.0, 110.0, 0.9)) {
        REQUIRE(std::isfinite(v));
        REQUIRE(std::fabs(v) < 3.0f);
    }
}
