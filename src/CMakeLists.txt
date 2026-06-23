find_package(tomlplusplus CONFIG REQUIRED)
find_package(unofficial-sqlite3 CONFIG REQUIRED)

qt_add_executable(l2p-poe1
    main.cpp
    AppConfig.cpp
    AppConfig.h
    Cli.cpp
    Cli.h
    DialogHash.h
    Docs.h
    AppStyle.cpp
    AppStyle.h
    BackgroundWorker.h
    ChatPage.cpp
    ChatPage.h
    CloseOrphanSessionsWorker.cpp
    CloseOrphanSessionsWorker.h
    QueryService.cpp
    QueryService.h
    CurrentPage.cpp
    CurrentPage.h
    Database.cpp
    Database.h
    DatabaseHealth.cpp
    DatabaseHealth.h
    DatabaseHealthWorker.cpp
    DatabaseHealthWorker.h
    GameOverlay.cpp
    GameOverlay.h
    ListEditor.cpp
    ListEditor.h
    LiveEvent.h
    LiveEventBus.cpp
    LiveEventBus.h
    LiveEventRule.h
    LiveEventRuleEngine.cpp
    LiveEventRuleEngine.h
    LogIngestWorker.cpp
    LogIngestWorker.h
    DmPage.cpp
    DmPage.h
    MainWindow.cpp
    PastPage.cpp
    PastPage.h
    MainWindow.h
    NavBar.cpp
    NavBar.h
    NotificationWidget.cpp
    NotificationWidget.h
    ScrollJumpButton.cpp
    ScrollJumpButton.h
    SettingsPage.cpp
    SettingsPage.h
    TaskManager.cpp
    TaskManager.h
    TaskPanel.cpp
    TaskPanel.h
    Theme.cpp
    Theme.h
    WindowTracker.h
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
        "${CMAKE_SOURCE_DIR}/assets/icons/bullseye.svg"
        "${CMAKE_SOURCE_DIR}/assets/icons/qr-code.svg"
        "${CMAKE_SOURCE_DIR}/assets/icons/minecart-loaded.svg"
        "${CMAKE_SOURCE_DIR}/assets/icons/tree-fill.svg"
        "${CMAKE_SOURCE_DIR}/assets/icons/geo.svg"
        "${CMAKE_SOURCE_DIR}/assets/icons/geo-fill.svg"
        "${CMAKE_SOURCE_DIR}/assets/icons/door-open-fill.svg"
        "${CMAKE_SOURCE_DIR}/assets/icons/safe2-fill.svg"
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

if (WIN32)
    target_sources(l2p-poe1 PRIVATE WindowTrackerWindows.cpp OverlayKeepalive.cpp OverlayKeepalive.h l2p-poe1.rc)
    target_link_libraries(l2p-poe1 PRIVATE psapi)
    if (MSVC)
        # Disable the auto-generated manifest; we embed our own via the RC file.
        target_link_options(l2p-poe1 PRIVATE "/MANIFEST:NO")
    endif()
elseif (UNIX)
    target_sources(l2p-poe1 PRIVATE WindowTrackerLinux.cpp)
    find_package(X11 REQUIRED)
    target_link_libraries(l2p-poe1 PRIVATE X11::X11)
endif()

target_link_libraries(l2p-poe1 PRIVATE
    Qt6::Core
    Qt6::Svg
    Qt6::Widgets
    tomlplusplus::tomlplusplus
    unofficial::sqlite3::sqlite3
)

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
