add_executable(aes_tests
  test_smoke.cpp
  test_linalg.cpp
  test_devices.cpp
  test_circuit.cpp
  test_oversample_cab.cpp
  test_preamp.cpp
  test_amp.cpp
  test_netlist.cpp
  test_embedded.cpp)
target_compile_definitions(aes_tests PRIVATE
  AES_GOLDEN_DIR="${CMAKE_SOURCE_DIR}/golden"
  AES_AMPS_DIR="${CMAKE_SOURCE_DIR}/amps")
target_link_libraries(aes_tests PRIVATE aes_engine aes_amps Catch2::Catch2WithMain)

list(APPEND CMAKE_MODULE_PATH "${catch2_SOURCE_DIR}/extras")
include(Catch)
catch_discover_tests(aes_tests)
