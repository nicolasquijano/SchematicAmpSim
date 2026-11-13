#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <vector>

#include "sas/rack_modules.h"
#include "sas/ten_band_eq.h"

using namespace sas;

namespace {
constexpr double kFs = 48000.0, kPi = 3.14159265358979323846;

// rms gain in dB of the pedal for a sine at `hz`
double gainDb(const TenBandEqParams& p, double hz) {
    TenBandEq eq;
    eq.prepare(kFs);
    eq.setParams(p);
    std::vector<float> x(std::size_t(kFs * 0.6));
    for (std::size_t i = 0; i < x.size(); ++i) x[i] = float(0.05 * std::sin(2 * kPi * hz * double(i) / kFs));
    const auto in = x;
    eq.process(x.data(), nullptr, x.size());
    double a = 0, b = 0;
    for (std::size_t i = x.size() / 2; i < x.size(); ++i) { a += double(in[i]) * in[i]; b += double(x[i]) * x[i]; }
    return 10.0 * std::log10(b / a);
}
}  // namespace

TEST_CASE("ten-band EQ: flat is transparent, a slider moves its own octave by its amount, gain and volume add", "[rack]") {
    TenBandEqParams p;
    p.on = true;
    for (double hz : {60.0, 500.0, 5000.0}) CHECK(std::abs(gainDb(p, hz)) < 0.05);
    for (int b = 0; b < TenBandEqParams::kBands; ++b) {
        TenBandEqParams q = p;
        q.bandsDb[std::size_t(b)] = 9.0;
        const double hz = TenBandEq::kHz[b];
        CHECK(gainDb(q, hz) == Catch::Approx(9.0).margin(0.5));        // at the band's own frequency
        CHECK(gainDb(q, hz * 4.0 < 0.4 * kFs ? hz * 4.0 : hz / 4.0) < 2.0);  // two octaves away it has almost gone
        q.bandsDb[std::size_t(b)] = -9.0;
        CHECK(gainDb(q, hz) == Catch::Approx(-9.0).margin(0.5));
    }
    TenBandEqParams g = p;
    g.gainDb = 6.0;
    g.volumeDb = -2.0;
    CHECK(gainDb(g, 1000.0) == Catch::Approx(4.0).margin(0.1));
}

TEST_CASE("ten-band EQ module: off passes the signal untouched, on works on a stereo pair like on one channel", "[rack]") {
    TenBandEqModule m;
    m.prepare(kFs, 256);
    CHECK_FALSE(m.active());
    TenBandEqParams p;
    p.on = true;
    p.bandsDb[5] = 8.0;
    p.gainDb = 3.0;
    m.setParams(p);
    CHECK(m.active());
    CHECK(m.channels() == Channels::Flexible);
    std::vector<float> l(4800), r(4800), mono(4800);
    for (std::size_t i = 0; i < l.size(); ++i) l[i] = r[i] = mono[i] = float(0.05 * std::sin(2 * kPi * 1000.0 * double(i) / kFs));
    m.process(l.data(), r.data(), l.size());
    TenBandEqModule m2;
    m2.prepare(kFs, 256);
    m2.setParams(p);
    m2.process(mono.data(), nullptr, mono.size());
    for (std::size_t i = 0; i < l.size(); ++i) {
        REQUIRE(std::isfinite(l[i]));
        CHECK(l[i] == r[i]);
        CHECK(l[i] == mono[i]);
    }
    p.on = false;
    m.setParams(p);
    for (int k = 0; k < 40; ++k) m.process(l.data(), r.data(), l.size());  // the 20 ms fade ends
    CHECK_FALSE(m.active());
}
