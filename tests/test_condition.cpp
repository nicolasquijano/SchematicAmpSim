#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <vector>

#include "sas/chain.h"
#include "sas/condition.h"
#include "sas/embedded_amps.h"

using namespace sas;

namespace {

constexpr double kFs = 48000.0;

double idleOf(const AmpDef& def, double biasScale = 1.0) {
    PowerAmp pa(def, kFs);
    pa.setBias(biasScale);
    for (std::size_t i = 0, n = std::size_t(1.5 * kFs); i < n; ++i) pa.process(0.0);
    return pa.idleMilliamps();
}

ChainSettings defaults(const AmpDef& def) {
    ChainSettings s;
    for (const auto& k : def.preamp.knobs) s.knobs.push_back(k.def);
    for (const auto& k : def.power.knobs) s.powerKnobs.push_back(k.def);
    s.presence = def.power.presenceDefault;
    s.cabFilter = false;
    return s;
}

double silentRms(const AmpDef& def, int pickup, double noise) {
    Chain chain(def, kFs);
    auto s = defaults(def);
    s.pickup = pickup;
    s.noise = noise;
    chain.snap(s);
    const std::size_t n = std::size_t(1.0 * kFs);
    std::vector<float> in(n, 0.0f), out(n);
    chain.processAmp(in.data(), out.data(), n);
    double acc = 0;
    for (std::size_t i = n / 2; i < n; ++i) acc += double(out[i]) * out[i];
    return std::sqrt(acc / double(n - n / 2));
}

}  // namespace

TEST_CASE("a default condition leaves the amp untouched") {
    const auto def = loadEmbeddedAmp("jcm800_2203");
    const auto same = applyCondition(def, Condition{});
    CHECK(same.power.netlistText == def.power.netlistText);
    CHECK(same.preamp.netlistText == def.preamp.netlistText);
}

TEST_CASE("bias control moves the idle current of the output tubes") {
    for (const auto* id : {"jcm800_2203", "twin_65"}) {
        const auto def = loadEmbeddedAmp(id);
        const double stock = idleOf(def);
        INFO(id << " idle " << stock << " mA");
        CHECK(stock > 15.0);
        CHECK(stock < 80.0);
        CHECK(idleOf(def, 1.2) < stock * 0.85);  // a more negative bias: colder
        CHECK(idleOf(def, 0.8) > stock * 1.15);  // hotter
    }
    // Cathode-biased amps only read the current.
    const auto tweed = loadEmbeddedAmp("tweed_5e3");
    CHECK_FALSE(tweed.power.bias.adjustable());
    CHECK(idleOf(tweed) > 10.0);
}

TEST_CASE("another output tube keeps the same share of its dissipation rating") {
    for (const auto& id : embeddedAmpIds()) {
        const auto def = loadEmbeddedAmp(id);
        if (def.power.tubes.size() < 2) continue;
        const double stock = idleOf(def);
        for (std::size_t k = 1; k < def.power.tubes.size(); ++k) {
            const double want = stock * def.power.tubes[k].pmaxW / def.power.tubes[0].pmaxW;
            const double got = idleOf(applyCondition(def, {int(k), 0, 0}));
            INFO(id << " " << def.power.tubes[k].name << " wants " << want << " mA, gets " << got);
            CHECK(got == Catch::Approx(want).epsilon(0.05));
        }
    }
}

TEST_CASE("wear lowers the output tube current and every worn or toleranced unit still converges") {
    for (const auto& id : embeddedAmpIds()) {
        const auto def = loadEmbeddedAmp(id);
        const double fresh = idleOf(def);
        const double worn = idleOf(applyCondition(def, {0, 10, 0}));
        INFO(id << " fresh " << fresh << " worn " << worn);
        CHECK(worn < fresh * 0.95);
        CHECK(worn > fresh * 0.4);
        for (int unit : {1, 4, 9}) {
            const auto d = applyCondition(def, {0, unit == 4 ? 10 : 0, unit});
            FullAmp amp(d, kFs * 2.0);  // builds both sections and solves their DC points
            CHECK(amp.newtonFailures() == 0);
            INFO(id << " unit " << unit);
            CHECK(amp.power().idleMilliamps() >= 0.0);
        }
    }
}

TEST_CASE("units differ from each other and repeat exactly") {
    const auto def = loadEmbeddedAmp("plexi_1959");
    const auto a1 = applyCondition(def, {0, 0, 1});
    const auto a1b = applyCondition(def, {0, 0, 1});
    const auto a2 = applyCondition(def, {0, 0, 2});
    CHECK(a1.preamp.netlistText == a1b.preamp.netlistText);
    CHECK(a1.preamp.netlistText != a2.preamp.netlistText);
    CHECK(a1.preamp.netlistText != def.preamp.netlistText);
    CHECK(idleOf(a1) != idleOf(a2));
}

TEST_CASE("noise and hum: silent when off, audible when on, and a single coil hums more than a humbucker") {
    const auto def = loadEmbeddedAmp("jcm800_2203");
    const double off = silentRms(def, 1, 0.0);
    const double single = silentRms(def, 1, 1.0);
    const double humbucker = silentRms(def, 2, 1.0);
    INFO("off " << off << " single " << single << " humbucker " << humbucker);
    CHECK(off < 1e-5);
    CHECK(single > 10.0 * off);
    CHECK(single < 0.5);  // a hum, not a roar
    CHECK(humbucker < single * 0.6);
}

TEST_CASE("full circuits through the triode tables sound the same as the exact formulas") {
    constexpr double fs2 = kFs * 2.0;
    for (const auto* id : {"jcm800_2203", "highgain_120", "ac30_normal"}) {
        const auto def = loadEmbeddedAmp(id);
        FullAmp exact(def, fs2), tables(def, fs2, false, true);
        std::vector<double> knobs;
        for (const auto& k : def.preamp.knobs) knobs.push_back(k.def);
        exact.setKnobs(knobs, def.power.presenceDefault);
        tables.setKnobs(knobs, def.power.presenceDefault);
        double err = 0.0, ref = 0.0;
        for (std::size_t i = 0, n = std::size_t(0.5 * fs2); i < n; ++i) {
            const double x = 0.05 * std::sin(2.0 * 3.14159265358979 * 196.0 * double(i) / fs2) * std::min(1.0, double(i) / 4000.0);
            const double a = exact.process(x), b = tables.process(x);
            if (i > n / 2) { err += (a - b) * (a - b); ref += a * a; }
        }
        INFO(id << " error " << 10.0 * std::log10(err / ref) << " dB");
        CHECK(err < ref * 1e-6);  // below -60 dB
    }
}
