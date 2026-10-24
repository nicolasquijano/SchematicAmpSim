#include <catch2/catch_test_macros.hpp>

#include "aes/embedded_amps.h"
#include "aes/preamp.h"
#include "amp_fixture.h"

TEST_CASE("embedded amps are the files on disk") {
    const auto ids = aes::embeddedAmpIds();
    REQUIRE(!ids.empty());
    const auto embedded = aes::loadEmbeddedAmp("jcm800_2203");
    const auto& disk = jcm800();
    CHECK(embedded.id == disk.id);
    CHECK(embedded.preamp.netlistText == disk.preamp.netlistText);
    CHECK(embedded.power.netlistText == disk.power.netlistText);
    CHECK(embedded.preamp.knobs.size() == disk.preamp.knobs.size());
    CHECK_THROWS(aes::loadEmbeddedAmp("nope"));
}

TEST_CASE("malformed amp.json reports the offending key") {
    auto read = [](const std::string&) { return std::string(); };
    try {
        aes::loadAmpDef(R"({"id":"x","name":"y","preamp":{}})", read);
        FAIL("should have thrown");
    } catch (const std::exception& e) {
        CHECK(std::string(e.what()).find("netlist") != std::string::npos);
    }
    CHECK_THROWS(aes::loadAmpDef("{ not json", read));
}
