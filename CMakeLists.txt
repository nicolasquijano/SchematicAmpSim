cmake_minimum_required(VERSION 3.25)

# macOS: one binary for Apple Silicon and Intel, for macOS 11 and later. These must be set before project().
if(APPLE)
  set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "Architectures of the macOS binaries")
  set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Oldest macOS the plugin runs on")
endif()

project(SchematicAmpSim VERSION 0.2.0 LANGUAGES C CXX)

# The version lives here and nowhere else: bump VERSION above (and the suffix below) for a release, see docs/RELEASING.md.
# "-beta" while the saved-project format may still change; empty for a stable 1.0 and later.
set(SAS_VERSION_SUFFIX "-beta")
set(SAS_VERSION_STRING "${PROJECT_VERSION}${SAS_VERSION_SUFFIX}")
configure_file("${CMAKE_SOURCE_DIR}/cmake/version_info.h.in" "${CMAKE_BINARY_DIR}/generated/sas/version_info.h" @ONLY)

set(CMAKE_CXX_STANDARD 20)
# Strict IEEE arithmetic everywhere, like /fp:precise on MSVC (never /fp:fast): the parity tests against the Rust reference and the
# stability of the Newton solver depend on it. clang on Apple Silicon fuses a * b + c into one rounding by default; turn that off.
if(NOT MSVC)
  add_compile_options(-ffp-contract=off)
endif()
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set_property(GLOBAL PROPERTY USE_FOLDERS ON)

# Third-party sources live next to the project (and out of git) so wiping `build/` does not
# trigger a new download.
include(FetchContent)
set(FETCHCONTENT_BASE_DIR "${CMAKE_SOURCE_DIR}/third_party" CACHE PATH "" FORCE)

FetchContent_Declare(JUCE
  GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
  GIT_TAG        8.0.16-security
  GIT_SHALLOW    TRUE)
FetchContent_Declare(Catch2
  GIT_REPOSITORY https://github.com/catchorg/Catch2.git
  GIT_TAG        v3.16.1
  GIT_SHALLOW    TRUE)
FetchContent_Declare(nlohmann_json
  GIT_REPOSITORY https://github.com/nlohmann/json.git
  GIT_TAG        v3.11.3
  GIT_SHALLOW    TRUE)
FetchContent_MakeAvailable(JUCE Catch2 nlohmann_json)

include(CTest)

add_subdirectory(engine)
add_subdirectory(amps)
add_subdirectory(plugin)
add_subdirectory(tests)
add_subdirectory(tools)
