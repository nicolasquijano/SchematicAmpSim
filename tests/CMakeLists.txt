add_executable(sas_tests
  test_smoke.cpp
  test_linalg.cpp
  test_devices.cpp
  test_circuit.cpp
  test_oversample_cab.cpp
  test_preamp.cpp
  test_amp.cpp
  test_netlist.cpp
  test_embedded.cpp
  test_chain.cpp
  test_amps_generic.cpp
  test_split.cpp
  test_tables.cpp
  test_gate.cpp
  test_condition.cpp
  test_speaker.cpp
  test_compressor.cpp
  test_twin_fx.cpp
  test_tuner.cpp
  test_stereo.cpp
  test_version.cpp)
target_compile_definitions(sas_tests PRIVATE
  SAS_GOLDEN_DIR="${CMAKE_SOURCE_DIR}/golden"
  SAS_AMPS_DIR="${CMAKE_SOURCE_DIR}/amps"
  SAS_TEST_DATA_DIR="${CMAKE_SOURCE_DIR}/tests/data")
target_link_libraries(sas_tests PRIVATE sas_engine sas_amps Catch2::Catch2WithMain)

# Processor tests need the plugin's shared code (JUCE); kept as a separate executable so the
# engine tests stay JUCE-free.
add_executable(sas_plugin_tests plugin_tests.cpp)
target_include_directories(sas_plugin_tests PRIVATE ${CMAKE_SOURCE_DIR}/plugin)
target_link_libraries(sas_plugin_tests PRIVATE SchematicAmpSim sas_engine sas_amps juce::juce_audio_utils juce::juce_audio_formats
  juce::juce_dsp juce::juce_recommended_config_flags Catch2::Catch2WithMain)
target_compile_definitions(sas_plugin_tests PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)

list(APPEND CMAKE_MODULE_PATH "${catch2_SOURCE_DIR}/extras")
include(Catch)
catch_discover_tests(sas_tests)
catch_discover_tests(sas_plugin_tests TEST_PREFIX "plugin: ")
