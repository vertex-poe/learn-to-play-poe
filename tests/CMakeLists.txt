function(add_qt_test name)
    qt_add_executable(${name} ${ARGN})
    target_link_libraries(${name} PRIVATE Qt6::Test)
    add_test(NAME ${name} COMMAND ${name})
endfunction()

add_qt_test(test_placeholder test_placeholder.cpp)
