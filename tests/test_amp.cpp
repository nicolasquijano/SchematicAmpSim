#include <catch2/catch_test_macros.hpp>
#include <cmath>

#include "aes/amp2203.h"
#include "aes/ts808_fast.h"
#include "golden_io.h"

using namespace aes;

namespace {

Controls controlsOf(const golden::Case& c) {
    return {c.num("volume"), c.num("bass"), c.num("middle"), c.num("treble"), c.num("master")};
}

// Smooth cases must agree sample for sample; saturated ones are chaotic enough (continuation
// fallback amplifies last-bit differences) that level, band energy and THD are compared instead.
struct Bands { double low = 0, mid = 0, high = 0, total = 0; };

Bands bands(const std::vector<double>& y, double fs) {
    // Goertzel-free: crude DFT energy in three bands over the second half of the signal.
    const std::size_t n = y.size() / 2, off = y.size() - n;
    Bands b;
    const double pi = 3.14159265358979323846;
    for (std::size_t k = 1; k < n / 2; ++k) {
        const double f = double(k) * fs / double(n);
        if (f > 12000.0) break;
        if (k % 4 != 0) continue;  // sparse bins are enough for a band comparison
        double re = 0, im = 0;
        for (std::size_t i = 0; i < n; ++i) {
            const double a = 2.0 * pi * double(k) * double(i) / double(n);
            re += y[off + i] * std::cos(a);
            im -= y[off + i] * std::sin(a);
        }
        const double e = re * re + im * im;
        (f < 400 ? b.low : f < 3000 ? b.mid : b.high) += e;
        b.total += e;
    }
    return b;
}

double rms(const std::vector<double>& y) {
    double s = 0;
    for (double v : y) s += v * v;
    return std::sqrt(s / double(y.size()));
}

double db(double a, double b) { return 10.0 * std::log10((a + 1e-300) / (b + 1e-300)); }

template <class AmpT>
void checkAmp(const char* name, bool smooth) {
    const auto c = golden::load(name);
    AmpT amp(c.num("fs"));
    amp.setControls(controlsOf(c), c.num("presence"));
    amp.setInputMode(c.str("jack") == "high" ? InputMode::High : InputMode::Low);
    std::vector<double> y;
    for (double x : c.in) y.push_back(amp.process(x));
    const auto cmp = golden::compare(y, c.out);
    INFO(name << ": sample error " << cmp.errDb() << " dB, rms " << rms(y) << " vs " << rms(c.out)
              << ", failures " << amp.newtonFailures());
    CHECK(amp.newtonFailures() == 0);
    if (smooth) {
        CHECK(cmp.errDb() < -100.0);
    } else {
        CHECK(std::abs(20.0 * std::log10(rms(y) / rms(c.out))) < 0.3);
        const auto a = bands(y, c.num("fs")), r = bands(c.out, c.num("fs"));
        CHECK(std::abs(db(a.low, r.low)) < 0.6);
        CHECK(std::abs(db(a.mid, r.mid)) < 0.6);
        CHECK(std::abs(db(a.high, r.high)) < 1.5);
    }
}

}  // namespace

TEST_CASE("power amp DC state matches Rust") {
    PowerAmp2203 pa(96000.0);
    const auto ref = golden::loadDc("dc_poweramp.txt");
    REQUIRE(ref.size() == pa.solver().nodeCount());
    // The dump is taken after the 0.15 s settle onto the rectified waveform, so allow the
    // ripple/phase to differ slightly.
    for (std::size_t i = 0; i < ref.size(); ++i) {
        INFO("node " << i);
        CHECK(std::abs(pa.solver().voltage(i) - ref[i]) < 1e-3 + 1e-4 * std::abs(ref[i]));
    }
}

TEST_CASE("full amp matches Rust") {
    checkAmp<Amp2203>("full_clean", true);
    checkAmp<Amp2203>("full_crunch", false);
    checkAmp<Amp2203>("full_lead", false);
    checkAmp<Amp2203>("full_low_jack", false);
}

TEST_CASE("lite amp matches Rust") {
    checkAmp<AmpLite2203>("lite_clean", true);
    checkAmp<AmpLite2203>("lite_crunch", false);
    checkAmp<AmpLite2203>("lite_lead", false);
    checkAmp<AmpLite2203>("lite_low_jack", false);
}

TEST_CASE("fast Tube Screamer matches Rust") {
    for (const char* name : {"ts_low", "ts_mid", "ts_hot"}) {
        const auto c = golden::load(name);
        TubeScreamerFast ts(c.num("fs"));
        ts.setControls({c.num("drive"), c.num("tone"), c.num("level")});
        std::vector<double> y;
        for (double x : c.in) y.push_back(ts.process(x));
        const auto cmp = golden::compare(y, c.out);
        INFO(name << ": error " << cmp.errDb() << " dB");
        CHECK(cmp.errDb() < -120.0);
    }
}
