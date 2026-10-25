#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <vector>

#include "sas/gate.h"

namespace {
double rmsOf(const std::vector<float>& v, std::size_t from, std::size_t to) {
    double s = 0;
    for (std::size_t i = from; i < to; ++i) s += double(v[i]) * v[i];
    return std::sqrt(s / double(to - from));
}
}  // namespace

TEST_CASE("noise gate: silences what is under the threshold, passes what is over, and can be switched off") {
    constexpr double fs = 48000.0;
    auto tone = [&](double amp, std::size_t n) {
        std::vector<float> v(n);
        for (std::size_t i = 0; i < n; ++i) v[i] = float(amp * std::sin(2.0 * 3.14159265358979 * 220.0 * double(i) / fs));
        return v;
    };
    sas::NoiseGate g;
    g.prepare(fs);
    g.setParams(true, -50.0, 80.0);  // threshold 0.0032

    auto quiet = tone(0.001, 48000);  // -60 dB hiss-like level
    g.process(quiet.data(), quiet.size());
    CHECK(rmsOf(quiet, 24000, 48000) < 0.0005 * 0.707 * 0.2);  // at least ~14 dB down after the release

    auto loud = tone(0.1, 24000);
    g.process(loud.data(), loud.size());
    CHECK(rmsOf(loud, 12000, 24000) > 0.99 * 0.1 * 0.7071);  // opened and untouched

    // A decay under the threshold closes it again.
    auto tail = tone(0.001, 48000);
    g.process(tail.data(), tail.size());
    CHECK(g.gain() < 0.05);

    g.setParams(false, -50.0, 80.0);
    auto off = tone(0.001, 4800);
    g.process(off.data(), off.size());
    CHECK(rmsOf(off, 2400, 4800) > 0.95 * 0.001 * 0.7071);
}
