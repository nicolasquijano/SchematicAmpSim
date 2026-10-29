// Tests that every embedded amp must pass, plus checks specific to the Plexi transcription.
#include <catch2/catch_test_macros.hpp>
#include <array>
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
                // A jack that is much hotter than the others (the SVT's channel 1) is tested at the level the plugin gives it with "level match" on.
                const double peak = 0.1 * std::min(1.0, std::pow(10.0, def.preamp.jacks[j].levelDb / 20.0));
                const double level = rmsAt(amp, 220.0, peak);
                INFO(id << " jack " << def.preamp.jacks[j].name << " rms " << level);
                CHECK(level > 0.1);
                // The very first sample of a hard onset into the inductive speaker load may fail once (the next ones converge).
                CHECK(amp.newtonFailures() <= 2);
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

TEST_CASE("Twin: the spring reverb leaves a decaying tail only with the Reverb knob up") {
    const AmpDef def = loadEmbeddedAmp("twin_65");
    REQUIRE(def.preamp.reverb.present);
    const std::size_t rev = def.knobIndex("reverb");
    // Two identical amps, reverb at `level` and at 0: the difference of their outputs is the reverb alone
    // (the supply hum is the same in both). Its rms in windows [0.3, 0.5], [1.5, 1.7] and [2.5, 2.7] s
    // after a 0.1 s burst.
    auto tail = [&](double level) {
        FullAmp a(def, kFs), b(def, kFs);
        auto k = defaultKnobs(def);
        k[rev] = level;
        a.setKnobs(k, 0.5);
        k[rev] = 0.0;
        b.setKnobs(k, 0.5);
        a.setJack(def.jackIndex("vibrato1"));
        b.setJack(def.jackIndex("vibrato1"));
        const double burst = 0.1;
        std::array<double, 3> start{0.3, 1.5, 2.5}, acc{}, out{};
        std::array<std::size_t, 3> cnt{};
        const std::size_t total = std::size_t((burst + 2.8) * kFs);
        for (std::size_t i = 0; i < total; ++i) {
            const double t = double(i) / kFs;
            const double x = t < burst ? 0.02 * std::sin(2 * kPi * 330.0 * t) : 0.0;
            const double d = a.process(x) - b.process(x);
            REQUIRE(std::isfinite(d));
            for (std::size_t w = 0; w < 3; ++w)
                if (t >= burst + start[w] && t < burst + start[w] + 0.2) { acc[w] += d * d; ++cnt[w]; }
        }
        for (std::size_t w = 0; w < 3; ++w) out[w] = std::sqrt(acc[w] / double(cnt[w]));
        return out;
    };
    const auto none = tail(0.0), full = tail(1.0), mid = tail(0.4);
    INFO("reverb 0: " << none[0] << "; reverb 1: " << full[0] << " " << full[1] << " " << full[2] << "; 0.4: " << mid[0]);
    CHECK(none[0] < 1e-9);          // knob down: no reverb at all
    CHECK(full[0] > 1e-3);          // a clear tail
    CHECK(full[0] > full[1]);       // that decays
    CHECK(full[1] > full[2]);
    CHECK(full[1] > 1e-4);          // and lasts for seconds
    CHECK(mid[0] < full[0]);        // less with the knob lower
}

TEST_CASE("5150: supply, bias and idle current are in the range of the amp, and the power amp is quiet at rest") {
    const AmpDef def = loadEmbeddedAmp("highgain_120");
    PowerAmp power(def, kFs);
    const auto net = parseNetlist(def.power.netlistText, def.power.netlist);
    CHECK(power.solver().dcConverged);
    auto v = [&](const char* n) { return power.solver().voltage(net.node(n)); };
    CHECK(std::abs(v("a") - 480.0) < 40.0);      // B+ around 480 V
    CHECK(std::abs(v("cb") + 50.0) < 6.0);       // bias around -50 V
    const double idleMa = (v("a") - v("pt")) / 40.0 / 2.0 * 1e3;  // per tube
    CHECK(idleMa > 28.0);
    CHECK(idleMa < 55.0);
    // At rest nothing oscillates: the feedback loop with presence and resonance is stable.
    for (std::size_t i = 0; i < std::size_t(0.4 * kFs); ++i) power.process(0.0);
    double lo = 1e9, hi = -1e9;
    for (std::size_t i = 0; i < std::size_t(0.1 * kFs); ++i) {
        power.process(0.0);
        lo = std::min(lo, power.solver().voltage(net.node("t8")));
        hi = std::max(hi, power.solver().voltage(net.node("t8")));
    }
    CHECK(hi - lo < 0.5);
}

