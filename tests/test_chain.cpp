#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <string>

#include "sas/chain.h"
#include "sas/embedded_amps.h"
#include "amp_fixture.h"

using namespace sas;

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

    for (Quality q : {Quality::Normal, Quality::High, Quality::Lite, Quality::Eco, Quality::Normal}) {
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
    const double low = level([](ChainSettings& s) { s.jack = 1; s.jackLevelMatch = false; });  // the raw circuit
    CHECK(low < base * 0.2);  // Low skips the first gain stage: much quieter

    const double ts = level([](ChainSettings& s) { s.tsOn = true; s.tsDrive = 0.8; });
    CHECK(std::abs(ts - base) / base > 0.05);

    const double noCab = level([](ChainSettings& s) { s.cabFilter = false; });
    CHECK(noCab > 0.0);
}

TEST_CASE("level match lifts the JCM800's quiet Low jack and leaves High alone") {
    const auto& def = jcm800();
    auto level = [&](std::size_t jack, bool match, double peak) {
        Chain chain(def, kFs);
        auto s = defaults(def);
        s.jack = jack;
        s.jackLevelMatch = match;
        chain.snap(s);
        return rms(run(chain, sine(220.0, peak, 14000)), 7000);
    };
    // Small signal: the Low jack is ~31 dB below High; with the match it lands within a few dB.
    const double high = level(0, true, 0.003), lowRaw = level(1, false, 0.003), lowMatched = level(1, true, 0.003);
    INFO("high " << high << ", low " << lowRaw << ", low matched " << lowMatched);
    CHECK(20.0 * std::log10(high / lowRaw) > 25.0);
    CHECK(std::abs(20.0 * std::log10(high / lowMatched)) < 4.0);
    // High is untouched by the match, and the output stays finite and within full scale when driven hard.
    CHECK(level(0, true, 0.003) == level(0, false, 0.003));
    CHECK(level(1, true, 0.3) < 1.0);
}

TEST_CASE("every amp and every jack plays at the same loudness at the reference input") {
    // The reference input is the level of the author's recorded guitar (rms ~0.05): a three-sine
    // guitar-like signal of 0.1 V peak. Amps differ at other playing levels (their gains and
    // saturation differ), but at the reference they must agree; see tools/sas_match_levels.
    auto guitar = [](std::size_t n) {
        std::vector<float> v(n);
        for (std::size_t i = 0; i < n; ++i) {
            const double t = double(i) / kFs, pi = 3.14159265358979;
            v[i] = float(0.1 * (0.6 * std::sin(2 * pi * 110.0 * t) + 0.3 * std::sin(2 * pi * 330.7 * t + 0.4) + 0.1 * std::sin(2 * pi * 1234.5 * t)));
        }
        return v;
    };
    const auto in = guitar(60000);
    std::vector<std::pair<std::string, double>> levels;
    for (const auto& id : embeddedAmpIds()) {
        const AmpDef def = loadEmbeddedAmp(id);
        for (std::size_t j = 0; j < def.preamp.jacks.size(); ++j) {
            Chain chain(def, kFs);
            ChainSettings s = defaults(def);
            for (const auto& k : def.power.knobs) s.powerKnobs.push_back(k.def);
            s.presence = def.power.presenceDefault;
            s.jack = j;
            chain.snap(s);
            std::vector<float> out(in.size());
            chain.processAmp(in.data(), out.data(), in.size());
            levels.push_back({def.id + "/" + def.preamp.jacks[j].name, 20.0 * std::log10(rms(out, 30000))});
        }
    }
    REQUIRE(levels.size() >= 7);
    for (const auto& [name, db] : levels) {
        INFO(name << ": " << db << " dB re " << levels[0].first << " " << levels[0].second);
        CHECK(std::abs(db - levels[0].second) < 1.0);
    }
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
