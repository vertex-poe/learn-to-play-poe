qt_add_executable(l2p-poe1
    main.cpp
)

target_link_libraries(l2p-poe1 PRIVATE
    Qt6::Core
    Qt6::Widgets
)
