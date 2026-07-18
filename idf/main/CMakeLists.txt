idf_component_register(
    SRCS "main.c" "radar/radar_range.c" "radar/radar_ui.c" "radar/radar_wifi.c" "radar/radar_aircraft.c" "radar/aircraft_type_db.c" "radar/radar_storage.c" "radar/radar_lvgl_fs.c"
         "radar/radar_ginfo.c" "radar/radar_photo.c" "EXIO/TCA9554PWR.c"
         "LCD_Driver/ST7701S.c"
         "Touch_Driver/CST820.c"
         "Touch_Driver/esp_lcd_touch/esp_lcd_touch.c"
         "LVGL_Driver/LVGL_Driver.c"
         "LVGL_Driver/lv_port_mem.c"
         "I2C_Driver/I2C_Driver.c"
    INCLUDE_DIRS "." "radar" "EXIO" "LCD_Driver" "Touch_Driver"
                 "Touch_Driver/esp_lcd_touch" "LVGL_Driver" "I2C_Driver"
    REQUIRES driver esp_driver_spi esp_driver_i2c esp_driver_gpio esp_driver_ledc
             esp_lcd esp_timer freertos lvgl__lvgl
             esp_http_client esp-tls mbedtls json nvs_flash esp_wifi fatfs sdmmc)
