find_package(tomlplusplus CONFIG REQUIRED)
# unofficial-sqlite3 is found in the root CMakeLists — l2p-poe itself no
# longer links it (poe-info-service owns the database exclusively, ADR-006);
# it's found there only so tests/ can still use it directly.

qt_add_executable(l2p-poe
    main.cpp
    core/AppConfig.cpp
    core/AppConfig.h
    core/Cli.cpp
    core/Cli.h
    core/MainWindow.cpp
    core/MainWindow.h
    core/PaintProbeFilter.cpp
    core/PaintProbeFilter.h
    core/PerfProbe.cpp
    core/PerfProbe.h
    core/DeferredTaskQueue.cpp
    core/DeferredTaskQueue.h
    events/LiveEvent.h
    events/LiveEventBus.cpp
    events/LiveEventBus.h
    events/LiveEventRule.h
    events/LiveEventRuleEngine.cpp
    events/LiveEventRuleEngine.h
    workers/BackgroundWorker.h
    workers/TaskManager.cpp
    workers/TaskManager.h
    platform/WindowTracker.h
    util/DialogHash.h
    util/Docs.h
    util/PoeAccountStore.cpp
    util/PoeAccountStore.h
    util/ScopedBudget.h
    ui/AppStyle.cpp
    ui/AppStyle.h
    ui/overlay/GameOverlay.cpp
    ui/overlay/GameOverlay.h
    ui/NavBar.cpp
    ui/NavBar.h
    ui/TaskPanel.cpp
    ui/TaskPanel.h
    ui/Theme.cpp
    ui/Theme.h
    ui/chat/ChatPage.cpp
    ui/chat/ChatPage.h
    ui/chat/DmPage.cpp
    ui/chat/DmPage.h
    ui/log/SessionViewPage.cpp
    ui/log/SessionViewPage.h
    ui/log/LogPage.cpp
    ui/log/LogPage.h
    ui/settings/PoeLoginWindow.cpp
    ui/settings/PoeLoginWindow.h
    ui/settings/SettingsDialog.cpp
    ui/settings/SettingsDialog.h
    ui/settings/SettingsPage.cpp
    ui/settings/SettingsPage.h
    ui/widgets/ListEditor.cpp
    ui/widgets/ListEditor.h
    services/ServiceManager.cpp
    services/ServiceManager.h
    services/PoeInfoClient.cpp
    services/PoeInfoClient.h
    services/PoeInfoRecords.h
    ui/widgets/NotificationWidget.cpp
    ui/widgets/NotificationWidget.h
    ui/widgets/NotificationsPanel.cpp
    ui/widgets/NotificationsPanel.h
    ui/widgets/ScrollJumpButton.cpp
    ui/widgets/ScrollJumpButton.h
)

qt_add_resources(l2p-poe "resources"
    PREFIX "/icons"
    BASE "${CMAKE_SOURCE_DIR}/assets/logo"
    FILES
        "${CMAKE_SOURCE_DIR}/assets/logo/vertex-icon.png"
)

qt_add_resources(l2p-poe "svgicons"
    PREFIX "/icons"
    BASE "${CMAKE_SOURCE_DIR}/assets/icons"
    FILES
        "${CMAKE_SOURCE_DIR}/assets/icons/gear.svg"
        "${CMAKE_SOURCE_DIR}/assets/icons/gear-fill.svg"
        "${CMAKE_SOURCE_DIR}/assets/icons/arrow-down.svg"
        "${CMAKE_SOURCE_DIR}/assets/icons/chevron-bar-left.svg"
        "${CMAKE_SOURCE_DIR}/assets/icons/chevron-bar-right.svg"
        "${CMAKE_SOURCE_DIR}/assets/icons/chevron-bar-down.svg"
        "${CMAKE_SOURCE_DIR}/assets/icons/info-circle.svg"
        "${CMAKE_SOURCE_DIR}/assets/icons/house-fill.svg"
        "${CMAKE_SOURCE_DIR}/assets/icons/map-fill.svg"
        "${CMAKE_SOURCE_DIR}/assets/icons/alarm-fill.svg"
        "${CMAKE_SOURCE_DIR}/assets/icons/shop.svg"
        "${CMAKE_SOURCE_DIR}/assets/icons/bug-fill.svg"
        "${CMAKE_SOURCE_DIR}/assets/icons/coin.svg"
        "${CMAKE_SOURCE_DIR}/assets/icons/trophy-fill.svg"
        "${CMAKE_SOURCE_DIR}/assets/icons/bullseye.svg"
        "${CMAKE_SOURCE_DIR}/assets/icons/qr-code.svg"
        "${CMAKE_SOURCE_DIR}/assets/icons/minecart-loaded.svg"
        "${CMAKE_SOURCE_DIR}/assets/icons/tree-fill.svg"
        "${CMAKE_SOURCE_DIR}/assets/icons/geo.svg"
        "${CMAKE_SOURCE_DIR}/assets/icons/geo-fill.svg"
        "${CMAKE_SOURCE_DIR}/assets/icons/door-open-fill.svg"
        "${CMAKE_SOURCE_DIR}/assets/icons/safe2-fill.svg"
        "${CMAKE_SOURCE_DIR}/assets/icons/indent.svg"
        "${CMAKE_SOURCE_DIR}/assets/icons/box-arrow-in-right.svg"
        "${CMAKE_SOURCE_DIR}/assets/icons/person-fill.svg"
        "${CMAKE_SOURCE_DIR}/assets/icons/clipboard-fill.svg"
        "${CMAKE_SOURCE_DIR}/assets/icons/search.svg"
        "${CMAKE_SOURCE_DIR}/assets/icons/fleur-de-lis.svg"
        "${CMAKE_SOURCE_DIR}/assets/icons/fleur-de-lis-shield.svg"
        "${CMAKE_SOURCE_DIR}/assets/icons/cattle-skull.svg"
        "${CMAKE_SOURCE_DIR}/assets/icons/branch.svg"
        "${CMAKE_SOURCE_DIR}/assets/icons/stopwatch-fill.svg"
)