TEST_CASE("5150: the power amp makes about 100 W clean-to-clipped and the presence control opens the highs") {
    const AmpDef def = loadEmbeddedAmp("highgain_120");
    auto outRms = [&](double presence, double hz, double peak) {
        PowerAmp p(def, kFs);
        p.setPresence(presence);
        return rmsAt(p, hz, peak);
    };
    const double loud = outRms(0.5, 440.0, 10.0);
    INFO("440 Hz at 10 V: " << loud << " V rms into the 8 ohm tap");
    CHECK(loud * loud / 8.0 > 80.0);
    CHECK(loud * loud / 8.0 < 220.0);
    // Presence: more highs, same lows.
    CHECK(outRms(1.0, 5000.0, 0.2) > 1.5 * outRms(0.0, 5000.0, 0.2));
    CHECK(std::abs(20.0 * std::log10(outRms(1.0, 100.0, 0.2) / outRms(0.0, 100.0, 0.2))) < 1.5);
}

TEST_CASE("5150: Lead has more gain than Crunch, which has more than Rhythm; the tone stack and posts act") {
    const AmpDef def = loadEmbeddedAmp("highgain_120");
    auto level = [&](const char* jack, const char* knob, double v, double hz, double peak = 0.0005) {
        FullAmp amp(def, kFs);
        auto k = defaultKnobs(def);
        if (knob) k[def.knobIndex(knob)] = v;
        amp.setKnobs(k, 0.5);
        amp.setJack(def.jackIndex(jack));
        return rmsAt(amp, hz, peak);
    };
    const double r = level("rhythm", nullptr, 0, 500.0), c = level("crunch", nullptr, 0, 500.0), l = level("lead", nullptr, 0, 500.0);
    INFO("rhythm " << r << " crunch " << c << " lead " << l);
    CHECK(c > 3.0 * r);
    CHECK(l > 1.5 * c);
    // Pre pots of the selected channel work, and the other channel's pots do not matter.
    CHECK(level("rhythm", "rhythm_pre", 0.8, 500.0) > 1.5 * level("rhythm", "rhythm_pre", 0.3, 500.0));
    CHECK(level("lead", "lead_pre", 0.8, 500.0, 0.00005) > 1.3 * level("lead", "lead_pre", 0.3, 500.0, 0.00005));
    CHECK(std::abs(level("lead", "rhythm_pre", 0.1, 500.0, 0.00005) / level("lead", "rhythm_pre", 0.9, 500.0, 0.00005) - 1.0) < 0.15);
    // The shared tone stack on the clean channel: HIGH opens the treble, LOW the bass.
    CHECK(level("rhythm", "high", 1.0, 4000.0) > 1.3 * level("rhythm", "high", 0.0, 4000.0));
    CHECK(level("rhythm", "low", 1.0, 80.0) > 1.3 * level("rhythm", "low", 0.0, 80.0));
}

TEST_CASE("British Class A: HT chain, cathode bias and idle current are in the range of the amp; Cut and the tone controls act") {
    const AmpDef def = loadEmbeddedAmp("ac30_normal");
    {
        PowerAmp power(def, kFs);
        const auto net = parseNetlist(def.power.netlistText, def.power.netlist);
        CHECK(power.solver().dcConverged);
        auto v = [&](const char* n) { return power.solver().voltage(net.node(n)); };
        CHECK(std::abs(v("b") - 330.0) < 25.0);      // HT after the choke
                CHECK(v("kc") > 6.0);                        // shared 47 ohm cathode bias
        CHECK(v("kc") < 13.0);
        const double idleMa = (v("b") - v("pt")) / 50.0 / 2.0 * 1e3;  // per tube
        CHECK(idleMa > 30.0);
        CHECK(idleMa < 60.0);
    }
    // Cut turns the highs down and leaves the lows.
    auto outRms = [&](double cut, double hz, double peak) {
        PowerAmp p(def, kFs);
        p.setPresence(cut);
        return rmsAt(p, hz, peak);
    };
    CHECK(outRms(1.0, 6000.0, 0.05) < 0.5 * outRms(0.0, 6000.0, 0.05));
    CHECK(std::abs(20.0 * std::log10(outRms(1.0, 100.0, 0.05) / outRms(0.0, 100.0, 0.05))) < 1.5);
    // The channel pots and the tone controls.
    auto level = [&](const char* jack, const char* knob, double v, double hz, double peak = 0.0005) {
        FullAmp amp(def, kFs);
        auto k = defaultKnobs(def);
        if (knob) k[def.knobIndex(knob)] = v;
        amp.setKnobs(k, 0.0);
        amp.setJack(def.jackIndex(jack));
        return rmsAt(amp, hz, peak);
    };
    CHECK(level("normal", "normal_volume", 0.4, 500.0) > 1.5 * level("normal", "normal_volume", 0.1, 500.0));
    CHECK(level("brilliant", "brilliant_volume", 0.4, 500.0) > 1.5 * level("brilliant", "brilliant_volume", 0.1, 500.0));
    CHECK(level("brilliant", "treble", 1.0, 4000.0) > 1.3 * level("brilliant", "treble", 0.0, 4000.0));
    CHECK(level("brilliant", "bass", 1.0, 80.0) > 1.2 * level("brilliant", "bass", 0.0, 80.0));  // the Normal channel skips the tone controls;);
}

