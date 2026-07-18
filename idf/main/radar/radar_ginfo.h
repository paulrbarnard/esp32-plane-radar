#pragma once
#include <stdbool.h>
typedef struct { char registration[16], owner[64], operator_name[64], type[64], year[8]; } radar_ginfo_t;
bool radar_ginfo_lookup(const char *hex, radar_ginfo_t *out);
bool radar_ginfo_start(const char *hex);
bool radar_ginfo_take(radar_ginfo_t *out);
