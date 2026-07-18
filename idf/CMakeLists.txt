cmake_minimum_required(VERSION 3.16)

# Reuse the LVGL component already proven with the LandyGauge hardware base.
set(EXTRA_COMPONENT_DIRS
    "/Volumes/Shed Data/ownCloud/Development/ESP32/Defender/LandyGauge/components/lvgl__lvgl")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(plane_radar_idf)
