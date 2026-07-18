#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    char registration[16];
    char owner[64];
    char operator_name[64];
    char year[8];
    char photo_path[96];
    char source[48];
    char photo_credit[64];
    char photo_license[24];
} radar_aircraft_details_t;

typedef struct {
    char manufacturer[64];
    char model[96];
    char airframe[24];
    char engine_type[24];
    uint8_t engine_count;
} radar_storage_type_info_t;

/* Mounts a FAT-formatted SD card at /sdcard without ever formatting it. */
bool radar_storage_init(void);
bool radar_storage_present(void);
bool radar_storage_load_type(const char *designator, radar_storage_type_info_t *info);
bool radar_storage_load_details(const char *hex, radar_aircraft_details_t *details);
