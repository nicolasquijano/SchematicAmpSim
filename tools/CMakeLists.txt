add_executable(sas_inspect inspect_amp.cpp)
target_link_libraries(sas_inspect PRIVATE sas_engine sas_amps)

add_executable(sas_fit_lite fit_lite.cpp)
target_link_libraries(sas_fit_lite PRIVATE sas_engine sas_amps)

add_executable(sas_bench bench.cpp)
target_link_libraries(sas_bench PRIVATE sas_engine sas_amps)
set_target_properties(sas_bench PROPERTIES INTERPROCEDURAL_OPTIMIZATION ON)

# The same benchmark with the solver's cycle counters compiled in (engine sources built twice on purpose).
# The engine sources are taken by glob so a new one is never missing here.
file(GLOB _sas_engine_sources CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/engine/src/*.cpp")
add_executable(sas_profile EXCLUDE_FROM_ALL bench.cpp
  ${_sas_engine_sources}
  ${CMAKE_BINARY_DIR}/engine/compressor_net.cpp
  ${CMAKE_BINARY_DIR}/amps/embedded_amp_files.cpp
  ${CMAKE_SOURCE_DIR}/amps/embedded_amps.cpp)
target_compile_definitions(sas_profile PRIVATE SAS_PROFILE NAM_SAMPLE_FLOAT NAM_ENABLE_A2_FAST NOMINMAX WIN32_LEAN_AND_MEAN)
# The engine sources include the NAM loader, so the profile build needs the NAM headers and objects too.
target_include_directories(sas_profile SYSTEM PRIVATE "${nam_SOURCE_DIR}" "${nam_SOURCE_DIR}/Dependencies/eigen" "${nam_SOURCE_DIR}/Dependencies/nlohmann")
target_sources(sas_profile PRIVATE $<TARGET_OBJECTS:sas_nam_core>)
target_include_directories(sas_profile PRIVATE ${CMAKE_SOURCE_DIR}/engine/include ${CMAKE_SOURCE_DIR}/amps/include ${CMAKE_BINARY_DIR}/generated)
target_compile_features(sas_profile PRIVATE cxx_std_20)
target_link_libraries(sas_profile PRIVATE nlohmann_json::nlohmann_json)

add_executable(sas_match_levels match_levels.cpp)
target_link_libraries(sas_match_levels PRIVATE sas_engine sas_amps)

add_executable(sas_bias_cal bias_cal.cpp)
target_link_libraries(sas_bias_cal PRIVATE sas_engine sas_amps)

# Normal mode against a reference (formulas, tolerance 1e-9): how far the sound moves when the solver is made faster (docs/PLAN_CPU.md).
add_executable(sas_compare compare.cpp)
target_link_libraries(sas_compare PRIVATE sas_engine sas_amps)
set_target_properties(sas_compare PROPERTIES INTERPROCEDURAL_OPTIMIZATION ON)

# Why Newton needs many iterations on an amp (histograms, shortened steps, which device keeps the residual).
add_executable(sas_diag_newton diag_newton.cpp)
target_link_libraries(sas_diag_newton PRIVATE sas_engine sas_amps)

# The LU kernels alone (cycles per factorisation and solve, and a checksum of the results).
add_executable(sas_bench_lu bench_lu.cpp)
target_include_directories(sas_bench_lu PRIVATE ${CMAKE_SOURCE_DIR}/engine/include)
target_compile_features(sas_bench_lu PRIVATE cxx_std_20)

# The power amp at the host rate instead of 2x: sound and cost against the Normal mode as it is (docs/PLAN_CPU.md).
add_executable(sas_compare_rate compare_rate.cpp)
target_link_libraries(sas_compare_rate PRIVATE sas_engine sas_amps)
set_target_properties(sas_compare_rate PROPERTIES INTERPROCEDURAL_OPTIMIZATION ON)

# CPU of the whole chain per quality (Normal, Normal1x, Lite).
add_executable(sas_bench_chain bench_chain.cpp)
target_link_libraries(sas_bench_chain PRIVATE sas_engine sas_amps)

# CPU of every rack module (docs/MODULAR_RACK.md, section 6).
add_executable(sas_bench_modules bench_modules.cpp)
target_link_libraries(sas_bench_modules PRIVATE sas_engine sas_amps)

# Loads real .nam models the way the plugin does: CPU, rate match and output sanity.
add_executable(sas_nam_check nam_check.cpp)
target_link_libraries(sas_nam_check PRIVATE sas_engine sas_amps)

# Cost of each preamp stage by size (nodes, ports): what splitting a big preamp would save.
add_executable(sas_stage_cost stage_cost.cpp)
target_link_libraries(sas_stage_cost PRIVATE sas_engine sas_amps)

# A split preamp against the same preamp as one netlist (level, octave bands, waveform).
add_executable(sas_split_check split_check.cpp)
target_link_libraries(sas_split_check PRIVATE sas_engine sas_amps)
