add_executable(sas_inspect inspect_amp.cpp)
target_link_libraries(sas_inspect PRIVATE sas_engine sas_amps)

add_executable(sas_fit_lite fit_lite.cpp)
target_link_libraries(sas_fit_lite PRIVATE sas_engine sas_amps)

add_executable(sas_bench bench.cpp)
target_link_libraries(sas_bench PRIVATE sas_engine sas_amps)
set_target_properties(sas_bench PROPERTIES INTERPROCEDURAL_OPTIMIZATION ON)

# The same benchmark with the solver's cycle counters compiled in (engine sources built twice on purpose).
add_executable(sas_profile EXCLUDE_FROM_ALL bench.cpp
  ${CMAKE_SOURCE_DIR}/engine/src/circuit.cpp
  ${CMAKE_SOURCE_DIR}/engine/src/netlist.cpp
  ${CMAKE_SOURCE_DIR}/engine/src/amp_def.cpp
  ${CMAKE_SOURCE_DIR}/engine/src/preamp.cpp
  ${CMAKE_SOURCE_DIR}/engine/src/poweramp.cpp
  ${CMAKE_BINARY_DIR}/amps/embedded_amp_files.cpp
  ${CMAKE_SOURCE_DIR}/amps/embedded_amps.cpp)
target_compile_definitions(sas_profile PRIVATE SAS_PROFILE)
target_include_directories(sas_profile PRIVATE ${CMAKE_SOURCE_DIR}/engine/include ${CMAKE_SOURCE_DIR}/amps/include)
target_link_libraries(sas_profile PRIVATE nlohmann_json::nlohmann_json)

add_executable(sas_match_levels match_levels.cpp)
target_link_libraries(sas_match_levels PRIVATE sas_engine sas_amps)
