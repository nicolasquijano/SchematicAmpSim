// Tests that every embedded amp must pass, plus checks specific to the Plexi transcription.
#include <catch2/catch_test_macros.hpp>
#include <cmath>

#include "sas/amp.h"
#include "sas/embedded_amps.h"
#include "sas/netlist.h"

using namespace sas;

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

TEST_CASE("Eco (triode tables) sounds the same as Lite on every amp") {
    for (const auto& id : embeddedAmpIds()) {
        const AmpDef def = loadEmbeddedAmp(id);
        for (double peak : {0.02, 0.1, 0.4}) {
            LiteAmp lite(def, kFs), eco(def, kFs, true);
            const auto k = defaultKnobs(def);
            lite.setKnobs(k, def.power.presenceDefault);
            eco.setKnobs(k, def.power.presenceDefault);
            const std::size_t n = std::size_t(0.5 * kFs);
            double accL = 0, accE = 0, accD = 0;
            for (std::size_t i = 0; i < n; ++i) {
                const double x = peak * std::sin(2 * kPi * 150.0 * double(i) / kFs);
                const double a = lite.process(x), b = eco.process(x);
                REQUIRE(std::isfinite(b));
                if (i >= n / 2) {
                    accL += a * a;
                    accE += b * b;
                    accD += (a - b) * (a - b);
                }
            }
            const double levelDb = 10.0 * std::log10(accE / accL), diffDb = 10.0 * std::log10((accD + 1e-30) / accL);
            INFO(id << " at " << peak << " V: level " << levelDb << " dB, sample difference " << diffDb << " dB");
            CHECK(std::abs(levelDb) < 0.1);
            // Tables alone are exact (-150 dB). An amp with its own finer-split Eco preamp differs by the
            // split's loading error (tests/test_split.cpp bounds it per frequency): ~0.5 % of amplitude here.
            CHECK(diffDb < (def.hasEcoPreamp ? -40.0 : -120.0));
            CHECK(eco.newtonFailures() == 0);
        }
    }
}

TEST_CASE("selector captions and labels come from each amp") {
    const AmpDef afd = loadEmbeddedAmp("afd100"), plexi = loadEmbeddedAmp("plexi_1959"), jcm = loadEmbeddedAmp("jcm800_2203");
    CHECK(afd.preamp.jackLabel == "MODE");
    CHECK(afd.preamp.jacks[0].label == "Normal");
    CHECK(afd.preamp.jacks[1].label == "Alt");
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

TEST_CASE("Twin: supply and operating point are in the range the drawing shows") {
    const AmpDef def = loadEmbeddedAmp("twin_65");
    PowerAmp power(def, kFs);
    const auto net = parseNetlist(def.power.netlistText, def.power.netlist);
    auto v = [&](const char* n) { return power.solver().voltage(net.node(n)); };
    CHECK(std::abs(v("a") - 440.0) < 12.0);      // TP28 +440 V
    CHECK(std::abs(v("x") - 393.0) < 8.0);       // TP30 +393 V
    CHECK(std::abs(v("k") - 99.0) < 15.0);       // TP18: phase inverter cathodes +99 V
    CHECK(std::abs(v("cbias") + 50.0) < 6.0);    // TP26 -50 V
    const double idleMa = (v("a") - v("pt")) / 35.0 / 2.0 * 1e3;  // per tube
    CHECK(idleMa > 25.0);
    CHECK(idleMa < 45.0);

    Preamp pre(def, kFs, power.railX());
    const auto pnet = parseNetlist(def.preamp.netlistText, def.preamp.netlist);
    auto p = [&](const char* n) { return pre.solver().voltage(pnet.node(n)); };
    CHECK(std::abs(p("ka2") - 1.9) < 0.4);       // TP2 +1.9 V (V2A cathode)
    CHECK(std::abs(p("kb") - 2.0) < 0.4);        // TP5 +2.0 V (V1B cathode)
}

TEST_CASE("Twin: jacks, channels and the ganged tone controls behave like the amp") {
    const AmpDef def = loadEmbeddedAmp("twin_65");
    auto level = [&](const char* jack, double bass, double treble, double hz, double peak = 0.005) {
        FullAmp amp(def, kFs);
        auto k = defaultKnobs(def);
        k[def.knobIndex("bass")] = bass;
        k[def.knobIndex("treble")] = treble;
        amp.setKnobs(k, 0.5);
        amp.setJack(def.jackIndex(jack));
        return rmsAt(amp, hz, peak);
    };
    // Input 2 is the -6 dB input, on both channels.
    for (const char* ch : {"vibrato", "normal"}) {
        const double hi = level((std::string(ch) + "1").c_str(), 0.6, 0.6, 500.0);
        const double lo = level((std::string(ch) + "2").c_str(), 0.6, 0.6, 500.0);
        INFO(ch << ": input 1 " << hi << " V, input 2 " << lo << " V");
        CHECK(std::abs(20.0 * std::log10(lo / hi) + 6.0) < 1.5);
    }
    // One set of tone controls serves both channels: more treble opens the highs on either.
    for (const char* jack : {"vibrato1", "normal1"}) {
        const double bright = level(jack, 0.5, 1.0, 3000.0) / level(jack, 0.5, 1.0, 200.0);
        const double dull = level(jack, 0.5, 0.0, 3000.0) / level(jack, 0.5, 0.0, 200.0);
        INFO(jack << ": " << bright << " vs " << dull);
        CHECK(bright > 2.0 * dull);
        CHECK(level(jack, 1.0, 0.5, 100.0) > 1.5 * level(jack, 0.0, 0.5, 100.0));  // bass knob
    }
}

TEST_CASE("Twin: the optical vibrato chops the signal only when it is on and the intensity is up") {
    const AmpDef def = loadEmbeddedAmp("twin_65");
    REQUIRE(def.preamp.vibrato.present);
    const std::size_t intensity = def.knobIndex("intensity");
    // Depth in dB of the 20 ms output rms of a steady tone over the second half of `seconds`.
    auto depthDb = [&](bool on, double inten, double speed) {
        FullAmp amp(def, kFs);
        auto k = defaultKnobs(def);
        k[intensity] = inten;
        amp.setKnobs(k, 0.5);
        amp.setJack(def.jackIndex("vibrato1"));
        amp.pre().setVibrato(on, speed);
        const std::size_t win = std::size_t(0.02 * kFs), total = std::size_t(6.0 * kFs);
        std::vector<double> rms;
        double acc = 0;
        for (std::size_t i = 0; i < total; ++i) {
            const double y = amp.process(0.02 * std::sin(2 * kPi * 220.0 * double(i) / kFs));
            REQUIRE(std::isfinite(y));
            acc += y * y;
            if ((i + 1) % win == 0) { rms.push_back(std::sqrt(acc / double(win))); acc = 0; }
        }
        double lo = 1e9, hi = 0;
        for (std::size_t i = rms.size() / 2; i < rms.size(); ++i) { lo = std::min(lo, rms[i]); hi = std::max(hi, rms[i]); }
        return 20.0 * std::log10(hi / lo);
    };
    CHECK(depthDb(false, 1.0, 0.5) < 0.5);   // footswitch off: steady
    CHECK(depthDb(true, 0.0, 0.5) < 0.5);    // intensity 0: no effect
    CHECK(depthDb(true, 1.0, 0.5) > 6.0);    // full intensity: a clear chop
    CHECK(depthDb(true, 0.5, 0.5) > 3.0);
    CHECK(depthDb(true, 0.5, 0.5) < depthDb(true, 1.0, 0.5));
}
