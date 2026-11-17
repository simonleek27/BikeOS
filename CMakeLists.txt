cmake_minimum_required(VERSION 3.21)
project(BikeOSHost LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(bikeos_core STATIC
    core/src/framebuffer.cpp
    core/src/geo.cpp
    core/src/gps.cpp
    core/src/gps_simulator.cpp
    core/src/map_data.cpp
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

find_package(SDL2 CONFIG REQUIRED)
find_package(ZLIB REQUIRED)
add_executable(bikeos_simulator
    host/simulator/main.cpp
    host/simulator/png_writer.cpp
)
target_link_libraries(bikeos_simulator PRIVATE bikeos_core SDL2::SDL2 ZLIB::ZLIB)
target_compile_definitions(bikeos_simulator PRIVATE BIKEOS_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
target_compile_options(bikeos_simulator PRIVATE -Wall -Wextra -Wpedantic -Werror)
