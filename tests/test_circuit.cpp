#include <catch2/catch_test_macros.hpp>
#include <cmath>

#include "aes/circuit.h"

using namespace aes;

TEST_CASE("RC step response matches the analytic solution") {
    Circuit c;
    const Node a = c.node("a"), vcc = c.node("vcc");
    c.input(a, 10'000.0);
    c.supply(vcc, 0.0, 1.0);
    c.resistor(vcc, GND, 1e3);
    c.capacitor(a, GND, 10e-9);  // tau = 100 us
    const double fs = 192'000.0, tau = 100e-6;
    Solver s(std::move(c), fs);
    for (int n = 1; n <= 400; ++n) {
        s.process(1.0);
        // Trapezoidal integration sees the step arriving half a sample earlier.
        const double want = 1.0 - std::exp(-(n - 0.5) / fs / tau);
        REQUIRE(std::abs(s.voltage(a) - want) < 2e-3);
    }
}

TEST_CASE("coupled inductors act as a transformer") {
    Circuit c;
    const Node pri = c.node("pri"), sec = c.node("sec");
    c.input(pri, 10.0);
    const double l1 = 2.0, l2 = 8.0, k = 0.9995, m = k * std::sqrt(l1 * l2);
    c.coupledInductors({{pri, GND}, {sec, GND}}, {l1, m, m, l2});
    c.resistor(sec, GND, 1000.0);
    const double fs = 96'000.0;
    Solver s(std::move(c), fs);
    double peakIn = 0, peakOut = 0;
    for (int n = 0; n < int(0.2 * fs); ++n) {
        s.process(std::sin(2.0 * 3.14159265358979 * 1000.0 * n / fs));
        if (n > int(0.1 * fs)) {
            peakIn = std::max(peakIn, std::abs(s.voltage(pri)));
            peakOut = std::max(peakOut, std::abs(s.voltage(sec)));
        }
    }
    REQUIRE(std::abs(peakOut / peakIn - 2.0) < 0.08);
}

TEST_CASE("common-cathode DC point satisfies KCL with the tube model") {
    const auto model = TriodeModel::ecc83();
    Circuit c;
    const Node vcc = c.node("vcc"), g = c.node("g"), p = c.node("p"), k = c.node("k");
    c.supply(vcc, 300.0, 1.0);
    c.input(g, 1e6);
    c.resistor(vcc, p, 100e3);
    c.resistor(k, GND, 1.5e3);
    c.resistor(g, GND, 1e6);
    c.triode(g, p, k, model);
    Solver s(std::move(c), 48'000.0);
    REQUIRE(s.dcConverged);
    const double vp = s.voltage(p), vk = s.voltage(k), vg = s.voltage(g);
    const double ip = model.plate(vg - vk, vp - vk).ip;
    REQUIRE(std::abs(ip - (300.0 - vp) / 100e3) < 1e-7);
    REQUIRE(std::abs(vk - ip * 1.5e3) < 1e-3);
    REQUIRE((vp > 100.0 && vp < 280.0));
}
