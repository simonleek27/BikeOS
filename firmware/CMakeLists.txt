cmake_minimum_required(VERSION 3.16)

# The connected prototype was physically identified as an ESP32-S3 before this
# target was selected. Override with -DIDF_TARGET=... only for a verified board.
if(NOT DEFINED IDF_TARGET)
    set(IDF_TARGET "esp32s3")
endif()

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

set(PROJECT_VER "0.5.0")
project(bikeos)


# `idf.py -p PORT map-flash` writes the tiled city map (data/amsterdam.btil,
# built by tools/build_city_tiles.py) to the "map" partition without touching
# the app. `idf.py flash` leaves the map partition alone.
set(BIKEOS_CITY_MAP "${CMAKE_CURRENT_LIST_DIR}/../data/amsterdam.btil")
if(EXISTS "${BIKEOS_CITY_MAP}")
    idf_component_get_property(bikeos_flash_args esptool_py FLASH_ARGS)
    idf_component_get_property(bikeos_flash_sub_args esptool_py FLASH_SUB_ARGS)
    esptool_py_flash_target(map-flash "${bikeos_flash_args}" "${bikeos_flash_sub_args}" ALWAYS_PLAINTEXT)
    esptool_py_flash_to_partition(map-flash "map" "${BIKEOS_CITY_MAP}")
endif()
