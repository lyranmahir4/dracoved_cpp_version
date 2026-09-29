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
    src/gui/aspect_peak_graph.h
    src/gui/aspect_peak_graph.cpp
    src/core/ashtakavarga.h
    src/core/ashtakavarga.cpp
    src/gui/ashtakavarga_panel.h
    src/gui/ashtakavarga_panel.cpp
    src/core/vedic_benchmark.h
    src/core/vedic_benchmark.cpp
    src/core/vedic_gochar.h
    src/core/vedic_gochar.cpp
    src/core/vedic_planet_nature.h
    src/core/vedic_planet_nature.cpp
    src/gui/vedic_benchmark_panel.h
    src/gui/vedic_benchmark_panel.cpp
    src/gui/main_window_planetary_hours.cpp
    src/gui/main_window_zodiacal_releasing.cpp
    src/gui/main_window_geodetic_equivalents.cpp
    src/gui/main_window_solar_report.cpp
    src/gui/main_window_solar_transits.cpp
    src/gui/solar_transit_types.h
    src/gui/solar_transit_algorithms.h
    src/gui/solar_transit_calculation.cpp
    src/gui/solar_transit_panel.h
    src/gui/solar_transit_panel.cpp
    src/gui/solar_transit_table_item.h
    src/gui/solar_transit_activity.h
    src/gui/solar_transit_activity_panel.cpp
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
    src/gui/vedic_panel.h
    src/gui/vedic_panel.cpp
    src/gui/south_indian_chart.h
    src/gui/south_indian_chart.cpp
    src/gui/compact_controls.h
    src/gui/moorthi_panel.h
    src/gui/moorthi_panel.cpp
    src/gui/moorthi_graph_panel.h
    src/gui/moorthi_graph_panel.cpp
    src/gui/tara_panel.h
    src/gui/tara_panel.cpp
    src/gui/dasha_panel.h
    src/gui/dasha_panel.cpp
    src/core/vimshottari.h
    src/core/vimshottari.cpp
    src/core/moorthi.h
    src/core/moorthi.cpp
    src/gui/chart_wheel_widget.h
    src/gui/chart_wheel_widget.cpp
    src/gui/search_advanced_worker.cpp
    src/gui/search_event_algorithms.h
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
    src/core/progression_events.h
    src/core/progression_events.cpp
    src/gui/progression_events_panel.h
    src/gui/progression_events_panel.cpp
    src/core/progressed_lunar_return.h
    src/core/progressed_lunar_return.cpp
    src/core/vedic_nakshatra.h
    src/core/vedic_nakshatra.cpp
    src/core/timezone_utils.h
    src/core/timezone_utils.cpp
)

target_link_libraries(dracoved_app PRIVATE Qt6::Widgets Qt6::Network Qt6::Svg Qt6::SvgWidgets)


