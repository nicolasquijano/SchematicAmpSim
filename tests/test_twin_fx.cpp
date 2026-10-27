#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <vector>

#include "sas/chain.h"
#include "sas/embedded_amps.h"

using namespace sas;

namespace {

constexpr double kFs = 48000.0;
constexpr double kPi = 3.14159265358979323846;

const char* qualityName(Quality q) {
    switch (q) {
        case Quality::Normal: return "Normal";
        case Quality::High: return "High";
        case Quality::Lite: return "Lite";
        default: return "Eco";
    }
}

ChainSettings twinSettings(const AmpDef& def, Quality q) {
    ChainSettings s;
    for (const auto& k : def.preamp.knobs) s.knobs.push_back(k.def);
    for (const auto& k : def.power.knobs) s.powerKnobs.push_back(k.def);
    s.presence = def.power.presenceDefault;
    s.quality = q;
    s.cabFilter = false;
    return s;
}

// rms in windows of `win` samples over [from, to).
std::vector<double> windowRms(const std::vector<float>& v, std::size_t from, std::size_t to, std::size_t win) {
    std::vector<double> r;
    for (std::size_t a = from; a + win <= to; a += win) {
        double e = 0;
        for (std::size_t i = a; i < a + win; ++i) e += double(v[i]) * v[i];
        r.push_back(std::sqrt(e / double(win)));
    }
    return r;
}

}  // namespace

TEST_CASE("American Twin: the vibrato modulates the level in every quality") {
    const auto def = loadEmbeddedAmp("twin_65");
    REQUIRE(def.preamp.vibrato.present);
    for (Quality q : {Quality::Normal, Quality::High, Quality::Lite, Quality::Eco}) {
        auto depth = [&](bool on) {
            Chain chain(def, kFs);
            auto s = twinSettings(def, q);
            s.vibratoOn = on;
            s.vibratoSpeed = 0.7;
            chain.snap(s);
            const std::size_t n = std::size_t(4.0 * kFs);
            std::vector<float> in(n), out(n);
            for (std::size_t i = 0; i < n; ++i) in[i] = float(0.05 * std::sin(2.0 * kPi * 220.0 * double(i) / kFs));
            chain.processAmp(in.data(), out.data(), n);
            const auto r = windowRms(out, n / 4, n, 480);
            double lo = 1e9, hi = 0;
            for (double v : r) { lo = std::min(lo, v); hi = std::max(hi, v); }
            return 20.0 * std::log10(hi / std::max(lo, 1e-12));
        };
        const double off = depth(false), on = depth(true);
        INFO(qualityName(q) << ": level swing " << off << " dB with the vibrato off, " << on << " dB on");
        CHECK(on > off + 1.5);
    }
}

TEST_CASE("American Twin: the spring reverb leaves a tail in every quality") {
    const auto def = loadEmbeddedAmp("twin_65");
    const std::size_t reverbKnob = def.knobIndex("reverb");
    for (Quality q : {Quality::Normal, Quality::High, Quality::Lite, Quality::Eco}) {
        auto tail = [&](double reverb) {
            Chain chain(def, kFs);
            auto s = twinSettings(def, q);
            s.knobs[reverbKnob] = reverb;
            chain.snap(s);
            const std::size_t n = std::size_t(2.0 * kFs);
            std::vector<float> in(n, 0.0f), out(n);
            for (std::size_t i = 0; i < std::size_t(0.05 * kFs); ++i) in[i] = float(0.2 * std::sin(2.0 * kPi * 330.0 * double(i) / kFs));  // a short note
            chain.processAmp(in.data(), out.data(), n);
            const auto r = windowRms(out, std::size_t(0.25 * kFs), std::size_t(0.75 * kFs), std::size_t(0.5 * kFs));
            return r.empty() ? 0.0 : r[0];
        };
        const double dry = tail(0.0), wet = tail(1.0);
        INFO(qualityName(q) << ": rms of the 0.25..0.75 s tail " << dry << " with the reverb at 0, " << wet << " at 10");
        CHECK(wet > 3.0 * dry);
        CHECK(wet > 1e-5);
    }
}
