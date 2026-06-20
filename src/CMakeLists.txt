find_package(tomlplusplus CONFIG REQUIRED)
find_package(unofficial-sqlite3 CONFIG REQUIRED)

qt_add_executable(l2p-poe1
    main.cpp
    AppConfig.cpp
    AppConfig.h
    AppStyle.cpp
    AppStyle.h
    BackgroundWorker.h
    Database.cpp
    Database.h
    GameOverlay.cpp
    GameOverlay.h
    ListEditor.cpp
    ListEditor.h
    LiveAlertsDialog.cpp
    LiveAlertsDialog.h
    LiveEvent.h
    LiveEventBus.cpp
    LiveEventBus.h
    LiveEventRule.h
    LiveEventRuleEngine.cpp
    LiveEventRuleEngine.h
    LogIngestWorker.cpp
    LogIngestWorker.h
    MainWindow.cpp
    MainWindow.h
    NotificationWidget.cpp
    NotificationWidget.h
    NotificationsPanel.cpp
    NotificationsPanel.h
    SettingsDialog.cpp
    SettingsDialog.h
    TaskManager.cpp
    TaskManager.h
    TaskPanel.cpp
    TaskPanel.h
    Theme.cpp
    Theme.h
    WindowTracker.h
)

qt_add_resources(l2p-poe1 "resources"
    PREFIX "/icons"
    BASE "${CMAKE_SOURCE_DIR}/assets/logo"
    FILES
        "${CMAKE_SOURCE_DIR}/assets/logo/vertex-icon.png"
)

if (WIN32)
    target_sources(l2p-poe1 PRIVATE WindowTrackerWindows.cpp l2p-poe1.rc)
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
