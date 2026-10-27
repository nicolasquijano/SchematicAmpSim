#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <vector>

#include "sas/chain.h"
#include "sas/circuit.h"
#include "sas/compressor.h"
#include "sas/devices.h"
#include "sas/embedded_amps.h"
#include "sas/netlist.h"

using namespace sas;

TEST_CASE("Ebers-Moll NPN: derivatives match finite differences and the junction currents follow the model") {
    const auto m = NpnModel::mmbt6429();
    for (double vbe : {-0.5, 0.2, 0.55, 0.62, 0.7}) {
        for (double vbc : {-5.0, -0.3, 0.2, 0.5, 0.65}) {
            const auto e = m.eval(vbe, vbc);
            const double h = 1e-6;
            const double dgf = (m.eval(vbe + h, vbc).jf - m.eval(vbe - h, vbc).jf) / (2 * h);
            const double dgr = (m.eval(vbe, vbc + h).jr - m.eval(vbe, vbc - h).jr) / (2 * h);
            CHECK(e.gf == Catch::Approx(dgf).epsilon(1e-4).margin(1e-12));
            CHECK(e.gr == Catch::Approx(dgr).epsilon(1e-4).margin(1e-12));
        }
    }
}

TEST_CASE("OTA: derivatives match finite differences") {
    const auto m = OtaModel::lm13700();
    for (double vd : {-0.2, -0.03, 0.0, 0.01, 0.1}) {
        for (double v1 : {1.0, 1.15, 1.25}) {
            const auto e = m.eval(vd, v1);
            const double h = 1e-6;
            CHECK(e.dvd == Catch::Approx((m.eval(vd + h, v1).iout - m.eval(vd - h, v1).iout) / (2 * h)).epsilon(1e-4).margin(1e-9));
            CHECK(e.dv1 == Catch::Approx((m.eval(vd, v1 + h).iout - m.eval(vd, v1 - h).iout) / (2 * h)).epsilon(1e-4).margin(1e-9));
            CHECK(e.g1 == Catch::Approx((m.eval(vd, v1 + h).i1 - m.eval(vd, v1 - h).i1) / (2 * h)).epsilon(1e-4).margin(1e-12));
        }
    }
}

TEST_CASE("netlist N element: a hard-driven transistor saturates, a lightly driven one stays active") {
    auto vce = [](double baseOhms) {
        auto net = parseNetlist(
            "V VCC vcc 1 5\n"
            "R RC vcc c 1k\n"
            "V VB vb 1 5\n"
            "R RB vb b " + std::to_string(baseOhms) + "\n"
            "N Q1 b c 0 MMBT6429\n");
        Solver s(std::move(net.circuit), 48000.0);
        REQUIRE(s.dcConverged);
        return s.voltage(net.node("c"));
    };
    const double sat = vce(10e3);   // 0.43 mA of base current: far more than the 4.7 mA collector current needs
    const double active = vce(4e6); // ~1 uA: beta * ib is 0.5 mA, the collector sits well above the emitter
    INFO("saturated Vce " << sat << ", active Vce " << active);
    CHECK(sat > 0.0);
    CHECK(sat < 0.35);   // a real saturation voltage, not the negative Vce a forward-active model would give
    CHECK(active > 3.5);
    CHECK(active < 4.9);
}

