#include <catch2/catch_test_macros.hpp>
#include <cctype>
#include <string>

#include "sas/version.h"
#include "sas/version_info.h"

TEST_CASE("the version comes from CMake and looks like major.minor.patch[-suffix]") {
    const std::string v = sas::engineVersion();
    CHECK(v == SAS_VERSION_STRING);
    REQUIRE(v.size() >= 5);
    CHECK(std::isdigit(static_cast<unsigned char>(v[0])));
    CHECK(std::count(v.begin(), v.end(), '.') == 2);
    CHECK(v.rfind(std::to_string(SAS_VERSION_MAJOR) + "." + std::to_string(SAS_VERSION_MINOR) + "." + std::to_string(SAS_VERSION_PATCH), 0) == 0);
}
