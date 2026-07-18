#pragma once

#include <stddef.h>

typedef enum {
    RADAR_AIRCRAFT_UNKNOWN,
    RADAR_AIRCRAFT_JET,
    RADAR_AIRCRAFT_PROP,
    RADAR_AIRCRAFT_GLIDER,
    RADAR_AIRCRAFT_HELICOPTER,
} radar_aircraft_kind_t;

typedef struct {
    float lat;
    float lon;
    float heading;
    float speed_knots;
    float altitude_feet;
    char callsign[9];
    char type[5];
    char hex[7];
    radar_aircraft_kind_t kind;
} radar_aircraft_t;

void radar_aircraft_start(void);
void radar_aircraft_refresh_ui(void);
void radar_aircraft_center(double *lat, double *lon);
