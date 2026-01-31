cmake_minimum_required(VERSION 3.16)

project(dracoved_qt_sanity_test LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets)

add_executable(dracoved_qt_sanity_test
    main.cpp
)

target_link_libraries(dracoved_qt_sanity_test PRIVATE Qt6::Widgets)

