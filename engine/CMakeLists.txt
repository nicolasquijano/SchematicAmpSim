add_library(aes_engine STATIC
  src/version.cpp
  src/circuit.cpp
  src/preamp2203.cpp
  src/poweramp2203.cpp)
target_include_directories(aes_engine PUBLIC include)
set_target_properties(aes_engine PROPERTIES INTERPROCEDURAL_OPTIMIZATION ON)
target_compile_features(aes_engine PUBLIC cxx_std_20)
# No /fp:fast: parity with the Rust reference needs strict IEEE behaviour.
if(MSVC)
  target_compile_options(aes_engine PRIVATE /W4 /fp:precise)
endif()
