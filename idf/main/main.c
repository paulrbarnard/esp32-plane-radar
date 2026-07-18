#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "TCA9554PWR.h"
#include "I2C_Driver.h"
#include "ST7701S.h"
#include "CST820.h"
#include "LVGL_Driver.h"
#include "radar_range.h"
#include "radar_ui.h"
#include "radar_wifi.h"
#include "radar_aircraft.h"
#include "radar_storage.h"
#include "radar_lvgl_fs.h"

void app_main(void)
{
    esp_err_t nvs_status = nvs_flash_init();
    if (nvs_status == ESP_ERR_NVS_NO_FREE_PAGES || nvs_status == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        nvs_status = nvs_flash_init();
    }
    ESP_ERROR_CHECK(nvs_status);
    I2C_Init();
    EXIO_Init();
    Set_EXIO(TCA9554_EXIO8, false);  // Buzzer is active-high on the expander.
    LCD_Init();
    Touch_Init();
    radar_storage_init();
    LVGL_Init();
    radar_lvgl_fs_init();
    radar_range_init();
    radar_ui_init();
    radar_wifi_start();
    radar_aircraft_start();
    ESP_LOGI("radar", "Native RGB display and CST820 touch initialised");

    while (true) {
        radar_aircraft_refresh_ui();
        radar_ui_tick();
        lv_timer_handler();
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}