TEST_CASE("American Tweed (5E3): supply, cathode bias and idle current are in the range of the amp; the controls act") {
    const AmpDef def = loadEmbeddedAmp("tweed_5e3");
    {
        PowerAmp power(def, kFs);
        const auto net = parseNetlist(def.power.netlistText, def.power.netlist);
        CHECK(power.solver().dcConverged);
        auto v = [&](const char* n) { return power.solver().voltage(net.node(n)); };
        CHECK(std::abs(v("a") - 340.0) < 30.0);   // B+1
        CHECK(std::abs(v("bb") - 300.0) < 30.0);  // B+2, the screens
        CHECK(std::abs(v("x") - 225.0) < 30.0);   // B+3
        CHECK(std::abs(v("kcat") - 19.0) < 4.0);  // the 250 ohm cathode bias
    }
    // About 12 to 15 W at the speaker when driven hard.
    {
        PowerAmp p(def, kFs);
        const double loud = rmsAt(p, 440.0, 60.0);
        INFO("440 Hz at 60 V: " << loud << " V rms into 8 ohm");
        CHECK(loud * loud / 8.0 > 7.0);
        CHECK(loud * loud / 8.0 < 18.0);
    }
    auto level = [&](const char* jack, const char* knob, double v, double hz, double peak = 0.002) {
        FullAmp amp(def, kFs);
        auto k = defaultKnobs(def);
        if (knob) k[def.knobIndex(knob)] = v;
        amp.setKnobs(k, 0.0);
        amp.setJack(def.jackIndex(jack));
        return rmsAt(amp, hz, peak);
    };
    // The Lo jacks take about 6 dB off.
    CHECK(level("bright_hi", nullptr, 0, 500.0) > 1.6 * level("bright_lo", nullptr, 0, 500.0));
    // The volume pots and the tone control.
    CHECK(level("bright_hi", "bright_volume", 0.8, 500.0) > 1.3 * level("bright_hi", "bright_volume", 0.3, 500.0));
    CHECK(level("normal_hi", "normal_volume", 0.8, 500.0) > 1.3 * level("normal_hi", "normal_volume", 0.3, 500.0));
    CHECK(level("bright_hi", "tone", 1.0, 4000.0) > 1.3 * level("bright_hi", "tone", 0.0, 4000.0));
    // Tone down shunts the highs of both channels (the 5n leg is on the shared node); only the Bright channel feeds the 500p.
    CHECK(level("normal_hi", "tone", 0.0, 4000.0) < 0.6 * level("normal_hi", "tone", 1.0, 4000.0));
}

namespace {
// The amp with its transformer's core saturation switched off, to see what it adds.
AmpDef withoutSaturation(AmpDef def) {
    auto& t = def.power.netlistText;
    for (auto at = t.find(" SAT="); at != std::string::npos; at = t.find(" SAT=")) {
        auto end = t.find('\n', at);
        t.erase(at, end == std::string::npos ? std::string::npos : end - at);
    }
    return def;
}
}  // namespace

TEST_CASE("output transformer core saturation: the small cores flatten the lows of a hard-driven amp, the large ones barely") {
    for (const char* id : {"tweed_5e3", "ac30_normal", "jcm800_2203"}) {
        DYNAMIC_SECTION(id) {
            const AmpDef with = loadEmbeddedAmp(id);
            const AmpDef without = withoutSaturation(with);
            REQUIRE(with.power.netlistText.find(" SAT=") != std::string::npos);
            REQUIRE(without.power.netlistText.find(" SAT=") == std::string::npos);
            // Drive the power amp hard (well past its clipping point) at a low and a mid frequency.
            auto lowToMid = [&](const AmpDef& d) {
                PowerAmp p1(d, kFs), p2(d, kFs);
                const double lo = rmsAt(p1, 40.0, 40.0, 1.0), mid = rmsAt(p2, 200.0, 40.0, 1.0);
                return 20.0 * std::log10(lo / mid);
            };
            const double a = lowToMid(with), b = lowToMid(without);
            INFO(id << ": 40 Hz relative to 200 Hz, with saturation " << a << " dB, without " << b << " dB");
            CHECK(a <= b + 0.05);  // saturation never adds lows
            if (std::string(id) != "jcm800_2203") CHECK(a < b - 0.3);
        }
    }
}

TEST_CASE("tube rectifier: the 5Y3 and GZ34 supplies sag when the amp is driven, and the sag follows from the current") {
    for (const char* id : {"tweed_5e3", "ac30_normal"}) {
        DYNAMIC_SECTION(id) {
            const AmpDef def = loadEmbeddedAmp(id);
            REQUIRE(def.power.supply.tubeVolts > 0.0);
            PowerAmp p(def, kFs);
            const auto net = parseNetlist(def.power.netlistText, def.power.netlist);
            auto b = [&] { return p.solver().voltage(net.node("a")); };
            // Average B+ over a few mains cycles at rest, then while driven hard.
            auto average = [&](double volts) {
                double acc = 0;
                const std::size_t n = std::size_t(0.4 * kFs);
                for (std::size_t i = 0; i < n; ++i) {
                    p.process(volts * std::sin(2.0 * kPi * 440.0 * double(i) / kFs));
                    if (i >= n / 2) acc += b();
                }
                return acc / double(n - n / 2);
            };
            const double idle = average(0.0), driven = average(60.0);
            INFO(id << ": B+ idle " << idle << " V, driven " << driven << " V");
            CHECK(idle - driven > 3.0);   // it sags
            CHECK(idle - driven < 90.0);  // but it is not collapsing
            CHECK(std::isfinite(driven));
        }
    }
}

