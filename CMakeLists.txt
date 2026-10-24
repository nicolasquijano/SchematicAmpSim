cmake_minimum_required(VERSION 3.25)
project(AES VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
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
FetchContent_MakeAvailable(JUCE Catch2)

include(CTest)

add_subdirectory(engine)
add_subdirectory(plugin)
add_subdirectory(tests)
add_subdirectory(tools)
