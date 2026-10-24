#include <catch2/catch_test_macros.hpp>
#include <cmath>

#include "aes/preamp2203.h"
#include "golden_io.h"

using namespace aes;

TEST_CASE("preamp DC operating point matches Rust to 1e-6 V") {
    Preamp2203 pre(96000.0, kDefaultRailVolts, TriodeModel::ecc83());
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
    for (const char* name : {"preamp_small", "preamp_mid"}) {
        const auto c = golden::load(name);
        Preamp2203 pre(c.num("fs"), c.num("rail"), TriodeModel::ecc83());
        pre.setControls({c.num("volume"), c.num("bass"), c.num("middle"), c.num("treble"), c.num("master")});
        std::vector<double> y;
        for (double x : c.in) y.push_back(pre.process(x));
        const auto cmp = golden::compare(y, c.out);
        INFO(name << ": error " << cmp.errDb() << " dB, failures " << pre.solver().newtonFailures);
        CHECK(pre.solver().newtonFailures == 0);
        CHECK(cmp.errDb() < -120.0);
    }
}
