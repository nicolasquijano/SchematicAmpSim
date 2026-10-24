#include <catch2/catch_test_macros.hpp>
#include <cmath>

#include "aes/netlist.h"

using namespace aes;

TEST_CASE("expressions: SI suffixes parse like C++ literals") {
    CHECK(evalExpression("5.6k") == 5.6e3);
    CHECK(evalExpression("0.022u") == 0.022e-6);
    CHECK(evalExpression("470p") == 470e-12);
    CHECK(evalExpression("1M") == 1e6);
    CHECK(evalExpression("100e-12") == 100e-12);
    CHECK(evalExpression("5.6k/2") == 5.6e3 / 2.0);
    CHECK(evalExpression("2*(3+4)-1") == 13.0);
    CHECK(evalExpression("-sqrt(16)+exp(0)") == -3.0);
    CHECK(evalExpression("a*2", {{"a", 21.0}}) == 42.0);
}

TEST_CASE("expressions reject garbage") {
    CHECK_THROWS_AS(evalExpression("2+"), NetlistError);
    CHECK_THROWS_AS(evalExpression("(1"), NetlistError);
    CHECK_THROWS_AS(evalExpression("nope"), NetlistError);
    CHECK_THROWS_AS(evalExpression("3q"), NetlistError);
    CHECK_THROWS_AS(evalExpression("foo(1)"), NetlistError);
}

TEST_CASE("netlist builds the same RC circuit as the hand-built one") {
    auto nl = parseNetlist(R"(
        * RC low-pass
        .param tau_c = 10n
        I VIN a 10k
        V VCC vcc 1 0
        R RLOAD vcc 0 1k      ; keeps the supply connected
        C C1 a gnd tau_c
    )");
    const double fs = 192000.0;
    const Node a = nl.node("a");
    Solver s(std::move(nl.circuit), fs);
    for (int n = 1; n <= 400; ++n) {
        s.process(1.0);
        const double want = 1.0 - std::exp(-(n - 0.5) / fs / 100e-6);
        REQUIRE(std::abs(s.voltage(a) - want) < 2e-3);
    }
}

TEST_CASE("netlist: tubes, nodes order, names and transformers") {
    auto nl = parseNetlist(R"(
        .nodes vcc g p k
        V VB vcc 1 300
        I VIN g 1M
        R RP vcc p 100k
        R RK k 0 1.5k
        R RG g 0 1M
        T V1 g p k ECC83
        .param n = 2
        .xfmr OT L=2
          W p 0 1 pri
          W k 0 n sec
          K pri sec 0.999
        .end
    )");
    CHECK(nl.node("vcc") == 1);
    CHECK(nl.node("k") == 4);
    CHECK(nl.resistors.count("RP") == 1);
    CHECK_NOTHROW(nl.source("VIN"));
    Solver s(std::move(nl.circuit), 48000.0);
    CHECK(s.dcConverged);
}

TEST_CASE("netlist errors carry file and line") {
    try {
        parseNetlist("R R1 a b 1k\nR R2 a b\n", "x.net");
        FAIL("should have thrown");
    } catch (const NetlistError& e) {
        CHECK(std::string(e.what()).find("x.net:2") != std::string::npos);
    }
    CHECK_THROWS_AS(parseNetlist("T V1 a b c NOPE"), NetlistError);
    CHECK_THROWS_AS(parseNetlist("R R1 a b 1k\nR R1 a b 2k"), NetlistError);
    CHECK_THROWS_AS(parseNetlist("R R1 a b -5"), NetlistError);
    CHECK_THROWS_AS(parseNetlist(".xfmr T L=1\n W a 0 1 p\n"), NetlistError);
    CHECK_THROWS_AS(parseNetlist("Z what"), NetlistError);
}
