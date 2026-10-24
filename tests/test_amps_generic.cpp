// Tests that every embedded amp must pass, plus checks specific to the Plexi transcription.
#include <catch2/catch_test_macros.hpp>
#include <cmath>

#include "aes/amp.h"
#include "aes/embedded_amps.h"
#include "aes/netlist.h"

using namespace aes;

namespace {

constexpr double kFs = 96000.0;
constexpr double kPi = 3.14159265358979323846;

std::vector<double> defaultKnobs(const AmpDef& d) {
    std::vector<double> k;
    for (const auto& x : d.preamp.knobs) k.push_back(x.def);
    return k;
}

template <class A>
double rmsAt(A& amp, double hz, double peak, double seconds = 0.5) {
    const std::size_t n = std::size_t(seconds * kFs);
    double acc = 0;
    std::size_t cnt = 0;
    for (std::size_t i = 0; i < n; ++i) {
        const double y = amp.process(peak * std::sin(2 * kPi * hz * double(i) / kFs));
        REQUIRE(std::isfinite(y));
        if (i >= n / 2) { acc += y * y; ++cnt; }
    }
    return std::sqrt(acc / double(cnt));
}

}  // namespace

TEST_CASE("every embedded amp loads, converges, and makes sound with every jack") {
    const auto ids = embeddedAmpIds();
    REQUIRE(ids.size() >= 2);
    for (const auto& id : ids) {
        DYNAMIC_SECTION(id) {
            const AmpDef def = loadEmbeddedAmp(id);
            CHECK(!def.name.empty());
            CHECK(!def.ui.empty());
            PowerAmp power(def, kFs);
            CHECK(power.solver().dcConverged);
            for (std::size_t j = 0; j < def.preamp.jacks.size(); ++j) {
                FullAmp amp(def, kFs);
                amp.setKnobs(defaultKnobs(def), def.power.presenceDefault);
                amp.setJack(j);
                const double level = rmsAt(amp, 220.0, 0.1);
                INFO(id << " jack " << def.preamp.jacks[j].name << " rms " << level);
                CHECK(level > 0.1);
                CHECK(amp.newtonFailures() == 0);
            }
        }
    }
}

TEST_CASE("every ui knob of every amp points at a real preamp knob") {
    for (const auto& id : embeddedAmpIds()) {
        const AmpDef def = loadEmbeddedAmp(id);
        for (const auto& u : def.ui) {
            INFO(id << " ui " << u.param);
            if (!u.knob.empty()) CHECK_NOTHROW(def.knobIndex(u.knob));
            if (!u.powerKnob.empty()) CHECK_NOTHROW(def.powerKnobIndex(u.powerKnob));
        }
    }
}

TEST_CASE("selector captions and labels come from each amp") {
    const AmpDef afd = loadEmbeddedAmp("afd100"), plexi = loadEmbeddedAmp("plexi_1959"), jcm = loadEmbeddedAmp("jcm800_2203");
    CHECK(afd.preamp.jackLabel == "MODE");
    CHECK(afd.preamp.jacks[0].label == "Normal");
    CHECK(afd.preamp.jacks[1].label == "AFD");
    CHECK(plexi.preamp.jackLabel == "CHANNEL");
    CHECK(jcm.preamp.jackLabel == "INPUT JACK");
    CHECK(jcm.preamp.jacks[1].label == "Low");
}

TEST_CASE("amp list order is stable: saved projects store the index") {
    const auto ids = embeddedAmpIds();
    REQUIRE(ids.size() >= 3);
    CHECK(ids[0] == "jcm800_2203");
    CHECK(ids[1] == "plexi_1959");
    CHECK(ids[2] == "afd100");
}

TEST_CASE("AFD100: operating point, power and controls") {
    const AmpDef def = loadEmbeddedAmp("afd100");
    {
        PowerAmp power(def, kFs);
        const auto net = parseNetlist(def.power.netlistText, def.power.netlist);
        auto v = [&](const char* n) { return power.solver().voltage(net.node(n)); };
        CHECK(v("a") > 450.0);  // B+
        CHECK(v("a") < 540.0);
        CHECK(std::abs(v("s12") - 340.0) < 15.0);  // regulated screens
        const double idleMa = (v("a") - v("pt")) / 40.0 / 2.0 * 1e3;
        CHECK(idleMa > 25.0);
        CHECK(idleMa < 45.0);
    }
    auto level = [&](double volume, double master, double power, std::size_t jack, double peak, double presence = 0.5) {
        FullAmp amp(def, kFs);
        auto k = defaultKnobs(def);
        k[def.knobIndex("volume")] = volume;
        k[def.knobIndex("master")] = master;
        amp.setKnobs(k, presence, {power});
        amp.setJack(jack);
        return rmsAt(amp, 220.0, peak);
    };
    const std::size_t normal = def.jackIndex("normal"), afd = def.jackIndex("afd");
    // A 100 W amp: wide open it delivers roughly 100 W into 16 ohms (40 V rms).
    const double open = level(1.0, 1.0, 1.0, normal, 0.5);
    const double watts = open * open / 16.0;
    INFO("full power " << watts << " W");
    CHECK(watts > 70.0);
    CHECK(watts < 150.0);
    // The master tames it, and AFD mode (V6B skipped) has less gain than the normal mode at low drive.
    CHECK(level(0.6, 0.2, 0.5, normal, 0.05) < level(0.6, 0.8, 0.5, normal, 0.05));
    CHECK(level(0.6, 0.5, 0.5, afd, 0.005) < 0.6 * level(0.6, 0.5, 0.5, normal, 0.005));
    // Less negative feedback (a larger "power" resistor) means more gain.
    CHECK(level(0.6, 0.5, 1.0, normal, 0.01) > 1.2 * level(0.6, 0.5, 0.0, normal, 0.01));
}

