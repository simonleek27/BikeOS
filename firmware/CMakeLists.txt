cmake_minimum_required(VERSION 3.16)

# The connected prototype was physically identified as an ESP32-S3 before this
# target was selected. Override with -DIDF_TARGET=... only for a verified board.
if(NOT DEFINED IDF_TARGET)
    set(IDF_TARGET "esp32s3")
endif()

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

set(PROJECT_VER "0.3.0")
project(bikeos)