TEST_CASE("netlist A element: the OTA output current is the control current times tanh(vd / 2 vt)") {
    auto run = [](double vd) {
        auto net = parseNetlist(
            "V VCC vcc 1 5\n"
            "R RCTL vcc ctl 47k\n"     // the bias current: about (5 - 1.2) / 47k = 80 uA
            "V VP p 1 " + std::to_string(vd) + "\n"
            "R RO n 0 10k\n"
            "A U1 p 0 n ctl LM13700\n");
        Solver s(std::move(net.circuit), 48000.0);
        REQUIRE(s.dcConverged);
        const double v1 = s.voltage(net.node("ctl"));
        const double i1 = (5.0 - v1) / 47e3;
        return std::pair{s.voltage(net.node("n")), i1};
    };
    const auto small = run(0.010);
    const auto big = run(0.200);
    const auto neg = run(-0.010);
    INFO("control pin current " << small.second << " A");
    CHECK(small.second > 60e-6);
    CHECK(small.second < 100e-6);  // set by the resistor against the ~1.2 V of the two junctions
    CHECK(small.first == Catch::Approx(small.second * std::tanh(0.010 / (2 * 0.02585)) * 10e3).epsilon(0.02));
    CHECK(big.first == Catch::Approx(big.second * 10e3).epsilon(0.02));  // saturated: the full control current
    CHECK(neg.first == Catch::Approx(-small.first).epsilon(0.02));      // odd symmetry
}

TEST_CASE("compressor: the operating point is what the schematic implies") {
    Compressor c(48000.0);
    const auto& s = c.solver();
    (void)s;
    WARN("envelope " << c.envelope() << " V, OTA bias " << c.otaBiasAmps() * 1e6 << " uA, newton failures " << c.solver().newtonFailures);
    CHECK(c.envelope() > 8.0);      // idle: C6 is charged to about the supply
    CHECK(c.envelope() < 9.01);
    CHECK(c.otaBiasAmps() > 5e-6);
    CHECK(c.otaBiasAmps() < 400e-6);
}

namespace {

constexpr double kFs = 48000.0;
constexpr double kPi = 3.14159265358979323846;

// Gain in dB of a sine of `amp` volts (peak) at `hz`, measured over the last quarter of `seconds`.
double gainDb(const CompControls& k, double amp, double hz = 440.0, double seconds = 2.0) {
    Compressor c(kFs);
    c.setControls(k);
    const std::size_t n = std::size_t(seconds * kFs);
    double acc = 0;
    std::size_t cnt = 0;
    for (std::size_t i = 0; i < n; ++i) {
        const double y = c.process(amp * std::sin(2.0 * kPi * hz * double(i) / kFs));
        if (i > n * 3 / 4) { acc += y * y; ++cnt; }
    }
    return 20.0 * std::log10(std::sqrt(acc / double(cnt)) / (amp / std::sqrt(2.0)));
}

}  // namespace

TEST_CASE("compressor: the compressed path squashes a wide input range into a narrow output range") {
    CompControls k;
    k.blend = 1.0;  // compressed path only
    k.sustain = 0.5;
    const double quiet = gainDb(k, 0.005), loud = gainDb(k, 0.5);
    // Output level (dB) = input level + gain: 40 dB more input must come out as far less than 40 dB more output.
    const double inputRise = 20.0 * std::log10(0.5 / 0.005);
    const double outputRise = inputRise + (loud - quiet);
    INFO("gain quiet " << quiet << " dB, loud " << loud << " dB; 40 dB more input gives " << outputRise << " dB more output");
    CHECK(quiet > loud + 15.0);
    CHECK(outputRise < 12.0);
    CHECK(outputRise > 0.0);  // still monotonic
}

TEST_CASE("compressor: sustain raises the gain of quiet notes, and blend 0 is a level-independent dry path") {
    CompControls lo, hi;
    lo.blend = hi.blend = 1.0;
    lo.sustain = 0.2;
    hi.sustain = 0.8;
    CHECK(gainDb(hi, 0.005) > gainDb(lo, 0.005) + 10.0);

    CompControls dry;
    dry.blend = 0.0;
    const double a = gainDb(dry, 0.02), b = gainDb(dry, 0.3);
    INFO("dry gain " << a << " dB at 20 mV, " << b << " dB at 300 mV");
    CHECK(std::abs(a - b) < 1.0);   // not compressed
    CHECK(a > -12.0);
    CHECK(a < -5.0);                // ~ -8.7 dB: IC2A, the blend network and the Level pot at mid rotation (about 15 % of its resistance)
}

