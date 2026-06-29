qt_add_executable(ref_basic_app main.cpp)
target_link_libraries(ref_basic_app PRIVATE Qt6::Widgets)
set_target_properties(ref_basic_app PROPERTIES WIN32_EXECUTABLE FALSE)