TEST_CASE("guitar input: a pickup's impedance makes a resonance that the cable lowers, and a pedal in front switches it off") {
    for (const char* id : {"plexi_1959", "tweed_5e3", "afd100"}) {
        DYNAMIC_SECTION(id) {
            const AmpDef def = loadEmbeddedAmp(id);
            auto level = [&](int pickup, int cable, bool pedal, double hz) {
                Preamp pre(def, kFs, def.power.supply.htDcSource * 0.8);
                pre.setGuitar(pickup, cable, pedal);
                const std::size_t n = std::size_t(0.4 * kFs);
                double acc = 0;
                for (std::size_t i = 0; i < n; ++i) {
                    const double y = pre.process(0.0004 * std::sin(2.0 * kPi * hz * double(i) / kFs));
                    REQUIRE(std::isfinite(y));
                    if (i >= n / 2) acc += y * y;
                }
                return std::sqrt(acc / double(n - n / 2));
            };
            // The highest lift over 300 Hz among a few frequencies around the resonances (2 to 4.5 kHz).
            auto peak = [&](int pickup, int cable) {
                double best = 0.0;
                for (double hz : {2000.0, 2800.0, 3500.0, 4500.0}) best = std::max(best, level(pickup, cable, false, hz) / level(pickup, cable, false, 300.0));
                return best;
            };
            const double off = peak(0, 5);
            INFO(id << ": best of 2 to 4.5 kHz over 300 Hz: off " << off << ", single coil 5 m " << peak(1, 5) << ", humbucker 5 m " << peak(2, 5));
            CHECK(peak(1, 5) > 1.3 * off);   // the resonance lifts the highs
            CHECK(peak(2, 5) > 1.1 * off);
            // A longer cable moves the resonance down: more cable, less at 6 kHz than none.
            CHECK(level(1, 14, false, 6000.0) < 0.9 * level(1, 0, false, 6000.0));
            // With a pedal in front the guitar's impedance is not there: same as "off".
            CHECK(std::abs(level(1, 5, true, 3500.0) / level(0, 5, false, 3500.0) - 1.0) < 0.01);
        }
    }
}
TEST_CASE("Triple Stack: supply, bias and the first plates are in the range the drawing shows") {
    const AmpDef def = loadEmbeddedAmp("triplestack_3ch");
    PowerAmp power(def, kFs);
    const auto net = parseNetlist(def.power.netlistText, def.power.netlist);
    CHECK(power.solver().dcConverged);
    auto v = [&](const char* n) { return power.solver().voltage(net.node(n)); };
    CHECK(std::abs(v("a") - 475.0) < 25.0);      // node A of the drawing
    CHECK(std::abs(v("cb") + 52.0) < 6.0);       // bias -52 V
    const double idleMa = (v("a") - v("pt")) / 40.0 / 2.0 * 1e3;  // per tube
    CHECK(idleMa > 28.0);
    CHECK(idleMa < 50.0);
    Preamp pre(def, kFs, power.railX());
    const auto pnet = parseNetlist(def.preamp.netlistText, def.preamp.netlist);
    auto p = [&](const char* n) { return pre.solver().voltage(pnet.node(n)); };
    const double p1a = p("p1a"), p2a = p("p2a");
    INFO("V1A plate " << p1a << " V, V2A plate " << p2a << " V");
    CHECK(std::abs(p1a - 207.0) < 25.0);
    CHECK(std::abs(p2a - 277.0) < 30.0);
}

TEST_CASE("Triple Stack: channel 1 is the clean one, channels 2 and 3 have much more gain, the tone controls and master act") {
    const AmpDef def = loadEmbeddedAmp("triplestack_3ch");
    auto level = [&](const char* jack, const char* knob, double v, double hz, double peak = 0.0005) {
        FullAmp amp(def, kFs);
        auto k = defaultKnobs(def);
        if (knob) k[def.knobIndex(knob)] = v;
        amp.setKnobs(k, 0.5);
        amp.setJack(def.jackIndex(jack));
        return rmsAt(amp, hz, peak);
    };
    const double clean = level("ch1_clean", nullptr, 0, 500.0), pushed = level("ch1_pushed", nullptr, 0, 500.0);
    const double raw2 = level("ch2_raw", nullptr, 0, 500.0), raw3 = level("ch3_raw", nullptr, 0, 500.0);
    INFO("clean " << clean << " pushed " << pushed << " ch2 " << raw2 << " ch3 " << raw3);
    CHECK(pushed > 1.3 * clean);
    CHECK(raw2 > 3.0 * clean);
    CHECK(std::abs(raw2 / raw3 - 1.0) < 0.1);
    // The gain pot and the tone stack act on the selected channel.
    for (const char* jack : {"ch1_clean", "ch2_raw", "ch3_vintage"}) {
        INFO(jack);
        CHECK(level(jack, "gain", 0.8, 500.0, 0.00005) > 1.3 * level(jack, "gain", 0.3, 500.0, 0.00005));
        CHECK(level(jack, "treble", 1.0, 4000.0) > 1.3 * level(jack, "treble", 0.0, 4000.0));
        CHECK(level(jack, "bass", 1.0, 80.0) > 1.3 * level(jack, "bass", 0.0, 80.0));
        CHECK(level(jack, "master", 0.8, 500.0, 0.00005) > 1.3 * level(jack, "master", 0.3, 500.0, 0.00005));
    }
    // Vintage is the smoother voicing: less level than Raw.
    CHECK(level("ch2_vintage", nullptr, 0, 500.0) < 0.9 * raw2);
}

