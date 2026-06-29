function(add_qt_test name)
    qt_add_executable(${name} ${ARGN})
    target_link_libraries(${name} PRIVATE Qt6::Test)
    add_test(NAME ${name} COMMAND ${name})
endfunction()

add_qt_test(test_placeholder test_placeholder.cpp)
add_qt_test(test_dpi test_dpi.cpp)

qt_add_executable(test_startup_timing test_startup_timing.cpp)
target_link_libraries(test_startup_timing PRIVATE Qt6::Test unofficial::sqlite3::sqlite3)
target_compile_definitions(test_startup_timing PRIVATE
    L2P_EXE_PATH="$<TARGET_FILE:l2p-poe1>"
    L2P_SCHEMA_SQL_PATH="${CMAKE_SOURCE_DIR}/data/schema.sql"
    L2P_PERF_RESULTS_PATH="${CMAKE_BINARY_DIR}/perf_results.json")
add_test(NAME test_startup_timing COMMAND test_startup_timing)
set_tests_properties(test_startup_timing PROPERTIES LABELS "perf")

qt_add_executable(test_perf_metrics test_perf_metrics.cpp)
target_link_libraries(test_perf_metrics PRIVATE
    Qt6::Core Qt6::Test unofficial::sqlite3::sqlite3
)
target_compile_definitions(test_perf_metrics PRIVATE
    L2P_EXE_PATH="$<TARGET_FILE:l2p-poe1>"
    L2P_PERF_RESULTS_PATH="${CMAKE_BINARY_DIR}/perf_results.json"
    L2P_SCHEMA_SQL_PATH="${CMAKE_SOURCE_DIR}/data/schema.sql"
    L2P_PERF_TARGETS_PATH="${CMAKE_SOURCE_DIR}/tests/perf_targets.json"
)
add_test(NAME test_perf_metrics COMMAND test_perf_metrics)
set_tests_properties(test_perf_metrics PROPERTIES LABELS "perf")

# Unit tests for Database::fetchSessions() — order, field mapping, limit, offset.
# Compiles Database.cpp directly; embeds schema.sql (seed.sql omitted intentionally:
# Database warns+continues if :/db/seed.sql is absent, and sessions tests don't need it).
qt_add_executable(test_database_sessions
    test_database_sessions.cpp
    ${CMAKE_SOURCE_DIR}/src/db/Database.cpp
)
qt_add_resources(test_database_sessions "test_dbdata"
    PREFIX "/db"
    BASE "${CMAKE_SOURCE_DIR}/data"
    FILES "${CMAKE_SOURCE_DIR}/data/schema.sql"
)
target_include_directories(test_database_sessions PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(test_database_sessions PRIVATE
    Qt6::Core Qt6::Test unofficial::sqlite3::sqlite3
)
add_test(NAME test_database_sessions COMMAND test_database_sessions)

# Functional correctness for the startup path: app must start, populate LogPage,
# and emit the timing marker — both with an empty DB and a pre-populated DB.
# Runs on every `just test` (not labeled perf) so regressions are caught early.
qt_add_executable(test_log_startup test_log_startup.cpp)
target_link_libraries(test_log_startup PRIVATE
    Qt6::Core Qt6::Test unofficial::sqlite3::sqlite3
)
target_compile_definitions(test_log_startup PRIVATE
    L2P_EXE_PATH="$<TARGET_FILE:l2p-poe1>"
    L2P_SCHEMA_SQL_PATH="${CMAKE_SOURCE_DIR}/data/schema.sql"
)
add_test(NAME test_log_startup COMMAND test_log_startup)
