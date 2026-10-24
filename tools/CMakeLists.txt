add_executable(aes_inspect inspect_amp.cpp)
target_link_libraries(aes_inspect PRIVATE aes_engine aes_amps)

add_executable(aes_fit_lite fit_lite.cpp)
target_link_libraries(aes_fit_lite PRIVATE aes_engine aes_amps)

add_executable(aes_bench bench.cpp)
target_link_libraries(aes_bench PRIVATE aes_engine aes_amps)
set_target_properties(aes_bench PROPERTIES INTERPROCEDURAL_OPTIMIZATION ON)

# The same benchmark with the solver's cycle counters compiled in (engine sources built twice on purpose).
add_executable(aes_profile EXCLUDE_FROM_ALL bench.cpp
  ${CMAKE_SOURCE_DIR}/engine/src/circuit.cpp
  ${CMAKE_SOURCE_DIR}/engine/src/netlist.cpp
  ${CMAKE_SOURCE_DIR}/engine/src/amp_def.cpp
  ${CMAKE_SOURCE_DIR}/engine/src/preamp.cpp
  ${CMAKE_SOURCE_DIR}/engine/src/poweramp.cpp
  ${CMAKE_BINARY_DIR}/amps/embedded_amp_files.cpp
  ${CMAKE_SOURCE_DIR}/amps/embedded_amps.cpp)
target_compile_definitions(aes_profile PRIVATE AES_PROFILE)
target_include_directories(aes_profile PRIVATE ${CMAKE_SOURCE_DIR}/engine/include ${CMAKE_SOURCE_DIR}/amps/include)
target_link_libraries(aes_profile PRIVATE nlohmann_json::nlohmann_json)
