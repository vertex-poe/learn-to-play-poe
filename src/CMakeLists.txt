find_package(tomlplusplus CONFIG REQUIRED)
# unofficial-sqlite3 is found in the root CMakeLists so tests/ can also use it.
include(FetchContent)
set(BUILD_WITH_QT6          ON  CACHE BOOL "" FORCE)
set(BUILD_TRANSLATIONS      OFF CACHE BOOL "" FORCE)
set(BUILD_TEST_APPLICATION  OFF CACHE BOOL "" FORCE)
FetchContent_Declare(qtkeychain
    GIT_REPOSITORY https://github.com/frankosterfeld/qtkeychain.git
    GIT_TAG        0.14.3
)
FetchContent_MakeAvailable(qtkeychain)
FetchContent_GetProperties(qtkeychain SOURCE_DIR _qtkeychain_src BINARY_DIR _qtkeychain_bin)

qt_add_executable(l2p-poe1
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
    db/Database.cpp
    db/Database.h
    db/DatabaseHealth.cpp
    db/DatabaseHealth.h
    db/DatabaseHealthWorker.cpp
    db/DatabaseHealthWorker.h
    db/QueryService.cpp
    db/QueryService.h
    events/LiveEvent.h
    events/LiveEventBus.cpp
    events/LiveEventBus.h
    events/LiveEventRule.h
    events/LiveEventRuleEngine.cpp
    events/LiveEventRuleEngine.h
    workers/BackgroundWorker.h
    workers/CloseOrphanSessionsWorker.cpp
    workers/CloseOrphanSessionsWorker.h
    workers/LogIngestWorker.cpp
    workers/LogIngestWorker.h
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
    ui/widgets/NotificationWidget.cpp
    ui/widgets/NotificationWidget.h
    ui/widgets/NotificationsPanel.cpp
    ui/widgets/NotificationsPanel.h
    ui/widgets/ScrollJumpButton.cpp
    ui/widgets/ScrollJumpButton.h
)

file(GLOB SEED_INPUTS
    "${CMAKE_SOURCE_DIR}/data/seed_base.sql"
    "${CMAKE_SOURCE_DIR}/data/areas/*.sql"
)

add_custom_command(
    OUTPUT "${CMAKE_SOURCE_DIR}/data/seed.sql"
    COMMAND Python3::Interpreter "${CMAKE_SOURCE_DIR}/dev/build/combine_seed.py"
    DEPENDS ${SEED_INPUTS} "${CMAKE_SOURCE_DIR}/dev/build/combine_seed.py"
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    COMMENT "Combining seed SQL files -> data/seed.sql"
    VERBATIM
)

qt_add_resources(l2p-poe1 "dbdata"
    PREFIX "/db"
    BASE "${CMAKE_SOURCE_DIR}/data"
    FILES
        "${CMAKE_SOURCE_DIR}/data/schema.sql"
        "${CMAKE_SOURCE_DIR}/data/seed.sql"
)

qt_add_resources(l2p-poe1 "resources"
    PREFIX "/icons"
    BASE "${CMAKE_SOURCE_DIR}/assets/logo"
    FILES
        "${CMAKE_SOURCE_DIR}/assets/logo/vertex-icon.png"
)

qt_add_resources(l2p-poe1 "svgicons"
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
)

qt_add_resources(l2p-poe1 "brandsvg"
    PREFIX "/brand"
    BASE "${CMAKE_SOURCE_DIR}/assets/vertex-industries"
    FILES
        "${CMAKE_SOURCE_DIR}/assets/vertex-industries/vertex-icon.svg"
)

add_custom_target(validate_doc_urls
    COMMAND ${CMAKE_COMMAND}
        -D "SOURCE_DIR=${CMAKE_SOURCE_DIR}"
        -P "${CMAKE_SOURCE_DIR}/cmake/ValidateDocUrls.cmake"
    COMMENT "Validating docsSiteUrl paths"
    VERBATIM
)
add_dependencies(l2p-poe1 validate_doc_urls)

target_include_directories(l2p-poe1 PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

if (WIN32)
    target_sources(l2p-poe1 PRIVATE platform/WindowTrackerWindows.cpp platform/OverlayKeepalive.cpp platform/OverlayKeepalive.h platform/l2p-poe1.rc platform/l2p-poe1.manifest)
    target_link_libraries(l2p-poe1 PRIVATE psapi dbghelp)
elseif (UNIX)
    target_sources(l2p-poe1 PRIVATE platform/WindowTrackerLinux.cpp)
    find_package(X11 REQUIRED)
    target_link_libraries(l2p-poe1 PRIVATE X11::X11)
endif()

target_include_directories(l2p-poe1 PRIVATE ${_qtkeychain_src} ${_qtkeychain_bin})
find_package(Qt6 COMPONENTS Core Gui Widgets Svg WebEngineWidgets Network Concurrent WebSockets REQUIRED)

target_link_libraries(l2p-poe1 PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::Svg
    Qt6::WebEngineWidgets
    Qt6::Network
    Qt6::Concurrent
    Qt6::WebSockets
    tomlplusplus::tomlplusplus
    unofficial::sqlite3::sqlite3
    qt6keychain
)

# On Windows, copy the dynamically linked qtkeychain DLL next to the executable
# so that tests (and the app) can run without STATUS_DLL_NOT_FOUND (0xC0000135).
if(WIN32)
    add_custom_command(TARGET l2p-poe1 POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
        $<TARGET_FILE:qt6keychain>
        $<TARGET_FILE_DIR:l2p-poe1>
    )
endif()

# --- Install / deploy ---
install(TARGETS l2p-poe1
    RUNTIME DESTINATION .
    BUNDLE  DESTINATION .
    LIBRARY DESTINATION .
)

qt_generate_deploy_app_script(
    TARGET l2p-poe1
    OUTPUT_SCRIPT deploy_script
    NO_UNSUPPORTED_PLATFORM_ERROR
)
install(SCRIPT ${deploy_script})
