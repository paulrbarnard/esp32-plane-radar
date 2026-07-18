cmake_minimum_required(VERSION 3.16)

# Shared development components.  Projects reference these; they do not vendor
# framework or library sources into their own repositories.
set(EXTRA_COMPONENT_DIRS
    "/Volumes/Shed Data/ownCloud/Development/ESP32/shared-components/arduino-esp32")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(plane_radar)
