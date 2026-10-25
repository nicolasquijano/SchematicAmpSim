#include <catch2/catch_test_macros.hpp>
#include <cstring>

#include "sas/version.h"

TEST_CASE("engine links and reports a version") {
    REQUIRE(std::strlen(sas::engineVersion()) > 0);
}
