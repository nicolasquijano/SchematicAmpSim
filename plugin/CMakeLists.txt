juce_add_plugin(AES
  COMPANY_NAME "Nico"
  PLUGIN_MANUFACTURER_CODE Nico
  PLUGIN_CODE Aes1
  FORMATS VST3
  # Visible name (bundle file and name in the DAW). The VST3 class id comes from the manufacturer code
  # and PLUGIN_CODE above, not from this name, so renaming it keeps saved projects working; the
  # CMake target stays `AES`.
  PRODUCT_NAME "Schematic Amp Sim"
  PLUGIN_NAME "Schematic Amp Sim"
  IS_SYNTH FALSE
  NEEDS_MIDI_INPUT FALSE
  NEEDS_MIDI_OUTPUT FALSE
  COPY_PLUGIN_AFTER_BUILD FALSE
  VST3_CATEGORIES Fx Distortion)

target_sources(AES PRIVATE
  PluginProcessor.cpp
  PluginProcessor.h
  PluginEditor.cpp
  PluginEditor.h)

target_compile_definitions(AES PUBLIC
  JUCE_WEB_BROWSER=0
  JUCE_USE_CURL=0
  JUCE_VST3_CAN_REPLACE_VST2=0
  JUCE_DISPLAY_SPLASH_SCREEN=0)

target_link_libraries(AES
  PRIVATE
    aes_engine
    aes_amps
    juce::juce_audio_utils
    juce::juce_dsp
  PUBLIC
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags)
