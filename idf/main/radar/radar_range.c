#include "radar_range.h"

#include <math.h>
#include <stdio.h>
#include "nvs.h"

#define RING_TO_OUTER 1.333333333f

/* The screen label marks the third ring (three quarters of the aircraft
 * plotting radius). Radar is deliberately kilometre-only: 5, 10, 15, 20,
 * then 25 km. */
static const radar_range_preset_t presets[] = {
    {5.0f, 5.0f * RING_TO_OUTER},
    {10.0f, 10.0f * RING_TO_OUTER},
    {15.0f, 15.0f * RING_TO_OUTER},
    {20.0f, 20.0f * RING_TO_OUTER},
    {25.0f, 25.0f * RING_TO_OUTER},
};
static uint8_t index_ = 2;
static bool miles_;

static void save(void) {
    nvs_handle_t nvs;
    if (nvs_open("planeradar", NVS_READWRITE, &nvs) == ESP_OK) {
        nvs_set_u8(nvs, "rangeIdx", index_);
        nvs_set_u8(nvs, "useMiles", miles_);
        nvs_commit(nvs);
        nvs_close(nvs);
    }
}

void radar_range_init(void) {
    nvs_handle_t nvs;
    uint8_t stored = index_;
    if (nvs_open("planeradar", NVS_READONLY, &nvs) == ESP_OK) {
        nvs_get_u8(nvs, "rangeIdx", &stored);
        nvs_close(nvs);
    }
    index_ = stored < (sizeof(presets) / sizeof(presets[0])) ? stored : 2;
    /* Deliberately disregard the legacy useMiles preference. */
    miles_ = false;
}

void radar_range_next(void) { index_ = (index_ + 1) % (sizeof(presets) / sizeof(presets[0])); save(); }
const radar_range_preset_t *radar_range_current(void) { return &presets[index_]; }
bool radar_range_use_miles(void) { return miles_; }
void radar_range_format_label(char *buffer, unsigned length) {
    snprintf(buffer, length, "%dkm", (int)lroundf(presets[index_].ring3_km));
}