TEST_CASE("Bassman 5F6-A: supply, bias and the preamp plates are in the range of the drawing; the #2 inputs are 6 dB down") {
    const AmpDef def = loadEmbeddedAmp("bassman_5f6a");
    PowerAmp power(def, kFs);
    const auto net = parseNetlist(def.power.netlistText, def.power.netlist);
    CHECK(power.solver().dcConverged);
    auto v = [&](const char* n) { return power.solver().voltage(net.node(n)); };
    CHECK(std::abs(v("a") - 450.0) < 30.0);      // +450 V after the rectifier
    CHECK(std::abs(v("bias") + 48.0) < 6.0);     // bias -48 V
    Preamp pre(def, kFs, power.railX());
    const auto pnet = parseNetlist(def.preamp.netlistText, def.preamp.netlist);
    auto p = [&](const char* n) { return pre.solver().voltage(pnet.node(n)); };
    CHECK(std::abs(p("pb") - 150.0) < 25.0);     // V1 plates +150 V
    CHECK(std::abs(p("k2a") - 1.2) < 0.4);       // V2a cathode +1.2 V
    auto level = [&](const char* jack, double peak) {
        FullAmp amp(def, kFs);
        amp.setKnobs(defaultKnobs(def), 0.5);
        amp.setJack(def.jackIndex(jack));
        return rmsAt(amp, 500.0, peak);
    };
    const double r = level("bright2", 0.0005) / level("bright1", 0.0005);
    INFO("#2 / #1 = " << r);
    CHECK(r > 0.4);
    CHECK(r < 0.62);
}

TEST_CASE("Bassman 5F6-A: the bright channel is brighter at low volume, the jumper adds gain, and the tone stack and presence act") {
    const AmpDef def = loadEmbeddedAmp("bassman_5f6a");
    auto level = [&](const char* jack, const char* knob, double v, double hz, double peak = 0.0005, double presence = 0.5) {
        FullAmp amp(def, kFs);
        auto k = defaultKnobs(def);
        if (knob) k[def.knobIndex(knob)] = v;
        amp.setKnobs(k, presence);
        amp.setJack(def.jackIndex(jack));
        return rmsAt(amp, hz, peak);
    };
    auto knobsLow = [&](const char* jack, const char* vol, double hz) { return level(jack, vol, 0.3, hz); };
    // At a low volume the 100p bright cap lifts the treble of the bright channel, not the normal one's.
    const double brightTilt = knobsLow("bright1", "bright_volume", 4000.0) / knobsLow("bright1", "bright_volume", 200.0);
    const double normalTilt = knobsLow("normal1", "normal_volume", 4000.0) / knobsLow("normal1", "normal_volume", 200.0);
    INFO("tilt bright " << brightTilt << " normal " << normalTilt);
    CHECK(brightTilt > 1.5 * normalTilt);
    CHECK(level("jumpered", nullptr, 0, 500.0) > 1.3 * level("bright1", nullptr, 0, 500.0));
    // Each volume only acts on its own channel's jack.
    CHECK(level("bright1", "bright_volume", 0.8, 500.0) > 1.5 * level("bright1", "bright_volume", 0.3, 500.0));
    CHECK(std::abs(level("bright1", "normal_volume", 0.1, 500.0) / level("bright1", "normal_volume", 0.9, 500.0) - 1.0) < 0.3);
    CHECK(level("normal1", "treble", 1.0, 4000.0) > 1.3 * level("normal1", "treble", 0.0, 4000.0));
    CHECK(level("normal1", "bass", 1.0, 80.0) > 1.3 * level("normal1", "bass", 0.0, 80.0));
}

