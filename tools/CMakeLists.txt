add_executable(aes_inspect inspect_amp.cpp)
target_link_libraries(aes_inspect PRIVATE aes_engine aes_amps)

add_executable(aes_fit_lite fit_lite.cpp)
target_link_libraries(aes_fit_lite PRIVATE aes_engine aes_amps)

add_executable(aes_bench bench.cpp)
target_link_libraries(aes_bench PRIVATE aes_engine aes_amps)
set_target_properties(aes_bench PROPERTIES INTERPROCEDURAL_OPTIMIZATION ON)
