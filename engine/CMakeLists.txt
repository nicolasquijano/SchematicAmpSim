add_library(sas_engine STATIC
  src/version.cpp
  src/circuit.cpp
  src/netlist.cpp
  src/amp_def.cpp
  src/preamp.cpp
  src/vibrato.cpp
  src/poweramp.cpp
  src/chain.cpp)
target_include_directories(sas_engine PUBLIC include)
target_link_libraries(sas_engine PUBLIC nlohmann_json::nlohmann_json)
set_target_properties(sas_engine PROPERTIES INTERPROCEDURAL_OPTIMIZATION ON)
target_compile_features(sas_engine PUBLIC cxx_std_20)
# No /fp:fast: parity with the Rust reference needs strict IEEE behaviour.
if(MSVC)
  target_compile_options(sas_engine PRIVATE /W4 /fp:precise)
endif()
