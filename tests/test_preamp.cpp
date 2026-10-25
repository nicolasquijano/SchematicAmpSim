#include <catch2/catch_test_macros.hpp>
#include <cmath>

#include "sas/preamp.h"
#include "amp_fixture.h"
#include "golden_io.h"

using namespace sas;

TEST_CASE("preamp DC operating point matches Rust to 1e-6 V") {
    const auto& def = jcm800();
    Preamp pre(def, 96000.0, 410.0);
    REQUIRE(pre.solver().dcConverged);
    const auto ref = golden::loadDc("dc_preamp.txt");
    REQUIRE(ref.size() == pre.solver().nodeCount());
    // dump_golden writes voltage(k) for k = 0..nodeCount-1 (k = 0 is ground).
    for (std::size_t i = 0; i < ref.size(); ++i) {
        INFO("node " << i);
        CHECK(std::abs(pre.solver().voltage(i) - ref[i]) < 1e-6);
    }
}

TEST_CASE("preamp transient matches Rust") {
    const auto& def = jcm800();
    for (const char* name : {"preamp_small", "preamp_mid"}) {
        const auto c = golden::load(name);
        Preamp pre(def, c.num("fs"), c.num("rail"));
        pre.setKnobs(knobsOf(def, c));
        std::vector<double> y;
        for (double x : c.in) y.push_back(pre.process(x));
        const auto cmp = golden::compare(y, c.out);
        INFO(name << ": error " << cmp.errDb() << " dB, failures " << pre.solver().newtonFailures);
        CHECK(pre.solver().newtonFailures == 0);
        CHECK(cmp.errDb() < -120.0);
    }
}

TEST_CASE("knob and jack names resolve, bad names are rejected") {
    const auto& def = jcm800();
    CHECK(def.knobIndex("volume") == 0);
    CHECK(def.jackIndex("low") == 1);
    CHECK_THROWS(def.knobIndex("gain"));
    Preamp pre(def, 96000.0, 410.0);
    CHECK(pre.knobCount() == 5);
    CHECK_THROWS(pre.setKnobs({0.1, 0.2}));
}
