# VST3 everywhere, plus Audio Unit on macOS (Logic, GarageBand).
set(SAS_PLUGIN_FORMATS VST3)
if(APPLE)
  list(APPEND SAS_PLUGIN_FORMATS AU)
endif()

juce_add_plugin(SchematicAmpSim
  VERSION ${PROJECT_VERSION}
  COMPANY_NAME "Nico"
  PLUGIN_MANUFACTURER_CODE Nico
  PLUGIN_CODE Sas1
  FORMATS ${SAS_PLUGIN_FORMATS}
  # Visible name (bundle file and name in the DAW). The VST3 class id comes from the manufacturer code
  # and PLUGIN_CODE above, not from this name.
  PRODUCT_NAME "Schematic Amp Sim"
  PLUGIN_NAME "Schematic Amp Sim"
  IS_SYNTH FALSE
  NEEDS_MIDI_INPUT FALSE
  NEEDS_MIDI_OUTPUT FALSE
  COPY_PLUGIN_AFTER_BUILD FALSE
  VST3_CATEGORIES Fx Distortion
  AU_MAIN_TYPE kAudioUnitType_Effect
  BUNDLE_ID com.nicolasquijano.schematicampsim)

target_sources(SchematicAmpSim PRIVATE
  PluginProcessor.cpp
  PluginProcessor.h
  PluginEditor.cpp
  PluginEditor.h)

target_compile_definitions(SchematicAmpSim PUBLIC
  JUCE_WEB_BROWSER=0
  JUCE_USE_CURL=0
  JUCE_VST3_CAN_REPLACE_VST2=0
  JUCE_DISPLAY_SPLASH_SCREEN=0)

target_link_libraries(SchematicAmpSim
  PRIVATE
    sas_engine
    sas_amps
    juce::juce_audio_utils
    juce::juce_dsp
  PUBLIC
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags)