TEST_CASE("Hiwatt DR103: supply, bias, the inverter and the preamp plates are in the range of the drawing") {
    const AmpDef def = loadEmbeddedAmp("hiwatt_dr103");
    PowerAmp power(def, kFs);
    const auto net = parseNetlist(def.power.netlistText, def.power.netlist);
    CHECK(power.solver().dcConverged);
    auto v = [&](const char* n) { return power.solver().voltage(net.node(n)); };
    CHECK(std::abs(v("a") - 475.0) < 30.0);      // HT supply 1
    CHECK(std::abs(v("bias") + 43.0) < 6.0);     // set for ~38 mA (the drawing prints -38 V)
    const double idleMa = (v("a") - v("pt")) / 30.0 / 2.0 * 1e3;  // per tube, 30 ohm per primary half
    INFO("idle " << idleMa << " mA per EL34");
    CHECK(idleMa > 30.0);
    CHECK(idleMa < 46.0);
    // The V3b follower holds the inverter's grids and cathodes near +85 V.
    CHECK(std::abs(v("nref") - 85.0) < 12.0);
    CHECK(std::abs(v("kc") - v("nref")) < 4.0);
    Preamp pre(def, kFs, power.railX());
    const auto pnet = parseNetlist(def.preamp.netlistText, def.preamp.netlist);
    auto p = [&](const char* n) { return pre.solver().voltage(pnet.node(n)); };
    INFO("V1 plate " << p("pn") << " V, V2 plate " << p("p2") << " V");
    CHECK(std::abs(p("pn") - 160.0) < 90.0);     // the drawing prints 160 V; the tube model draws less and sits higher
    CHECK(std::abs(p("p2") - 140.0) < 45.0);     // 140 V, same reason
}

TEST_CASE("Hiwatt DR103: the brilliant channel is thinner, each volume acts on its own channel, tone, master and presence act") {
    const AmpDef def = loadEmbeddedAmp("hiwatt_dr103");
    auto level = [&](const char* jack, const char* knob, double v, double hz, double peak = 0.0005, double presence = 0.5) {
        FullAmp amp(def, kFs);
        auto k = defaultKnobs(def);
        if (knob) k[def.knobIndex(knob)] = v;
        amp.setKnobs(k, presence);
        amp.setJack(def.jackIndex(jack));
        return rmsAt(amp, hz, peak);
    };
    // The 1000p coupling of the brilliant channel against the normal one's 22n: less bass.
    const double brilTilt = level("brilliant", nullptr, 0, 4000.0) / level("brilliant", nullptr, 0, 100.0);
    const double normTilt = level("normal", nullptr, 0, 4000.0) / level("normal", nullptr, 0, 100.0);
    INFO("tilt brilliant " << brilTilt << " normal " << normTilt);
    CHECK(brilTilt > 1.5 * normTilt);
    CHECK(level("brilliant", "brilliant_volume", 0.8, 500.0) > 1.5 * level("brilliant", "brilliant_volume", 0.3, 500.0));
    CHECK(std::abs(level("brilliant", "normal_volume", 0.1, 500.0) / level("brilliant", "normal_volume", 0.9, 500.0) - 1.0) < 0.3);
    CHECK(level("normal", "normal_volume", 0.8, 500.0) > 1.5 * level("normal", "normal_volume", 0.3, 500.0));
    for (const char* jack : {"brilliant", "normal"}) {
        INFO(jack);
        CHECK(level(jack, "treble", 1.0, 4000.0) > 1.3 * level(jack, "treble", 0.0, 4000.0));
        CHECK(level(jack, "bass", 1.0, 80.0) > 1.3 * level(jack, "bass", 0.0, 80.0));
        CHECK(level(jack, "middle", 1.0, 500.0) > 1.3 * level(jack, "middle", 0.0, 500.0));
        CHECK(level(jack, "master", 0.8, 500.0) > 1.3 * level(jack, "master", 0.3, 500.0));
        CHECK(level(jack, nullptr, 0, 3150.0, 0.0005, 1.0) > 1.5 * level(jack, nullptr, 0, 3150.0, 0.0005, 0.0));
    }
}

TEST_CASE("Orange OR120: supply, bias and the preamp plates are in a sane range") {
    const AmpDef def = loadEmbeddedAmp("orange_or120");
    PowerAmp power(def, kFs);
    const auto net = parseNetlist(def.power.netlistText, def.power.netlist);
    CHECK(power.solver().dcConverged);
    auto v = [&](const char* n) { return power.solver().voltage(net.node(n)); };
    CHECK(std::abs(v("a") - 475.0) < 30.0);      // A+
    CHECK(v("b") < v("a"));                      // the choke
    CHECK(v("x") < v("c"));                      // C+ then D+ through the 33k pair
    CHECK(std::abs(v("bias") + 43.0) < 6.0);
    const double idleMa = (v("a") - v("pt")) / 35.0 / 2.0 * 1e3;  // per tube, 35 ohm per primary half
    INFO("idle " << idleMa << " mA per EL34");
    CHECK(idleMa > 25.0);
    CHECK(idleMa < 45.0);
    Preamp pre(def, kFs, power.railX());
    const auto pnet = parseNetlist(def.preamp.netlistText, def.preamp.netlist);
    auto p = [&](const char* n) { return pre.solver().voltage(pnet.node(n)); };
    INFO("V1 plate " << p("p1") << " V, V2 plate " << p("p2") << " V, D+ " << p("x") << " V");
    CHECK(p("p1") > 90.0);
    CHECK(p("p1") < 200.0);
    CHECK(std::abs(p("p1") - p("p2")) < 5.0);    // the two stages are identical
}

