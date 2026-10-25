#include <catch2/catch_test_macros.hpp>

#include "sas/embedded_amps.h"
#include "sas/preamp.h"
#include "amp_fixture.h"

TEST_CASE("embedded amps are the files on disk") {
    const auto ids = sas::embeddedAmpIds();
    REQUIRE(!ids.empty());
    const auto embedded = sas::loadEmbeddedAmp("jcm800_2203");
    const auto disk = sas::loadAmpDefFromFolder(std::string(SAS_AMPS_DIR) + "/jcm800_2203");  // not the parity fixture, which swaps the tube model
    CHECK(embedded.id == disk.id);
    CHECK(embedded.preamp.netlistText == disk.preamp.netlistText);
    CHECK(embedded.power.netlistText == disk.power.netlistText);
    CHECK(embedded.preamp.knobs.size() == disk.preamp.knobs.size());
    CHECK_THROWS(sas::loadEmbeddedAmp("nope"));
}

TEST_CASE("malformed amp.json reports the offending key") {
    auto read = [](const std::string&) { return std::string(); };
    try {
        sas::loadAmpDef(R"({"id":"x","name":"y","preamp":{}})", read);
        FAIL("should have thrown");
    } catch (const std::exception& e) {
        CHECK(std::string(e.what()).find("netlist") != std::string::npos);
    }
    CHECK_THROWS(sas::loadAmpDef("{ not json", read));
}
