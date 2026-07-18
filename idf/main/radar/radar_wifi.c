#include "radar_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "nvs.h"
#include "nvs_flash.h"

static bool connected;
static void on_wifi(void *arg, esp_event_base_t base, int32_t id, void *data) {
    (void)arg; (void)base; (void)data;
    if (id == WIFI_EVENT_STA_START) esp_wifi_connect();
    if (id == WIFI_EVENT_STA_DISCONNECTED) { connected = false; esp_wifi_connect(); }
}
static void on_ip(void *arg, esp_event_base_t base, int32_t id, void *data) {
    (void)arg; (void)base; (void)id; (void)data; connected = true;
}
bool radar_wifi_start(void) {
    esp_netif_init();
    esp_event_loop_create_default();
    esp_netif_create_default_wifi_sta();
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    if (esp_wifi_init(&cfg) != ESP_OK) return false;
    esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, on_wifi, NULL, NULL);
    esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, on_ip, NULL, NULL);
    esp_wifi_set_storage(WIFI_STORAGE_FLASH);
    esp_wifi_set_mode(WIFI_MODE_STA);
    return esp_wifi_start() == ESP_OK;
}
bool radar_wifi_connected(void) { return connected; }

static void erase_settings_namespace(const char *name)
{
    nvs_handle_t handle;
    if (nvs_open(name, NVS_READWRITE, &handle) != ESP_OK) return;
    nvs_erase_all(handle);
    nvs_commit(handle);
    nvs_close(handle);
}

bool radar_wifi_clear_settings(void)
{
    /* esp_wifi_restore() removes the credentials and other Wi-Fi settings
     * persisted by ESP-IDF. The two application namespaces retain Radar's
     * location, range and units, so clear those as the original BOOT hold did. */
    const esp_err_t result = esp_wifi_restore();
    erase_settings_namespace("radar");
    erase_settings_namespace("planeradar");
    return result == ESP_OK;
}
