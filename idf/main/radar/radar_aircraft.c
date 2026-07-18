#include "radar_aircraft.h"

#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cJSON.h"
#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "nvs.h"

#include "radar_range.h"
#include "radar_ui.h"
#include "radar_wifi.h"

#define RADAR_AIRCRAFT_MAX 64
#define RADAR_RESPONSE_MAX (64 * 1024)

static const char *TAG = "adsb";
static radar_aircraft_t aircraft[RADAR_AIRCRAFT_MAX];
/* The main task has a small system-provided stack. Keep UI hand-off storage
 * out of it; NVS and LVGL both require additional stack during a refresh. */
static radar_aircraft_t ui_snapshot[RADAR_AIRCRAFT_MAX];
static size_t aircraft_count;
static SemaphoreHandle_t aircraft_lock;
static uint32_t generation;
static uint32_t displayed_generation;

typedef struct { char *data; size_t length; bool overflow; } response_buffer_t;

static bool type_starts_with(const char *type, const char *prefix)
{
    return strncmp(type, prefix, strlen(prefix)) == 0;
}

static radar_aircraft_kind_t classify_aircraft(const char *category, const char *type, float speed_knots)
{
    /* ADS-B emitter categories are authoritative where supplied. */
    if (category && strcmp(category, "A7") == 0) return RADAR_AIRCRAFT_HELICOPTER;
    if (category && strcmp(category, "B1") == 0) return RADAR_AIRCRAFT_GLIDER;

    /* Common ICAO type-code families fill in feeds that omit category. */
    if (type_starts_with(type, "R22") || type_starts_with(type, "R44") ||
        type_starts_with(type, "EC") || type_starts_with(type, "H1") ||
        type_starts_with(type, "S92") || type_starts_with(type, "B06") ||
        type_starts_with(type, "A10") || type_starts_with(type, "A13") ||
        type_starts_with(type, "AW")) return RADAR_AIRCRAFT_HELICOPTER;
    if (type_starts_with(type, "GL") || type_starts_with(type, "DG") ||
        type_starts_with(type, "ASK") || type_starts_with(type, "ASW") ||
        type_starts_with(type, "LS") || type_starts_with(type, "G10")) return RADAR_AIRCRAFT_GLIDER;
    if (type_starts_with(type, "C1") || type_starts_with(type, "C2") ||
        type_starts_with(type, "PA") || type_starts_with(type, "DA") ||
        type_starts_with(type, "SR") || type_starts_with(type, "BE") ||
        type_starts_with(type, "P28") || type_starts_with(type, "TBM")) return RADAR_AIRCRAFT_PROP;
    if (type_starts_with(type, "A3") || type_starts_with(type, "B7") ||
        type_starts_with(type, "E1") || type_starts_with(type, "E2") ||
        type_starts_with(type, "E3") || type_starts_with(type, "CRJ") ||
        type_starts_with(type, "CL") || type_starts_with(type, "GLF") ||
        type_starts_with(type, "LJ") || speed_knots >= 220.0f) return RADAR_AIRCRAFT_JET;
    return RADAR_AIRCRAFT_UNKNOWN;
}

static void load_location(double *lat, double *lon)
{
    *lat = 52.3676;
    *lon = 4.9041;
    nvs_handle_t nvs;
    if (nvs_open("radar", NVS_READONLY, &nvs) != ESP_OK) return;
    size_t length = sizeof(*lat);
    bool valid = nvs_get_blob(nvs, "lat", lat, &length) == ESP_OK && length == sizeof(*lat);
    length = sizeof(*lon);
    valid = valid && nvs_get_blob(nvs, "lon", lon, &length) == ESP_OK && length == sizeof(*lon);
    nvs_close(nvs);
    if (!valid || *lat < -90 || *lat > 90 || *lon < -180 || *lon > 180) { *lat = 52.3676; *lon = 4.9041; }
}

static esp_err_t on_http_event(esp_http_client_event_t *event)
{
    if (event->event_id != HTTP_EVENT_ON_DATA) return ESP_OK;
    response_buffer_t *response = event->user_data;
    if (response->length + event->data_len >= RADAR_RESPONSE_MAX) { response->overflow = true; return ESP_FAIL; }
    memcpy(response->data + response->length, event->data, event->data_len);
    response->length += event->data_len;
    return ESP_OK;
}

