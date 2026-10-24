#include <catch2/catch_test_macros.hpp>
#include <cstring>

#include "aes/version.h"

TEST_CASE("engine links and reports a version") {
    REQUIRE(std::strlen(aes::engineVersion()) > 0);
}
