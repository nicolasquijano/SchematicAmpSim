add_library(aes_engine STATIC
  src/version.cpp
  src/circuit.cpp
  src/netlist.cpp
  src/amp_def.cpp
  src/preamp.cpp
  src/poweramp.cpp
  src/chain.cpp)
target_include_directories(aes_engine PUBLIC include)
target_link_libraries(aes_engine PUBLIC nlohmann_json::nlohmann_json)
set_target_properties(aes_engine PROPERTIES INTERPROCEDURAL_OPTIMIZATION ON)
target_compile_features(aes_engine PUBLIC cxx_std_20)
# No /fp:fast: parity with the Rust reference needs strict IEEE behaviour.
if(MSVC)
  target_compile_options(aes_engine PRIVATE /W4 /fp:precise)
endif()
