function(add_qt_test name)
    qt_add_executable(${name} ${ARGN})
    target_link_libraries(${name} PRIVATE Qt6::Test)
    add_test(NAME ${name} COMMAND ${name})
endfunction()

add_qt_test(test_placeholder test_placeholder.cpp)
add_qt_test(test_dpi test_dpi.cpp)

qt_add_executable(test_startup_timing test_startup_timing.cpp)
target_link_libraries(test_startup_timing PRIVATE Qt6::Test)
target_compile_definitions(test_startup_timing PRIVATE
    L2P_EXE_PATH="$<TARGET_FILE:l2p-poe1>"
    L2P_PERF_RESULTS_PATH="${CMAKE_BINARY_DIR}/perf_results.json")
add_test(NAME test_startup_timing COMMAND test_startup_timing)
set_tests_properties(test_startup_timing PROPERTIES LABELS "perf")
