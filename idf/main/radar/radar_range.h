#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef struct { float ring3_km; float outer_km; } radar_range_preset_t;

void radar_range_init(void);
void radar_range_next(void);
const radar_range_preset_t *radar_range_current(void);
bool radar_range_use_miles(void);
void radar_range_format_label(char *buffer, unsigned length);
