#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_system.h"
#include "nvs_flash.h"
#include "driver/gpio.h"
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

#define RADAR_BOOT_BUTTON GPIO_NUM_0
#define RADAR_BOOT_DEBOUNCE_US 30000
#define RADAR_BOOT_MIN_PRESS_US 40000
#define RADAR_BOOT_RESET_HOLD_US 3000000

typedef enum {
    RADAR_BOOT_NONE,
    RADAR_BOOT_RANGE,
    RADAR_BOOT_RESET_SETTINGS,
} radar_boot_action_t;

static void boot_button_init(void)
{
    const gpio_config_t config = {
        .pin_bit_mask = 1ULL << RADAR_BOOT_BUTTON,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&config));
}

/* GPIO0 is the Waveshare BOOT button and is active low. Poll it from the
 * LVGL-owning main task so changing range cannot race the renderer. */
static radar_boot_action_t boot_button_take_action(void)
{
    static bool raw_down;
    static bool stable_down;
    static int64_t raw_changed_at;
    static int64_t pressed_at;
    static bool hold_handled;
    const int64_t now = esp_timer_get_time();
    const bool down = gpio_get_level(RADAR_BOOT_BUTTON) == 0;

    if (down != raw_down) {
        raw_down = down;
        raw_changed_at = now;
    }
    if (raw_down == stable_down) {
        if (stable_down && !hold_handled && now - pressed_at >= RADAR_BOOT_RESET_HOLD_US) {
            hold_handled = true;
            return RADAR_BOOT_RESET_SETTINGS;
        }
        return RADAR_BOOT_NONE;
    }
    if (now - raw_changed_at < RADAR_BOOT_DEBOUNCE_US) return RADAR_BOOT_NONE;

    stable_down = raw_down;
    if (stable_down) {
        pressed_at = now;
        hold_handled = false;
        return RADAR_BOOT_NONE;
    }
    if (!hold_handled && now - pressed_at >= RADAR_BOOT_MIN_PRESS_US) return RADAR_BOOT_RANGE;
    return RADAR_BOOT_NONE;
}

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
    boot_button_init();
    radar_ui_init();
    radar_wifi_start();
    radar_aircraft_start();
    ESP_LOGI("radar", "Native RGB display and CST820 touch initialised");

    while (true) {
        const radar_boot_action_t boot_action = boot_button_take_action();
        if (boot_action == RADAR_BOOT_RESET_SETTINGS) {
            ESP_LOGW("radar", "BOOT held: clearing Wi-Fi and Radar settings, restarting");
            radar_wifi_clear_settings();
            vTaskDelay(pdMS_TO_TICKS(100));
            esp_restart();
        }
        if (boot_action == RADAR_BOOT_RANGE) {
            radar_range_next();
            radar_ui_update_range();
            radar_aircraft_redraw_ui();
            ESP_LOGI("radar", "BOOT: range changed");
        }
        radar_aircraft_refresh_ui();
        radar_ui_tick();
        lv_timer_handler();
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}
