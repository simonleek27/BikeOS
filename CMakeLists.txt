cmake_minimum_required(VERSION 3.21)
project(BikeOSHost LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(bikeos_core STATIC
    core/src/canvas.cpp
    core/src/color_renderer.cpp
    core/src/font_data.cpp
    core/src/fonts.cpp
    core/src/framebuffer.cpp
    core/src/geo.cpp
    core/src/gps.cpp
    core/src/gps_simulator.cpp
    core/src/map_data.cpp
    core/src/map_pack.cpp
    core/src/map_renderer.cpp
    core/src/navigation.cpp
    core/src/nmea.cpp
    core/src/route_data.cpp
    protocol/src/protocol.cpp
)
target_include_directories(bikeos_core PUBLIC core/include protocol/include)
target_compile_options(bikeos_core PRIVATE -Wall -Wextra -Wpedantic -Werror)

enable_testing()
add_executable(bikeos_tests host/tests/test_main.cpp)
target_link_libraries(bikeos_tests PRIVATE bikeos_core)
target_compile_definitions(bikeos_tests PRIVATE BIKEOS_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
target_compile_options(bikeos_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
add_test(NAME bikeos_core_tests COMMAND bikeos_tests)

find_package(ZLIB REQUIRED)
add_executable(bikeos_color_preview
    host/preview/main.cpp
    host/simulator/png_writer.cpp
)
target_link_libraries(bikeos_color_preview PRIVATE bikeos_core ZLIB::ZLIB)
target_include_directories(bikeos_color_preview PRIVATE host/simulator)
target_compile_definitions(bikeos_color_preview PRIVATE BIKEOS_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
target_compile_options(bikeos_color_preview PRIVATE -Wall -Wextra -Wpedantic -Werror)

add_executable(bikeos_virtual_screen host/virtual_screen/engine.cpp)
target_link_libraries(bikeos_virtual_screen PRIVATE bikeos_core)
target_compile_definitions(bikeos_virtual_screen PRIVATE BIKEOS_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
target_compile_options(bikeos_virtual_screen PRIVATE -Wall -Wextra -Wpedantic -Werror)

# The interactive monochrome simulator needs SDL2; skip it when SDL2 is absent.
find_package(SDL2 CONFIG QUIET)
if(SDL2_FOUND)
    add_executable(bikeos_simulator
        host/simulator/main.cpp
        host/simulator/png_writer.cpp
    )
    target_link_libraries(bikeos_simulator PRIVATE bikeos_core SDL2::SDL2 ZLIB::ZLIB)
    target_compile_definitions(bikeos_simulator PRIVATE BIKEOS_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
    target_compile_options(bikeos_simulator PRIVATE -Wall -Wextra -Wpedantic -Werror)
else()
    message(STATUS "SDL2 not found; building without bikeos_simulator")
endif()
