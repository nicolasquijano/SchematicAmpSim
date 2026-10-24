add_executable(aes_tests
  test_smoke.cpp)
target_link_libraries(aes_tests PRIVATE aes_engine Catch2::Catch2WithMain)

list(APPEND CMAKE_MODULE_PATH "${catch2_SOURCE_DIR}/extras")
include(Catch)
catch_discover_tests(aes_tests)