TEST_CASE("compressor: the Attack switch decides how fast the envelope recovers") {
    auto envelopeAfter = [](bool fast) {
        Compressor c(kFs);
        CompControls k;
        k.fastRelease = fast;
        c.setControls(k);
        // A loud burst pulls the envelope down; then silence.
        for (std::size_t i = 0, n = std::size_t(0.5 * kFs); i < n; ++i) c.process(0.5 * std::sin(2.0 * kPi * 220.0 * double(i) / kFs));
        const double low = c.envelope();
        for (std::size_t i = 0, n = std::size_t(0.3 * kFs); i < n; ++i) c.process(0.0);
        return std::pair{low, c.envelope()};
    };
    const auto slow = envelopeAfter(false), fast = envelopeAfter(true);
    INFO("slow: " << slow.first << " -> " << slow.second << " V; fast: " << fast.first << " -> " << fast.second << " V");
    CHECK(slow.first < 3.0);
    CHECK(fast.second > slow.second + 1.0);
    CHECK(fast.second > 7.0);  // 0.3 s is three time constants of R20 x C6
}

TEST_CASE("compressor: stays finite and converges on hard, rough input") {
    Compressor c(kFs);
    CompControls k;
    k.sustain = 1.0;
    k.blend = 0.5;
    k.tone = 1.0;
    k.level = 1.0;
    c.setControls(k);
    unsigned state = 99;
    for (std::size_t i = 0, n = std::size_t(2.0 * kFs); i < n; ++i) {
        state = state * 1664525u + 1013904223u;
        const double noise = double(state >> 8) / double(1u << 24) - 0.5;
        const double burst = (i / 8000) % 2 ? 2.0 : 0.02;  // 2 V peaks next to near silence
        const double y = c.process(burst * (0.7 * std::sin(2.0 * kPi * 330.0 * double(i) / kFs) + 0.6 * noise));
        REQUIRE(std::isfinite(y));
    }
    CHECK(c.solver().newtonFailures == 0);
    // Turning every knob mid-signal is safe too.
    for (int step = 0; step < 50; ++step) {
        k.sustain = (step % 7) / 6.0;
        k.blend = (step % 5) / 4.0;
        k.level = (step % 3) / 2.0;
        k.fastRelease = step % 2;
        c.setControls(k);
        for (int i = 0; i < 480; ++i) REQUIRE(std::isfinite(c.process(0.1 * std::sin(0.05 * i))));
    }
}

TEST_CASE("compressor in the chain: off changes nothing, on narrows the dynamics") {
    const auto def = loadEmbeddedAmp("twin_65");
    auto level = [&](double amp, bool on) {
        Chain chain(def, kFs);
        ChainSettings s;
        for (const auto& k : def.preamp.knobs) s.knobs.push_back(k.def);
        for (const auto& k : def.power.knobs) s.powerKnobs.push_back(k.def);
        s.presence = def.power.presenceDefault;
        s.quality = Quality::Lite;
        s.cabFilter = false;
        s.compOn = on;
        s.comp.sustain = 0.5;
        s.comp.blend = 1.0;
        s.comp.level = 0.76;
        chain.snap(s);
        std::vector<float> in(std::size_t(2.0 * kFs)), out(in.size());
        for (std::size_t i = 0; i < in.size(); ++i) in[i] = float(amp * std::sin(2.0 * kPi * 220.0 * double(i) / kFs));
        chain.processAmp(in.data(), out.data(), in.size());
        double acc = 0;
        for (std::size_t i = out.size() * 3 / 4; i < out.size(); ++i) {
            REQUIRE(std::isfinite(out[i]));
            acc += double(out[i]) * out[i];
        }
        return 10.0 * std::log10(acc / double(out.size() / 4));
    };
    const double offRange = level(0.2, false) - level(0.02, false);
    const double onRange = level(0.2, true) - level(0.02, true);
    INFO("a 20 dB louder note comes out " << offRange << " dB louder without the compressor and " << onRange << " dB with it");
    CHECK(offRange > 10.0);
    CHECK(onRange < offRange - 5.0);
}
