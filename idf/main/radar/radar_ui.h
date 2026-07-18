#pragma once

#include <stddef.h>
#include "radar_aircraft.h"

void radar_ui_init(void);
void radar_ui_tick(void);
void radar_ui_show_aircraft(const radar_aircraft_t *aircraft, size_t count);