static size_t parse_aircraft(const char *json, size_t length, radar_aircraft_t *out)
{
    cJSON *root = cJSON_ParseWithLength(json, length);
    cJSON *items = root ? cJSON_GetObjectItemCaseSensitive(root, "ac") : NULL;
    size_t count = 0;
    cJSON *item;
    cJSON_ArrayForEach(item, items) {
        cJSON *lat = cJSON_GetObjectItemCaseSensitive(item, "lat");
        cJSON *lon = cJSON_GetObjectItemCaseSensitive(item, "lon");
        cJSON *baro = cJSON_GetObjectItemCaseSensitive(item, "alt_baro");
        if (count == RADAR_AIRCRAFT_MAX || !cJSON_IsNumber(lat) || !cJSON_IsNumber(lon) ||
            (cJSON_IsString(baro) && strcmp(baro->valuestring, "ground") == 0)) continue;
        out[count].lat = (float)lat->valuedouble;
        out[count].lon = (float)lon->valuedouble;
        cJSON *heading = cJSON_GetObjectItemCaseSensitive(item, "true_heading");
        if (!cJSON_IsNumber(heading)) heading = cJSON_GetObjectItemCaseSensitive(item, "mag_heading");
        if (!cJSON_IsNumber(heading)) heading = cJSON_GetObjectItemCaseSensitive(item, "track");
        out[count].heading = cJSON_IsNumber(heading) ? (float)heading->valuedouble : 0.0f;
        cJSON *speed = cJSON_GetObjectItemCaseSensitive(item, "gs");
        if (!cJSON_IsNumber(speed)) speed = cJSON_GetObjectItemCaseSensitive(item, "tas");
        if (!cJSON_IsNumber(speed)) speed = cJSON_GetObjectItemCaseSensitive(item, "ias");
        out[count].speed_knots = cJSON_IsNumber(speed) ? (float)speed->valuedouble : 0.0f;
        out[count].altitude_feet = cJSON_IsNumber(baro) ? (float)baro->valuedouble : 0.0f;
        cJSON *flight = cJSON_GetObjectItemCaseSensitive(item, "flight");
        cJSON *hex = cJSON_GetObjectItemCaseSensitive(item, "hex");
        cJSON *type = cJSON_GetObjectItemCaseSensitive(item, "t");
        cJSON *category = cJSON_GetObjectItemCaseSensitive(item, "category");
        snprintf(out[count].callsign, sizeof(out[count].callsign), "%s", cJSON_IsString(flight) ? flight->valuestring : (cJSON_IsString(hex) ? hex->valuestring : ""));
        snprintf(out[count].type, sizeof(out[count].type), "%s", cJSON_IsString(type) ? type->valuestring : "");
        /* ADS-B providers normally send ICAO designators in upper case, but
         * canonicalise them before using them as a local catalogue key. */
        for (size_t n = 0; out[count].type[n]; ++n) {
            out[count].type[n] = (char)toupper((unsigned char)out[count].type[n]);
        }
        for (size_t n = strlen(out[count].type); n && out[count].type[n - 1] == ' '; --n) {
            out[count].type[n - 1] = '\0';
        }
        snprintf(out[count].hex, sizeof(out[count].hex), "%s", cJSON_IsString(hex) ? hex->valuestring : "");
        out[count].kind = classify_aircraft(cJSON_IsString(category) ? category->valuestring : NULL,
                                            out[count].type, out[count].speed_knots);
        for (size_t n = strlen(out[count].callsign); n && out[count].callsign[n - 1] == ' '; --n) out[count].callsign[n - 1] = '\0';
        ++count;
    }
    cJSON_Delete(root);
    return count;
}

static void fetch_aircraft(void)
{
    double lat, lon;
    load_location(&lat, &lon);
    char url[160];
    snprintf(url, sizeof(url), "https://opendata.adsb.fi/api/v3/lat/%.6f/lon/%.6f/dist/%.1f", lat, lon, radar_range_current()->outer_km / 1.852f);
    char *data = malloc(RADAR_RESPONSE_MAX);
    if (!data) return;
    response_buffer_t response = {.data = data};
    esp_http_client_config_t config = {.url = url, .event_handler = on_http_event, .user_data = &response, .timeout_ms = 10000, .crt_bundle_attach = esp_crt_bundle_attach, .buffer_size = 1024};
    esp_http_client_handle_t client = esp_http_client_init(&config);
    esp_err_t err = esp_http_client_perform(client);
    int status = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);
    if (err != ESP_OK || status != 200 || response.overflow) { ESP_LOGW(TAG, "request failed: %s HTTP %d", esp_err_to_name(err), status); free(data); return; }
    radar_aircraft_t parsed[RADAR_AIRCRAFT_MAX];
    size_t count = parse_aircraft(data, response.length, parsed);
    free(data);
    xSemaphoreTake(aircraft_lock, portMAX_DELAY);
    memcpy(aircraft, parsed, count * sizeof(parsed[0]));
    aircraft_count = count;
    ++generation;
    xSemaphoreGive(aircraft_lock);
    ESP_LOGI(TAG, "%u aircraft", (unsigned)count);
}

static void radar_aircraft_task(void *arg)
{
    (void)arg;
    for (;;) { if (radar_wifi_connected()) fetch_aircraft(); vTaskDelay(pdMS_TO_TICKS(3000)); }
}

void radar_aircraft_start(void)
{
    aircraft_lock = xSemaphoreCreateMutex();
    configASSERT(aircraft_lock);
    xTaskCreate(radar_aircraft_task, "adsb_fetch", 8192, NULL, 4, NULL);
}

void radar_aircraft_center(double *lat, double *lon)
{
    load_location(lat, lon);
}

static void refresh_ui(bool force)
{
    size_t count;
    xSemaphoreTake(aircraft_lock, portMAX_DELAY);
    if (!force && generation == displayed_generation) { xSemaphoreGive(aircraft_lock); return; }
    count = aircraft_count;
    memcpy(ui_snapshot, aircraft, count * sizeof(ui_snapshot[0]));
    displayed_generation = generation;
    xSemaphoreGive(aircraft_lock);
    radar_ui_show_aircraft(ui_snapshot, count);
}

void radar_aircraft_refresh_ui(void)
{
    refresh_ui(false);
}

void radar_aircraft_redraw_ui(void)
{
    refresh_ui(true);
}
