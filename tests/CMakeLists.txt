add_executable(aes_tests
  test_smoke.cpp
  test_linalg.cpp
  test_devices.cpp
  test_circuit.cpp
  test_oversample_cab.cpp
  test_preamp.cpp
  test_amp.cpp
  test_netlist.cpp
  test_embedded.cpp
  test_chain.cpp)
target_compile_definitions(aes_tests PRIVATE
  AES_GOLDEN_DIR="${CMAKE_SOURCE_DIR}/golden"
  AES_AMPS_DIR="${CMAKE_SOURCE_DIR}/amps")
target_link_libraries(aes_tests PRIVATE aes_engine aes_amps Catch2::Catch2WithMain)

# Processor tests need the plugin's shared code (JUCE); kept as a separate executable so the
# engine tests stay JUCE-free.
add_executable(aes_plugin_tests plugin_tests.cpp)
target_include_directories(aes_plugin_tests PRIVATE ${CMAKE_SOURCE_DIR}/plugin)
target_link_libraries(aes_plugin_tests PRIVATE AES aes_engine aes_amps juce::juce_audio_utils juce::juce_audio_formats
  juce::juce_dsp juce::juce_recommended_config_flags Catch2::Catch2WithMain)
target_compile_definitions(aes_plugin_tests PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)

list(APPEND CMAKE_MODULE_PATH "${catch2_SOURCE_DIR}/extras")
include(Catch)
catch_discover_tests(aes_tests)
catch_discover_tests(aes_plugin_tests TEST_PREFIX "plugin: ")
