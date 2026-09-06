cmake_minimum_required(VERSION 3.16)

project(dracoved_app LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)


find_package(Qt6 REQUIRED COMPONENTS Widgets Network Svg SvgWidgets)

add_executable(dracoved_app
    resources.qrc
    src/main.cpp
    src/gui/main_window.h
    src/gui/main_window.cpp
    src/gui/main_window_aspect_peaks.cpp
    src/gui/main_window_planetary_hours.cpp
    src/gui/main_window_zodiacal_releasing.cpp
    src/gui/main_window_geodetic_equivalents.cpp
    src/gui/main_window_solar_report.cpp
    src/gui/main_window_natal_report.cpp
    src/gui/main_window_return_finder.cpp
    src/gui/planetary_hours_controller.h
    src/gui/planetary_hours_controller.cpp
    src/gui/zodiacal_releasing_controller.h
    src/gui/zodiacal_releasing_controller.cpp
    src/gui/geodetic_equivalents_controller.h
    src/gui/geodetic_equivalents_controller.cpp
    src/gui/return_calculation_service.h
    src/gui/return_calculation_service.cpp
    src/gui/return_finder_types.h
    src/gui/return_finder_types.cpp
    src/gui/return_finder_worker.h
    src/gui/return_finder_worker.cpp
    src/gui/return_finder_controller.h
    src/gui/return_finder_controller_compile.cpp
    src/gui/astro_map_widget.h
    src/gui/astro_map_widget.cpp
    src/gui/aspect_orbs_dialog.h
    src/gui/aspect_orbs_dialog.cpp
    src/gui/preferences_dialog.h
    src/gui/preferences_dialog.cpp
    src/gui/aspect_matrix_delegate.h
    src/gui/aspect_matrix_delegate.cpp
    src/gui/row_hover_delegate.h
    src/gui/row_hover_delegate.cpp
    src/gui/collapsible_section.h
    src/gui/collapsible_section.cpp
    src/gui/chart_setup_dialog.h
    src/gui/chart_setup_dialog.cpp
    src/gui/chart_manager_dialog.h
    src/gui/chart_manager_dialog.cpp
    src/gui/chart_profile_store.h
    src/gui/chart_profile_store.cpp
    src/gui/synastry_controller.h
    src/gui/synastry_controller.cpp
    src/gui/synastry_calc.h
    src/gui/synastry_calc.cpp
    src/gui/chart_wheel_widget.h
    src/gui/chart_wheel_widget.cpp
    src/gui/transit_workers.h
    src/gui/transit_calc_service.h
    src/gui/transit_calc_service.cpp
    src/core/chart_types.h
    src/core/arabic_lots.h
    src/core/arabic_lots.cpp
    src/core/fixed_stars.h
    src/core/fixed_stars.cpp
    src/core/formatting.h
    src/core/formatting.cpp
    src/core/swiss_eph.h
    src/core/swiss_eph.cpp
    src/core/lunar_nodes.h
    src/core/lunar_nodes.cpp
    src/core/planetary_hours.h
    src/core/planetary_hours.cpp
    src/core/zodiacal_releasing.h
    src/core/zodiacal_releasing.cpp
    src/core/geodetic_equivalents.h
    src/core/geodetic_equivalents.cpp
    src/core/tropical_natal.h
    src/core/tropical_natal.cpp
    src/core/tajaka.h
    src/core/tajaka.cpp
    src/core/progression.h
    src/core/progression.cpp
    src/core/progressed_lunar_return.h
    src/core/progressed_lunar_return.cpp
    src/core/timezone_utils.h
    src/core/timezone_utils.cpp
)

target_link_libraries(dracoved_app PRIVATE Qt6::Widgets Qt6::Network Qt6::Svg Qt6::SvgWidgets)


