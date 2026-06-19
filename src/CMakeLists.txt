find_package(tomlplusplus CONFIG REQUIRED)

qt_add_executable(l2p-poe1
    main.cpp
    AppConfig.cpp
    AppConfig.h
    AppStyle.cpp
    AppStyle.h
    GameOverlay.cpp
    GameOverlay.h
    ListEditor.cpp
    ListEditor.h
    MainWindow.cpp
    MainWindow.h
    NotificationWidget.cpp
    NotificationWidget.h
    NotificationsPanel.cpp
    NotificationsPanel.h
    SettingsDialog.cpp
    SettingsDialog.h
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
