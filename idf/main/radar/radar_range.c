#include "radar_range.h"

#include <math.h>
#include <stdio.h>
#include "nvs.h"

static const radar_range_preset_t presets[] = {
    {5.0f, 6.666667f}, {10.0f, 13.333334f}, {15.0f, 20.0f},
    {18.10512f, 24.14016f}, {24.14016f, 32.18688f},
};
static uint8_t index_ = 3;
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
    uint8_t miles = 0;
    if (nvs_open("planeradar", NVS_READONLY, &nvs) == ESP_OK) {
        nvs_get_u8(nvs, "rangeIdx", &stored);
        nvs_get_u8(nvs, "useMiles", &miles);
        nvs_close(nvs);
    }
    index_ = stored < (sizeof(presets) / sizeof(presets[0])) ? stored : 3;
    miles_ = miles != 0;
}

void radar_range_next(void) { index_ = (index_ + 1) % (sizeof(presets) / sizeof(presets[0])); save(); }
const radar_range_preset_t *radar_range_current(void) { return &presets[index_]; }
bool radar_range_use_miles(void) { return miles_; }
void radar_range_format_label(char *buffer, unsigned length) {
    const float value = miles_ ? presets[index_].ring3_km / 1.609344f : presets[index_].ring3_km;
    snprintf(buffer, length, "%d%s", (int)lroundf(value), miles_ ? "mi" : "km");
}
