cmake_minimum_required(VERSION 3.16)

project(dracoved_app LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

option(DRACOVED_ENABLE_ASTRO_MAP "Enable Astrocartography map (QtLocation)" OFF)

find_package(Qt6 REQUIRED COMPONENTS Widgets Network Svg SvgWidgets)

add_executable(dracoved_app
    resources.qrc
    src/main.cpp
    src/gui/main_window.h
    src/gui/main_window.cpp
    src/gui/aspect_orbs_dialog.h
    src/gui/aspect_orbs_dialog.cpp
    src/gui/chart_setup_dialog.h
    src/gui/chart_setup_dialog.cpp
    src/gui/chart_wheel_widget.h
    src/gui/chart_wheel_widget.cpp
    src/gui/transit_workers.h
    src/gui/transit_calc_service.h
    src/gui/transit_calc_service.cpp
    src/core/chart_types.h
    src/core/fixed_stars.h
    src/core/fixed_stars.cpp
    src/core/formatting.h
    src/core/formatting.cpp
    src/core/swiss_eph.h
    src/core/swiss_eph.cpp
    src/core/tropical_natal.h
    src/core/tropical_natal.cpp
    src/core/progression.h
    src/core/progression.cpp
    src/core/timezone_utils.h
    src/core/timezone_utils.cpp
)

target_link_libraries(dracoved_app PRIVATE Qt6::Widgets Qt6::Network Qt6::Svg Qt6::SvgWidgets)

if (DRACOVED_ENABLE_ASTRO_MAP)
    find_package(Qt6 REQUIRED COMPONENTS Quick QuickWidgets Qml Location Positioning)
    target_compile_definitions(dracoved_app PRIVATE DRACOVED_ENABLE_ASTRO_MAP=1)
    target_link_libraries(dracoved_app PRIVATE Qt6::Quick Qt6::QuickWidgets Qt6::Qml Qt6::Location Qt6::Positioning)
endif()
