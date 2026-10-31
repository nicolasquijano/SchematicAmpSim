# VST3 and a standalone application everywhere (no DAW needed), plus Audio Unit on macOS (Logic, GarageBand).
set(SAS_PLUGIN_FORMATS VST3 Standalone)
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
  AmpFace.cpp
  AmpFace.h
  PluginEditor.cpp
  PluginEditor.h)

target_compile_definitions(SchematicAmpSim PUBLIC
  JUCE_WEB_BROWSER=0
  JUCE_USE_CURL=0
  JUCE_VST3_CAN_REPLACE_VST2=0
  JUCE_DISPLAY_SPLASH_SCREEN=0)

if(SAS_ASIO_SDK_DIR)
  target_include_directories(SchematicAmpSim PRIVATE "${SAS_ASIO_SDK_DIR}")
  target_compile_definitions(SchematicAmpSim PUBLIC JUCE_ASIO=1)
endif()

# The amps' own fronts: amps/<id>/face/*.png, renamed <id>_<name>.png so the binary-data names do not collide.
file(GLOB _face_pngs CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/amps/*/face/*.png")
set(_face_copies "")
foreach(_png IN LISTS _face_pngs)
  get_filename_component(_name "${_png}" NAME)
  get_filename_component(_facedir "${_png}" DIRECTORY)
  get_filename_component(_ampdir "${_facedir}" DIRECTORY)
  get_filename_component(_id "${_ampdir}" NAME)
  # Only amps whose amp.json asks for a faceplate carry their images; the others keep theirs in the repo for later.
  file(READ "${_ampdir}/amp.json" _ampjson)
  string(FIND "${_ampjson}" "\"faceplate\"" _wants_face)
  if(_wants_face EQUAL -1)
    continue()
  endif()
  configure_file("${_png}" "${CMAKE_BINARY_DIR}/faces/${_id}_${_name}" COPYONLY)
  list(APPEND _face_copies "${CMAKE_BINARY_DIR}/faces/${_id}_${_name}")
endforeach()
if(_face_copies)
  juce_add_binary_data(SasFaces SOURCES ${_face_copies})
  set_target_properties(SasFaces PROPERTIES POSITION_INDEPENDENT_CODE ON)
  target_compile_definitions(SchematicAmpSim PRIVATE SAS_HAS_FACES=1)
endif()

# Cabinet impulse responses built into the plugin: plugin/cabs/*.wav (listed in the cabinet page, see SasProcessor::builtInImpulseResponse*).
# The WAV files are NOT part of the repository (they are third-party, see plugin/cabs/README.md): a build that finds none still works,
# with an empty list, through a stub header with the same names.
file(GLOB _cab_wavs CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/cabs/*.wav")
list(SORT _cab_wavs)
if(_cab_wavs)
  juce_add_binary_data(SasCabs HEADER_NAME CabData.h NAMESPACE CabData SOURCES ${_cab_wavs})
  set_target_properties(SasCabs PROPERTIES POSITION_INDEPENDENT_CODE ON)
else()
  message(STATUS "SchematicAmpSim: no cabinet impulses in plugin/cabs (the built-in list will be empty)")
  file(WRITE "${CMAKE_BINARY_DIR}/cab_stub/CabData.h" "#pragma once
// Generated: no impulse responses were found in plugin/cabs, so there are none built in.
namespace CabData {
constexpr int namedResourceListSize = 0;
inline const char* namedResourceList[1] = {\"\"};
inline const char* originalFilenames[1] = {\"\"};
inline const char* getNamedResource(const char*, int& size) { size = 0; return nullptr; }
}  // namespace CabData
")
  target_include_directories(SchematicAmpSim PRIVATE "${CMAKE_BINARY_DIR}/cab_stub")
endif()

target_link_libraries(SchematicAmpSim
  PRIVATE
    $<$<BOOL:${_cab_wavs}>:SasCabs>
    sas_engine
    sas_amps
    $<$<BOOL:${_face_copies}>:SasFaces>
    juce::juce_audio_utils
    juce::juce_dsp
  PUBLIC
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags)
