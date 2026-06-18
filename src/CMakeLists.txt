find_package(tomlplusplus CONFIG REQUIRED)

qt_add_executable(l2p-poe1
    main.cpp
    AppConfig.cpp
    AppConfig.h
    MainWindow.cpp
    MainWindow.h
    SettingsDialog.cpp
    SettingsDialog.h
)

qt_add_resources(l2p-poe1 "resources"
    PREFIX "/icons"
    BASE "${CMAKE_SOURCE_DIR}/assets/logo"
    FILES
        "${CMAKE_SOURCE_DIR}/assets/logo/vertex-icon.png"
)

target_link_libraries(l2p-poe1 PRIVATE
    Qt6::Core
    Qt6::Widgets
    tomlplusplus::tomlplusplus
)
