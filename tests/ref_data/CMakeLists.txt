qt_add_executable(ref_data_app main.cpp)
target_link_libraries(ref_data_app PRIVATE Qt6::Widgets Qt6::Concurrent unofficial::sqlite3::sqlite3)
set_target_properties(ref_data_app PROPERTIES WIN32_EXECUTABLE FALSE)
