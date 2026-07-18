#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "lvgl.h"

typedef struct {
    bool found;
    char status[64];
    char credit[96];
    char license[32];
    lv_img_dsc_t image;
} radar_photo_t;

/* Starts one live Wikimedia Commons lookup. The returned thumbnail is held only
 * in PSRAM and is discarded when radar_photo_clear() is called. */
bool radar_photo_start(const char *registration);
bool radar_photo_take(radar_photo_t *out);
void radar_photo_clear(void);
