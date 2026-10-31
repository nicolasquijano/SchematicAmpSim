// The spectrum display behind the EQ.
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <vector>

#include "sas/spectrum.h"

namespace {
constexpr double kFs = 48000.0, kPi = 3.14159265358979323846;

int peakPoint(const sas::SpectrumAnalyzer& a) {
    int best = 0;
    for (int i = 1; i < sas::SpectrumAnalyzer::kPoints; ++i)
        if (a.db()[std::size_t(i)] > a.db()[std::size_t(best)]) best = i;
    return best;
}

std::vector<float> tone(double hz, double amp, int n = sas::SpectrumAnalyzer::kFft) {
    std::vector<float> v(static_cast<std::size_t>(n));
    for (int i = 0; i < n; ++i) v[std::size_t(i)] = float(amp * std::sin(2.0 * kPi * hz * double(i) / kFs));
    return v;
}
}  // namespace

TEST_CASE("spectrum: a sine shows up at its frequency, about at its level, and silence stays at the floor") {
    for (double hz : {110.0, 440.0, 1000.0, 5000.0, 12000.0}) {
        sas::SpectrumAnalyzer a;
        a.setSampleRate(kFs);
        const auto x = tone(hz, 0.5);
        for (int i = 0; i < 12; ++i) a.analyze(x.data());  // let the smoothing settle
        const int p = peakPoint(a);
        INFO("tone " << hz << " Hz, peak at " << sas::SpectrumAnalyzer::freqOf(p) << " Hz");
        CHECK(std::fabs(std::log2(sas::SpectrumAnalyzer::freqOf(p) / hz)) < 0.12);  // within a sixth of an octave
        // The level, undone from the tilt: 0.5 amplitude = -6 dBFS, within a few dB (the window and the band edges).
        const float tilt = float(4.5 * std::log2(sas::SpectrumAnalyzer::freqOf(p) / 1000.0));
        CHECK(std::fabs(a.db()[std::size_t(p)] - tilt - (-6.0f)) < 4.0f);
    }
    sas::SpectrumAnalyzer a;
    std::vector<float> silence(sas::SpectrumAnalyzer::kFft, 0.0f);
    for (int i = 0; i < 12; ++i) a.analyze(silence.data());
    for (float d : a.db()) CHECK(d < -100.0f);
}

TEST_CASE("spectrum: two tones show two peaks, and the display falls slowly after the sound stops") {
    sas::SpectrumAnalyzer a;
    auto x = tone(200.0, 0.3), y = tone(3000.0, 0.3);
    for (std::size_t i = 0; i < x.size(); ++i) x[i] += y[i];
    for (int i = 0; i < 12; ++i) a.analyze(x.data());
    auto at = [&](double hz) { int best = 0; double d = 1e9; for (int i = 0; i < sas::SpectrumAnalyzer::kPoints; ++i) { const double e = std::fabs(std::log2(sas::SpectrumAnalyzer::freqOf(i) / hz)); if (e < d) { d = e; best = i; } } return a.db()[std::size_t(best)]; };
    CHECK(at(200.0) > at(600.0) + 15.0);
    CHECK(at(3000.0) > at(1000.0) + 15.0);
    const float before = at(200.0);
    std::vector<float> silence(sas::SpectrumAnalyzer::kFft, 0.0f);
    a.analyze(silence.data());
    CHECK(at(200.0) < before);          // it falls...
    CHECK(at(200.0) > before - 40.0f);  // ...but not all at once
}

TEST_CASE("spectrum feed: keeps the latest samples in order, averaging the two channels") {
    sas::SpectrumFeed feed;
    std::vector<float> l(20000), r(20000);
    for (std::size_t i = 0; i < l.size(); ++i) { l[i] = float(i); r[i] = float(i) + 2.0f; }
    for (std::size_t s = 0; s < l.size(); s += 512) feed.push(l.data() + s, r.data() + s, std::min<std::size_t>(512, l.size() - s));
    std::vector<float> got(4096);
    feed.latest(got.data(), got.size());
    for (std::size_t i = 0; i < got.size(); ++i) REQUIRE(got[i] == float(l.size() - got.size() + i) + 1.0f);
}
