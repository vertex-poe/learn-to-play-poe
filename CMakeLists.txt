cmake_minimum_required(VERSION 3.25)
project(l2p-poe1 VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt6 REQUIRED COMPONENTS Core Svg Widgets Test)
find_package(Python3 REQUIRED COMPONENTS Interpreter)

enable_testing()

add_subdirectory(src)
add_subdirectory(tests)
