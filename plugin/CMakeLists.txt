juce_add_plugin(AES
  COMPANY_NAME "Nico"
  PLUGIN_MANUFACTURER_CODE Nico
  PLUGIN_CODE Aes1
  FORMATS VST3
  PRODUCT_NAME "AES"
  IS_SYNTH FALSE
  NEEDS_MIDI_INPUT FALSE
  NEEDS_MIDI_OUTPUT FALSE
  COPY_PLUGIN_AFTER_BUILD FALSE
  VST3_CATEGORIES Fx Distortion)

target_sources(AES PRIVATE
  PluginProcessor.cpp
  PluginProcessor.h)

target_compile_definitions(AES PUBLIC
  JUCE_WEB_BROWSER=0
  JUCE_USE_CURL=0
  JUCE_VST3_CAN_REPLACE_VST2=0)

target_link_libraries(AES
  PRIVATE
    aes_engine
    juce::juce_audio_utils
    juce::juce_dsp
  PUBLIC
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags)
