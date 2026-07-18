#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    const char *manufacturer;
    const char *model;
    const char *airframe;
    const char *engine_type;
    uint8_t engine_count;
} radar_type_info_t;

/* Looks up the ICAO four-character aircraft type designator. */
bool radar_type_lookup(const char *designator, radar_type_info_t *info);
