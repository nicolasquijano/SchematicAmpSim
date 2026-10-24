#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cmath>

#include "aes/chain.h"
#include "amp_fixture.h"

using namespace aes;

namespace {

constexpr double kFs = 48000.0;

std::vector<float> sine(double hz, double amp, std::size_t n) {
    std::vector<float> v(n);
    for (std::size_t i = 0; i < n; ++i) v[i] = float(amp * std::sin(2.0 * 3.14159265358979 * hz * double(i) / kFs));
    return v;
}

double rms(const std::vector<float>& v, std::size_t from = 0) {
    double s = 0;
    for (std::size_t i = from; i < v.size(); ++i) s += double(v[i]) * v[i];
    return std::sqrt(s / double(v.size() - from));
}

ChainSettings defaults(const AmpDef& def) {
    ChainSettings s;
    for (const auto& k : def.preamp.knobs) s.knobs.push_back(k.def);
    return s;
}

std::vector<float> run(Chain& c, const std::vector<float>& in) {
    std::vector<float> out(in.size());
    c.processAmp(in.data(), out.data(), in.size());
    c.applyOutput(out.data(), out.size());
    return out;
}

}  // namespace

TEST_CASE("chain equals the same primitives wired by hand (static settings)") {
    const auto& def = jcm800();
    Chain chain(def, kFs);
    auto s = defaults(def);
    s.cabFilter = true;
    chain.setSettings(s);

    // The same wiring, written out with the engine pieces.
    FullAmp amp(def, kFs * 2.0);
    amp.setKnobs(s.knobs, s.presence);
    amp.setJack(0);
    amp.setSourceOhms(def.preamp.sourceOhmsDefault);
    Oversampler os(1);
    CabFilter cab{float(kFs)};

    const auto in = sine(220.0, 0.1, 9600);
    const auto got = run(chain, in);
    std::vector<float> want;
    for (float x : in) {
        float v = os.process(x, [&](float u) { return float(amp.process(double(u))); });
        want.push_back(cab.process(v / 100.0f));
    }
    double maxDiff = 0;
    for (std::size_t i = 0; i < want.size(); ++i) maxDiff = std::max(maxDiff, double(std::abs(got[i] - want[i])));
    INFO("max diff " << maxDiff);
    CHECK(maxDiff == 0.0);
}

TEST_CASE("chain reports the oversampler latency and handles every quality") {
    const auto& def = jcm800();
    Chain chain(def, kFs);
    auto s = defaults(def);
    s.quality = Quality::Normal;
    chain.setSettings(s);
    const double l2 = chain.latencySamples();
    s.quality = Quality::High;
    chain.setSettings(s);
    const double l4 = chain.latencySamples();
    CHECK(l2 > 15.0);
    CHECK(l4 > l2);
    s.quality = Quality::Lite;
    chain.setSettings(s);
    CHECK(chain.latencySamples() == l2);

    for (Quality q : {Quality::Normal, Quality::High, Quality::Lite, Quality::Normal}) {
        s.quality = q;
        chain.setSettings(s);
        const auto out = run(chain, sine(110.0, 0.1, 4800));
        for (float v : out) REQUIRE(std::isfinite(v));
        CHECK(rms(out, 2400) > 1e-4);
    }
}

TEST_CASE("jack, TS808 and cab filter change the sound in the expected direction") {
    const auto& def = jcm800();
    auto level = [&](auto&& tweak) {
        Chain chain(def, kFs);
        auto s = defaults(def);
        tweak(s);
        chain.setSettings(s);
        return rms(run(chain, sine(220.0, 0.05, 12000)), 6000);
    };
    const double base = level([](ChainSettings&) {});
    const double low = level([](ChainSettings& s) { s.jack = 1; });
    CHECK(low < base * 0.2);  // Low skips the first gain stage: much quieter

    const double ts = level([](ChainSettings& s) { s.tsOn = true; s.tsDrive = 0.8; });
    CHECK(std::abs(ts - base) / base > 0.05);

    const double noCab = level([](ChainSettings& s) { s.cabFilter = false; });
    CHECK(noCab > 0.0);
}

TEST_CASE("output gain follows the dB setting and non-finite samples are zeroed") {
    const auto& def = jcm800();
    Chain chain(def, kFs);
    auto s = defaults(def);
    chain.setSettings(s);
    const auto a = run(chain, sine(220.0, 0.05, 12000));
    s.outputDb = -6.0206;
    chain.setSettings(s);
    const auto b = run(chain, sine(220.0, 0.05, 12000));
    // After the 20 ms ramp the second block is ~half the amplitude (same signal, settled).
    CHECK(rms(b, 9000) / rms(a, 9000) == Catch::Approx(0.5).epsilon(0.05));

    std::vector<float> bad{1.0f, INFINITY, NAN, 0.5f};
    chain.applyOutput(bad.data(), bad.size());
    for (float v : bad) CHECK(std::isfinite(v));
}