TEST_CASE("Orange OR120: the FAC positions cut more bass, the gain, tone and boost controls act, and the feedback is negative") {
    const AmpDef def = loadEmbeddedAmp("orange_or120");
    auto level = [&](const char* jack, const char* knob, double v, double hz, double peak = 0.0002, double presence = 0.5) {
        FullAmp amp(def, kFs);
        auto k = defaultKnobs(def);
        if (knob) k[def.knobIndex(knob)] = v;
        amp.setKnobs(k, presence);
        amp.setJack(def.jackIndex(jack));
        return rmsAt(amp, hz, peak);
    };
    // The ladder of 4n7 ... 330p is a high-pass that rises: each position has less 100 Hz against 3 kHz than the one before.
    double last = 1e9;
    for (const char* jack : {"fac1", "fac2", "fac3", "fac4", "fac5", "fac6"}) {
        const double tilt = level(jack, nullptr, 0, 100.0, 0.00002) / level(jack, nullptr, 0, 3000.0, 0.00002);
        INFO(jack << " 100 Hz / 3 kHz " << tilt);
        CHECK(tilt < 1.1 * last);  // the deepest positions are down in the noise of the measurement: allow a little
        last = tilt;
    }
    CHECK(last < 0.1 * (level("fac1", nullptr, 0, 100.0, 0.00002) / level("fac1", nullptr, 0, 3000.0, 0.00002)));
    CHECK(level("fac1", "gain", 0.8, 500.0, 0.00002) > 1.5 * level("fac1", "gain", 0.3, 500.0, 0.00002));
    // The Orange tone circuit is not a Marshall's: the treble pot loads the bass network, so turned up it takes the low mids away and
    // leaves the highs (brighter tilt); the bass pot adds body around 160 Hz.
    auto tilt = [&](double treble) { return level("fac1", "treble", treble, 4000.0, 0.00002) / level("fac1", "treble", treble, 250.0, 0.00002); };
    CHECK(tilt(1.0) > 1.5 * tilt(0.0));
    CHECK(level("fac1", "bass", 1.0, 160.0, 0.00002) > 1.3 * level("fac1", "bass", 0.0, 160.0, 0.00002));
    CHECK(level("fac1", nullptr, 0, 3150.0, 0.00002, 1.0) > 1.5 * level("fac1", nullptr, 0, 3150.0, 0.00002, 0.0));
}

TEST_CASE("The reactive speaker load: the output voltage has a bass bump, a mid dip and a rising top; it can be switched off per amp") {
    auto gainDb = [&](const AmpDef& def, double hz) {
        PowerAmp pa(def, kFs);
        const std::size_t n = std::size_t(0.5 * kFs);
        double acc = 0, in = 0;
        for (std::size_t i = 0; i < n; ++i) {
            const double x = 0.05 * std::sin(2.0 * kPi * hz * double(i) / kFs);
            const double y = pa.process(x);
            REQUIRE(std::isfinite(y));
            if (i >= n / 2) { acc += y * y; in += x * x; }
        }
        return 10.0 * std::log10(acc / in);
    };
    for (const char* id : {"jcm800_2203", "plexi_1959", "tweed_5e3"}) {
        DYNAMIC_SECTION(id) {
            AmpDef flat = loadEmbeddedAmp(id);
            flat.power.speakerLoad.enabled = false;
            const AmpDef real = loadEmbeddedAmp(id);
            const double top = gainDb(real, 5000.0) - gainDb(real, 500.0), topFlat = gainDb(flat, 5000.0) - gainDb(flat, 500.0);
            const double bump = gainDb(real, 100.0) - gainDb(real, 400.0), bumpFlat = gainDb(flat, 100.0) - gainDb(flat, 400.0);
            INFO(id << ": 5k - 500 Hz " << top << " dB (flat " << topFlat << "), 100 - 400 Hz " << bump << " dB (flat " << bumpFlat << ")");
            CHECK(top > topFlat + 2.0);    // the voice coil's inductance raises the load at the top
            CHECK(bump > bumpFlat + 1.0);  // the cone resonance raises it near 80 Hz
        }
    }
}

