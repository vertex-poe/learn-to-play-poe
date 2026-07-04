function(add_qt_test name)
    qt_add_executable(${name} ${ARGN})
    target_link_libraries(${name} PRIVATE Qt6::Test)
    add_test(NAME ${name} COMMAND ${name})
endfunction()

add_qt_test(test_placeholder test_placeholder.cpp)
add_qt_test(test_dpi test_dpi.cpp)

# TaskManager/BackgroundWorker have no other app dependencies (just Qt), so
# this links their real sources directly instead of mirroring the logic.
# The two headers must be listed explicitly (not just #included) so AUTOMOC
# generates their Q_OBJECT machinery for this target too.
add_qt_test(test_task_manager
    test_task_manager.cpp
    ${CMAKE_SOURCE_DIR}/src/workers/TaskManager.cpp
    ${CMAKE_SOURCE_DIR}/src/workers/BackgroundWorker.h
    ${CMAKE_SOURCE_DIR}/src/workers/ProgressTrackerWorker.h)
target_include_directories(test_task_manager PRIVATE ${CMAKE_SOURCE_DIR}/src)

qt_add_executable(test_startup_timing test_startup_timing.cpp)
target_link_libraries(test_startup_timing PRIVATE Qt6::Test unofficial::sqlite3::sqlite3)
target_compile_definitions(test_startup_timing PRIVATE
    L2P_EXE_PATH="$<TARGET_FILE:l2p-poe>"
    L2P_SCHEMA_SQL_PATH="${CMAKE_SOURCE_DIR}/poe-info-service/internal/schema/sql/schema.sql"
    L2P_PERF_RESULTS_PATH="${CMAKE_BINARY_DIR}/perf_results.json")
add_test(NAME test_startup_timing COMMAND test_startup_timing)
set_tests_properties(test_startup_timing PROPERTIES LABELS "perf")

qt_add_executable(test_perf_metrics test_perf_metrics.cpp)
target_link_libraries(test_perf_metrics PRIVATE
    Qt6::Core Qt6::Test unofficial::sqlite3::sqlite3
)
target_compile_definitions(test_perf_metrics PRIVATE
    L2P_EXE_PATH="$<TARGET_FILE:l2p-poe>"
    L2P_PERF_RESULTS_PATH="${CMAKE_BINARY_DIR}/perf_results.json"
    L2P_SCHEMA_SQL_PATH="${CMAKE_SOURCE_DIR}/poe-info-service/internal/schema/sql/schema.sql"
    L2P_PERF_TARGETS_PATH="${CMAKE_SOURCE_DIR}/tests/perf_targets.json"
)
add_test(NAME test_perf_metrics COMMAND test_perf_metrics)
set_tests_properties(test_perf_metrics PROPERTIES LABELS "perf")

# Functional correctness for the startup path: app must start, populate LogPage,
# and emit the timing marker — both with an empty DB and a pre-populated DB.
# Runs on every `just test` (not labeled perf) so regressions are caught early.
qt_add_executable(test_log_startup test_log_startup.cpp)
set_target_properties(test_log_startup PROPERTIES WIN32_EXECUTABLE FALSE)
target_link_libraries(test_log_startup PRIVATE
    Qt6::Core Qt6::Test unofficial::sqlite3::sqlite3
)
target_compile_definitions(test_log_startup PRIVATE
    L2P_EXE_PATH="$<TARGET_FILE:l2p-poe>"
    L2P_SCHEMA_SQL_PATH="${CMAKE_SOURCE_DIR}/poe-info-service/internal/schema/sql/schema.sql"
)
add_test(NAME test_log_startup COMMAND test_log_startup)

add_subdirectory(ref_basic)
add_test(NAME test_ref_basic COMMAND ref_basic_app)

add_subdirectory(ref_data)
qt_add_executable(test_ref_data test_ref_data.cpp)
target_link_libraries(test_ref_data PRIVATE Qt6::Core Qt6::Test unofficial::sqlite3::sqlite3)
target_compile_definitions(test_ref_data PRIVATE
    L2P_REF_DATA_EXE_PATH="$<TARGET_FILE:ref_data_app>"
)
add_test(NAME test_ref_data COMMAND test_ref_data)
