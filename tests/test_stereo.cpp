#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <vector>

#include "sas/stereo.h"

using namespace sas;

namespace {

constexpr double kFs = 48000.0;

std::vector<float> noise(std::size_t n, unsigned seed = 5) {
    std::vector<float> v(n);
    unsigned state = seed;
    float lp = 0.0f;
    for (auto& x : v) {
        state = state * 1664525u + 1013904223u;
        lp += 0.35f * ((float(state >> 8) / float(1u << 24) - 0.5f) - lp);  // a guitar-like, band-limited noise
        x = 0.3f * lp * 4.0f;
    }
    return v;
}

double rms(const std::vector<float>& v, std::size_t from) {
    double e = 0;
    for (std::size_t i = from; i < v.size(); ++i) e += double(v[i]) * v[i];
    return std::sqrt(e / double(v.size() - from));
}

double correlation(const std::vector<float>& a, const std::vector<float>& b, std::size_t from) {
    double ab = 0, aa = 0, bb = 0;
    for (std::size_t i = from; i < a.size(); ++i) { ab += double(a[i]) * b[i]; aa += double(a[i]) * a[i]; bb += double(b[i]) * b[i]; }
    return ab / std::sqrt(aa * bb + 1e-30);
}

}  // namespace

TEST_CASE("stereo double: amount 0 leaves both channels exactly as they were") {
    StereoDouble d;
    d.prepare(kFs);
    d.setAmount(0.0, 20.0);
    auto l = noise(48000), r = l;
    const auto l0 = l, r0 = r;
    d.process(l.data(), r.data(), l.size());
    CHECK(l == l0);
    CHECK(r == r0);
}

TEST_CASE("stereo double: the two channels decorrelate, keep their level, and the mono sum stays usable") {
    StereoDouble d;
    d.prepare(kFs);
    d.setAmount(1.0, 20.0);
    const auto in = noise(96000);
    auto l = in, r = in;
    d.process(l.data(), r.data(), l.size());
    const std::size_t from = 24000;
    const double inRms = rms(in, from);
    const double lDb = 20.0 * std::log10(rms(l, from) / inRms), rDb = 20.0 * std::log10(rms(r, from) / inRms);
    std::vector<float> mono(l.size());
    for (std::size_t i = 0; i < mono.size(); ++i) mono[i] = 0.5f * (l[i] + r[i]);
    const double monoDb = 20.0 * std::log10(rms(mono, from) / inRms);
    INFO("left " << lDb << " dB, right " << rDb << " dB, mono sum " << monoDb << " dB, correlation " << correlation(l, r, from));
    CHECK(std::abs(lDb) < 2.5);   // each side keeps the level (the copy is a little duller, so a touch less)
    CHECK(std::abs(rDb) < 2.5);
    CHECK(correlation(l, r, from) < 0.5);  // wide
    CHECK(monoDb > -6.5);                   // and not a hollow comb when summed
}

TEST_CASE("stereo double: a knob move does not click, and a tone keeps its pitch within a few cents") {
    StereoDouble d;
    d.prepare(kFs);
    // A 220 Hz tone through the full double: the modulated delay moves the pitch of the copy by a fraction of a percent.
    d.setAmount(1.0, 20.0);
    std::vector<float> l(96000), r(96000);
    for (std::size_t i = 0; i < l.size(); ++i) l[i] = r[i] = float(0.3 * std::sin(2.0 * 3.14159265358979 * 220.0 * double(i) / kFs));
    d.process(l.data(), r.data(), l.size());
    for (float v : l) REQUIRE(std::isfinite(v));
    // Zero crossings give the frequency of the left channel over the last second.
    int crossings = 0;
    for (std::size_t i = l.size() - 48000; i + 1 < l.size(); ++i)
        if (l[i] <= 0.0f && l[i + 1] > 0.0f) ++crossings;
    CHECK(std::abs(1200.0 * std::log2(double(crossings) / 220.0)) < 8.0);

    // Amount from 1 to 0 in a block: the output has no jump larger than the signal's own step.
    d.setAmount(0.0, 20.0);
    std::vector<float> a(4800), b(4800);
    for (std::size_t i = 0; i < a.size(); ++i) a[i] = b[i] = float(0.3 * std::sin(2.0 * 3.14159265358979 * 220.0 * double(96000 + i) / kFs));
    d.process(a.data(), b.data(), a.size());
    float worst = 0.0f;
    for (std::size_t i = 1; i < a.size(); ++i) worst = std::max(worst, std::abs(a[i] - a[i - 1]));
    CHECK(worst < 0.1f);
}
