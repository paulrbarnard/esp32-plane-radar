file(GLOB_RECURSE radar_sources "${CMAKE_CURRENT_LIST_DIR}/../src/*.cpp")
file(GLOB_RECURSE lovyangfx_sources
    "/Volumes/Shed Data/ownCloud/Development/ESP32/shared-components/LovyanGFX/src/*.c"
    "/Volumes/Shed Data/ownCloud/Development/ESP32/shared-components/LovyanGFX/src/*.cpp")

set(wifimanager_sources
    "/Volumes/Shed Data/ownCloud/Development/ESP32/shared-components/WiFiManager/WiFiManager.cpp"
    "/Volumes/Shed Data/ownCloud/Development/ESP32/shared-components/WiFiManager/wm_consts_en.cpp"
    "/Volumes/Shed Data/ownCloud/Development/ESP32/shared-components/WiFiManager/wm_strings_en.cpp")

idf_component_register(
    SRCS ${radar_sources} ${lovyangfx_sources} ${wifimanager_sources}
    INCLUDE_DIRS
        "../include"
        "/Volumes/Shed Data/ownCloud/Development/ESP32/shared-components/LovyanGFX/src"
        "/Volumes/Shed Data/ownCloud/Development/ESP32/shared-components/WiFiManager"
        "/Volumes/Shed Data/ownCloud/Development/ESP32/shared-components/ArduinoJson/src"
    REQUIRES arduino esp_lcd driver esp_timer esp_wifi nvs_flash)
