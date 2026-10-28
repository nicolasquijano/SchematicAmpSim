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

# ASIO (Windows, low-latency audio for the standalone application). Steinberg's ASIO SDK is offered under the Steinberg ASIO License or
# under the GPL version 3; this project uses it under the GPL version 3 (the project is AGPL-3.0, which allows it). The SDK is fetched
# at configure time into third_party/ and is never copied into the repository. Turn it off with -DSAS_ENABLE_ASIO=OFF.
option(SAS_ENABLE_ASIO "Build the standalone application with ASIO support (Windows)" ON)
set(SAS_ASIO_SDK_DIR "")
if(WIN32 AND SAS_ENABLE_ASIO)
  FetchContent_Declare(asiosdk
    URL https://download.steinberg.net/sdk_downloads/ASIO-SDK_2.3.4_2025-10-15.zip
    URL_HASH SHA256=d5ebf0c20dd2c5f43771fd0c1418f4b361bf52434ee670097cfa6b3a335e2eca)
  FetchContent_MakeAvailable(asiosdk)
  set(SAS_ASIO_SDK_DIR "${asiosdk_SOURCE_DIR}/common")
  if(NOT EXISTS "${SAS_ASIO_SDK_DIR}/iasiodrv.h")
    message(FATAL_ERROR "the ASIO SDK was fetched but ${SAS_ASIO_SDK_DIR}/iasiodrv.h is missing")
  endif()
endif()

include(CTest)

add_subdirectory(engine)
add_subdirectory(amps)
add_subdirectory(plugin)
add_subdirectory(tests)
add_subdirectory(tools)