TEST_CASE("AFD100: the Lite power amp follows the full circuit") {
    const AmpDef def = loadEmbeddedAmp("afd100");
    for (double peak : {0.01, 0.05, 0.2, 0.5}) {
        FullAmp full(def, kFs);
        LiteAmp lite(def, kFs);
        auto k = defaultKnobs(def);
        k[def.knobIndex("master")] = 0.9;  // driven hard enough to reach the power stage's saturation
        full.setKnobs(k, 0.5, {0.5});
        lite.setKnobs(k, 0.5, {0.5});
        const double f = rmsAt(full, 150.0, peak, 1.0), l = rmsAt(lite, 150.0, peak, 1.0);
        const double errDb = 20.0 * std::log10(l / f);
        INFO("peak " << peak << ": full " << f << " V, lite " << l << " V, " << errDb << " dB");
        CHECK(std::abs(errDb) < 2.0);
    }
}

TEST_CASE("Plexi: supply and operating point are in the range the drawing shows") {
    const AmpDef def = loadEmbeddedAmp("plexi_1959");
    PowerAmp power(def, kFs);
    const auto net = parseNetlist(def.power.netlistText, def.power.netlist);
    auto v = [&](const char* n) { return power.solver().voltage(net.node(n)); };
    CHECK(v("a") > 440.0);   // B+ ~460 V on the drawing
    CHECK(v("a") < 490.0);
    CHECK(v("bias") < -38.0);
    CHECK(v("bias") > -48.0);
    const double idleMa = (v("a") - v("pt")) / 40.0 / 2.0 * 1e3;  // per tube
    CHECK(idleMa > 25.0);
    CHECK(idleMa < 45.0);

    Preamp pre(def, kFs, power.railX());
    const auto pnet = parseNetlist(def.preamp.netlistText, def.preamp.netlist);
    CHECK(std::abs(pre.solver().voltage(pnet.node("na")) - 270.0) < 25.0);  // V1 plates: 270 V
    CHECK(std::abs(pre.solver().voltage(pnet.node("nb")) - 300.0) < 25.0);  // V2 plates: 300 V
}

TEST_CASE("Plexi: channels, jumper and presence behave like the amp") {
    const AmpDef def = loadEmbeddedAmp("plexi_1959");
    auto level = [&](std::size_t jack, double volBright, double volNormal, double presence, double hz, double peak = 0.02) {
        FullAmp amp(def, kFs);
        auto k = defaultKnobs(def);
        k[def.knobIndex("bright_volume")] = volBright;
        k[def.knobIndex("normal_volume")] = volNormal;
        amp.setKnobs(k, presence);
        amp.setJack(jack);
        return rmsAt(amp, hz, peak);
    };
    const std::size_t bright = def.jackIndex("bright"), normal = def.jackIndex("normal"), jumpered = def.jackIndex("jumpered");

    // The channel selector isolates each channel: a closed volume on the unused one changes nothing much.
    CHECK(level(bright, 0.6, 0.0, 0.5, 220.0) > 3.0 * level(bright, 0.0, 0.6, 0.5, 220.0));
    CHECK(level(normal, 0.0, 0.6, 0.5, 220.0) > 3.0 * level(normal, 0.6, 0.0, 0.5, 220.0));
    // Jumpered adds the two channels (never quieter than either alone).
    const double b = level(bright, 0.6, 0.6, 0.5, 220.0), n = level(normal, 0.6, 0.6, 0.5, 220.0);
    CHECK(level(jumpered, 0.6, 0.6, 0.5, 220.0) >= 0.95 * std::max(b, n));
    // The bright channel's small coupling cap and bright cap: relatively more treble than the normal one.
    const double trebleRatioBright = level(bright, 0.5, 0.5, 0.5, 3000.0) / level(bright, 0.5, 0.5, 0.5, 200.0);
    const double trebleRatioNormal = level(normal, 0.5, 0.5, 0.5, 3000.0) / level(normal, 0.5, 0.5, 0.5, 200.0);
    CHECK(trebleRatioBright > trebleRatioNormal);
    // More presence opens up the highs of the power amp.
    // (a small input, so the power stage is not already clipping with and without it)
    CHECK(level(bright, 0.5, 0.5, 1.0, 4000.0, 0.001) > 1.5 * level(bright, 0.5, 0.5, 0.0, 4000.0, 0.001));
}

TEST_CASE("Plexi: the Lite power amp follows the full circuit from clean to saturated") {
    const AmpDef def = loadEmbeddedAmp("plexi_1959");
    for (double peak : {0.005, 0.02, 0.1, 0.5}) {
        FullAmp full(def, kFs);
        LiteAmp lite(def, kFs);
        const auto k = defaultKnobs(def);
        full.setKnobs(k, 0.5);
        lite.setKnobs(k, 0.5);
        const double f = rmsAt(full, 150.0, peak, 1.0), l = rmsAt(lite, 150.0, peak, 1.0);
        const double errDb = 20.0 * std::log10(l / f);
        INFO("peak " << peak << ": full " << f << " V, lite " << l << " V, " << errDb << " dB");
        CHECK(std::abs(errDb) < 1.5);
    }
}