qt_add_resources(l2p-poe "brandsvg"
    PREFIX "/brand"
    BASE "${CMAKE_SOURCE_DIR}/assets/vertex-industries"
    FILES
        "${CMAKE_SOURCE_DIR}/assets/vertex-industries/vertex-icon.svg"
)

# Build poe-info-service (Go) alongside the Qt app so it's available for tests
# and the runtime loader (ServiceManager looks for it next to l2p-poe.exe).
find_program(GO_EXECUTABLE go)
if(GO_EXECUTABLE)
    # Use l2p-poe's own output dir (not CMAKE_CURRENT_BINARY_DIR) so this still
    # lands next to l2p-poe.exe when CMAKE_RUNTIME_OUTPUT_DIRECTORY is
    # overridden (e.g. windows-msvc preset, dev/perf_baseline_prev.py).
    get_target_property(_l2p_poe_bin_dir l2p-poe RUNTIME_OUTPUT_DIRECTORY)
    if(NOT _l2p_poe_bin_dir)
        set(_l2p_poe_bin_dir "${CMAKE_CURRENT_BINARY_DIR}")
    endif()
    if(WIN32)
        set(_service_out "${_l2p_poe_bin_dir}/poe-info-service.exe")
    else()
        set(_service_out "${_l2p_poe_bin_dir}/poe-info-service")
    endif()
    file(GLOB_RECURSE _go_sources
        "${CMAKE_SOURCE_DIR}/poe-info-service/*.go"
        "${CMAKE_SOURCE_DIR}/poe-info-service/*.sql"
    )
    add_custom_command(
        OUTPUT  "${_service_out}"
        COMMAND "${GO_EXECUTABLE}" build -o "${_service_out}" .
        DEPENDS ${_go_sources}
                "${CMAKE_SOURCE_DIR}/poe-info-service/go.mod"
                "${CMAKE_SOURCE_DIR}/poe-info-service/go.sum"
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}/poe-info-service"
        COMMENT "Building poe-info-service"
        VERBATIM
    )
    add_custom_target(poe_info_service_bin ALL DEPENDS "${_service_out}")
    add_dependencies(l2p-poe poe_info_service_bin)
else()
    message(WARNING "go not found — poe-info-service will not be built; tests requiring it will fail")
endif()

add_custom_target(validate_doc_urls
    COMMAND ${CMAKE_COMMAND}
        -D "SOURCE_DIR=${CMAKE_SOURCE_DIR}"
        -P "${CMAKE_SOURCE_DIR}/cmake/ValidateDocUrls.cmake"
    COMMENT "Validating docsSiteUrl paths"
    VERBATIM
)
add_dependencies(l2p-poe validate_doc_urls)

target_include_directories(l2p-poe PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

if (WIN32)
    target_sources(l2p-poe PRIVATE platform/WindowTrackerWindows.cpp platform/OverlayKeepalive.cpp platform/OverlayKeepalive.h platform/l2p-poe.rc platform/l2p-poe.manifest)
    target_link_libraries(l2p-poe PRIVATE psapi dbghelp)
elseif (UNIX)
    target_sources(l2p-poe PRIVATE platform/WindowTrackerLinux.cpp)
    find_package(X11 REQUIRED)
    target_link_libraries(l2p-poe PRIVATE X11::X11)
endif()

find_package(Qt6 COMPONENTS Core Gui Widgets Svg WebEngineWidgets Network Concurrent WebSockets REQUIRED)

target_link_libraries(l2p-poe PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::Svg
    Qt6::WebEngineWidgets
    Qt6::Network
    Qt6::Concurrent
    Qt6::WebSockets
    tomlplusplus::tomlplusplus
)

# --- Install / deploy ---
install(TARGETS l2p-poe
    RUNTIME DESTINATION .
    BUNDLE  DESTINATION .
    LIBRARY DESTINATION .
)

qt_generate_deploy_app_script(
    TARGET l2p-poe
    OUTPUT_SCRIPT deploy_script
    NO_UNSUPPORTED_PLATFORM_ERROR
)
install(SCRIPT ${deploy_script})