TEST_CASE("Transformer parasitics: the primary's winding capacitance rolls the treble off, and no amp rings at rest with the feedback at its strongest") {
    // A small output transformer into a high-impedance primary (the tweed) loses the most at 20 kHz.
    auto gainDb = [&](const AmpDef& def, double hz) {
        PowerAmp pa(def, kFs);
        const std::size_t n = std::size_t(0.4 * kFs);
        double acc = 0, in = 0;
        for (std::size_t i = 0; i < n; ++i) {
            const double x = 0.05 * std::sin(2.0 * kPi * hz * double(i) / kFs);
            const double y = pa.process(x);
            if (i >= n / 2) { acc += y * y; in += x * x; }
        }
        return 10.0 * std::log10(acc / in);
    };
    AmpDef tweedOff = loadEmbeddedAmp("tweed_5e3");
    tweedOff.power.transformer.enabled = false;
    const AmpDef tweed = loadEmbeddedAmp("tweed_5e3");
    CHECK(gainDb(tweed, 20000.0) < gainDb(tweedOff, 20000.0) - 3.0);
    CHECK(std::abs(gainDb(tweed, 500.0) - gainDb(tweedOff, 500.0)) < 0.6);  // the audio band is not touched
    // Silence in, silence out: nothing oscillates, at the lowest and the highest presence.
    for (const auto& id : embeddedAmpIds()) {
        DYNAMIC_SECTION(id) {
            const AmpDef def = loadEmbeddedAmp(id);
            for (double pres : {0.0, 1.0}) {
                PowerAmp pa(def, kFs);
                pa.setPresence(pres);
                double peak = 0;
                for (std::size_t i = 0; i < std::size_t(0.3 * kFs); ++i) {
                    const double y = pa.process(0.0);
                    if (i > std::size_t(0.15 * kFs)) peak = std::max(peak, std::abs(y));
                }
                INFO(id << " presence " << pres << ": peak at rest " << peak << " V");
                CHECK(peak < 0.05);
            }
        }
    }
}

TEST_CASE("American Bass 300 (SVT): the supply, the bias and the first stages are in the range of the drawing") {
    const AmpDef def = loadEmbeddedAmp("svt_bass300");
    PowerAmp power(def, kFs);
    const auto net = parseNetlist(def.power.netlistText, def.power.netlist);
    CHECK(power.solver().dcConverged);
    auto v = [&](const char* n) { return power.solver().voltage(net.node(n)); };
    CHECK(std::abs(v("a") - 660.0) < 40.0);      // A
    CHECK(std::abs(v("x") - 462.0) < 60.0);      // B
    CHECK(std::abs(v("vs") - 350.0) < 1.0);      // the screens
    const double idleMa = (v("a") - v("pt")) / 20.0 / 3.0 * 1e3;  // per tube: 20 ohm per half, three tubes
    INFO("idle " << idleMa << " mA per 6550");
    CHECK(idleMa > 15.0);
    CHECK(idleMa < 35.0);                        // the drawing's 0.072 V across 1 ohm for three tubes is 24 mA
    Preamp pre(def, kFs, power.railX());
    const auto pnet = parseNetlist(def.preamp.netlistText, def.preamp.netlist);
    auto p = [&](const char* n) { return pre.solver().voltage(pnet.node(n)); };
    INFO("V1a plate " << p("p1") << " V, V4b cathode " << p("k4b") << " V");
    CHECK(p("rb") > 280.0);
    CHECK(p("rb") < 420.0);                      // the +300 V rail
    CHECK(std::abs(p("k4b") - 135.0) < 40.0);    // V4b's cathode follower
}

TEST_CASE("American Bass 300 (SVT): it is quiet at rest, the switches and the controls act") {
    const AmpDef def = loadEmbeddedAmp("svt_bass300");
    {
        // The preamp used to motorboat at rest through its single supply filter: with two stages it stays still.
        FullAmp amp(def, kFs);
        amp.setKnobs(defaultKnobs(def), 0.5);
        double acc = 0;
        const std::size_t n = std::size_t(1.0 * kFs);
        for (std::size_t i = 0; i < n; ++i) { const double y = amp.process(0.0); if (i > n / 2) acc += y * y; }
        CHECK(std::sqrt(acc / double(n / 2)) < 0.05);
    }
    auto level = [&](const char* knob, double v, double hz, int selector, int position, int jack = 0) {
        FullAmp amp(def, kFs);
        auto k = defaultKnobs(def);
        if (knob) k[def.knobIndex(knob)] = v;
        amp.setKnobs(k, 0.5);
        amp.setJack(std::size_t(jack));
        if (selector >= 0) amp.setSelector(std::size_t(selector), std::size_t(position));
        return rmsAt(amp, hz, 0.00003);
    };
    // Bass and treble move their own ends; the Ultra Hi switch lifts the top; Ultra Lo scoops the mids.
    CHECK(level("bass", 1.0, 40.0, -1, 0) > 1.5 * level("bass", 0.0, 40.0, -1, 0));
    CHECK(level("treble", 1.0, 4000.0, -1, 0) > 1.2 * level("treble", 0.0, 4000.0, -1, 0));
    CHECK(level(nullptr, 0, 3200.0, 1, 1) > 1.3 * level(nullptr, 0, 3200.0, 1, 0));   // ultra hi
    CHECK(level(nullptr, 0, 400.0, 0, 1) < 0.85 * level(nullptr, 0, 400.0, 0, 0));    // ultra lo (the twin-T notch)
    CHECK(level(nullptr, 0, 5000.0, 0, 2) > 1.5 * level(nullptr, 0, 40.0, 0, 2));     // bass cut leaves the top
    // Channel 1 has two more gain stages than channel 2.
    CHECK(level(nullptr, 0, 500.0, -1, 0, 0) > 3.0 * level(nullptr, 0, 500.0, -1, 0, 2));
}
