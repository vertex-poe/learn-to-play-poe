function(add_qt_test name)
    qt_add_executable(${name} ${ARGN})
    target_link_libraries(${name} PRIVATE Qt6::Test)
    add_test(NAME ${name} COMMAND ${name})
endfunction()

add_qt_test(test_placeholder test_placeholder.cpp)
add_qt_test(test_dpi test_dpi.cpp)

# SessionQueryLimits.h is a plain header (no Qt Widget dependencies), so this
# links against it directly instead of mirroring the logic.
add_qt_test(test_session_query_limits test_session_query_limits.cpp)
target_include_directories(test_session_query_limits PRIVATE ${CMAKE_SOURCE_DIR}/src)

# ZoneAfkSuffix.h is likewise a plain header, pulled out of SessionViewPage
# so the zone-card AFK suffix logic is testable without Qt Widget deps.
add_qt_test(test_zone_afk_suffix test_zone_afk_suffix.cpp)
target_include_directories(test_zone_afk_suffix PRIVATE ${CMAKE_SOURCE_DIR}/src)

# SteamApiKeyExtractor.h is likewise a plain header (no WebEngine dep), pulled
# out of SteamKeyLoginWindow so the scrape-regex logic is testable directly.
add_qt_test(test_steam_api_key_extractor test_steam_api_key_extractor.cpp)
target_include_directories(test_steam_api_key_extractor PRIVATE ${CMAKE_SOURCE_DIR}/src)

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

# SessionViewPage/LogPage's request-retry-on-error paths (see ROADMAP.md
# history): PoeInfoClient isn't mockable (concrete QObject, non-virtual
# request(), real QWebSocket), so these drive it through FakePoeInfoServer, an
# in-process QWebSocketServer double that speaks its request/response wire
# protocol, instead of mocking PoeInfoClient itself.
find_package(Qt6 COMPONENTS WebSockets REQUIRED)

set(_poe_info_retry_test_support
    FakePoeInfoServer.cpp
    FakePoeInfoServer.h
    ${CMAKE_SOURCE_DIR}/src/services/PoeInfoClient.cpp
    ${CMAKE_SOURCE_DIR}/src/services/PoeInfoClient.h
    ${CMAKE_SOURCE_DIR}/src/events/LiveEventBus.cpp
    ${CMAKE_SOURCE_DIR}/src/events/LiveEventBus.h
    ${CMAKE_SOURCE_DIR}/src/ui/widgets/NotificationWidget.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/widgets/NotificationWidget.h
    ${CMAKE_SOURCE_DIR}/src/ui/widgets/ScrollJumpButton.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/widgets/ScrollJumpButton.h
    ${CMAKE_SOURCE_DIR}/src/ui/Theme.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/Theme.h
    ${CMAKE_SOURCE_DIR}/src/ui/AppStyle.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/AppStyle.h
)

qt_add_executable(test_sessionviewpage_retry
    test_sessionviewpage_retry.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/log/SessionViewPage.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/log/SessionViewPage.h
    ${_poe_info_retry_test_support}
)
target_include_directories(test_sessionviewpage_retry PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(test_sessionviewpage_retry PRIVATE
    Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Svg Qt6::WebSockets Qt6::Test)
add_test(NAME test_sessionviewpage_retry COMMAND test_sessionviewpage_retry)

qt_add_executable(test_logpage_retry
    test_logpage_retry.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/log/LogPage.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/log/LogPage.h
    ${_poe_info_retry_test_support}
)
target_include_directories(test_logpage_retry PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(test_logpage_retry PRIVATE
    Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Svg Qt6::WebSockets Qt6::Test)
add_test(NAME test_logpage_retry COMMAND test_logpage_retry)

# SteamAccountStore's credentials.* round trip, driven through the same
# FakePoeInfoServer double (no Widgets/Gui needed — SteamAccountStore, like
# PoeInfoClient, is plain QObject/QtCore).
qt_add_executable(test_steamaccountstore
    test_steamaccountstore.cpp
    ${CMAKE_SOURCE_DIR}/src/util/SteamAccountStore.cpp
    ${CMAKE_SOURCE_DIR}/src/util/SteamAccountStore.h
    FakePoeInfoServer.cpp
    FakePoeInfoServer.h
    ${CMAKE_SOURCE_DIR}/src/services/PoeInfoClient.cpp
    ${CMAKE_SOURCE_DIR}/src/services/PoeInfoClient.h
)
target_include_directories(test_steamaccountstore PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(test_steamaccountstore PRIVATE Qt6::Core Qt6::WebSockets Qt6::Test)
add_test(NAME test_steamaccountstore COMMAND test_steamaccountstore)

# PoeOAuthStore's poe.oauth.* round trip (request/response) plus the
# "poeOAuthStatus" push-topic path, via the same FakePoeInfoServer double.
qt_add_executable(test_poeoauthstore
    test_poeoauthstore.cpp
    ${CMAKE_SOURCE_DIR}/src/util/PoeOAuthStore.cpp
    ${CMAKE_SOURCE_DIR}/src/util/PoeOAuthStore.h
    FakePoeInfoServer.cpp
    FakePoeInfoServer.h
    ${CMAKE_SOURCE_DIR}/src/services/PoeInfoClient.cpp
    ${CMAKE_SOURCE_DIR}/src/services/PoeInfoClient.h
)
target_include_directories(test_poeoauthstore PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(test_poeoauthstore PRIVATE Qt6::Core Qt6::WebSockets Qt6::Test)
add_test(NAME test_poeoauthstore COMMAND test_poeoauthstore)

add_subdirectory(ref_basic)
add_test(NAME test_ref_basic COMMAND ref_basic_app)

add_subdirectory(ref_data)
qt_add_executable(test_ref_data test_ref_data.cpp)
target_link_libraries(test_ref_data PRIVATE Qt6::Core Qt6::Test unofficial::sqlite3::sqlite3)
target_compile_definitions(test_ref_data PRIVATE
    L2P_REF_DATA_EXE_PATH="$<TARGET_FILE:ref_data_app>"
)
add_test(NAME test_ref_data COMMAND test_ref_data)
