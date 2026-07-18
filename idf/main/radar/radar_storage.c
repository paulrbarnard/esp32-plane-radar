#include "radar_storage.h"

#include <stdio.h>
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <strings.h>
#include <string.h>

#include "cJSON.h"
#include "esp_log.h"
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"
#include "driver/sdmmc_host.h"

#include "TCA9554PWR.h"

#define RADAR_SD_MOUNT "/sdcard"
#define RADAR_SD_CLK GPIO_NUM_2
#define RADAR_SD_CMD GPIO_NUM_1
#define RADAR_SD_D0  GPIO_NUM_42

static const char *TAG = "radar_sd";
static bool mounted;
static char type_catalogue_path[96] = RADAR_SD_MOUNT "/RADARDB/types.jsonl";
/* Storage lookups run from LVGL events on the small main task stack. */
static char type_json_buffer[512];
static char details_json_buffer[1024];

static void canonical_designator(const char *source, char destination[5])
{
    size_t written = 0;
    if (source) {
        while (*source && written < 4) {
            if (!isspace((unsigned char)*source)) {
                destination[written++] = (char)toupper((unsigned char)*source);
            }
            ++source;
        }
    }
    destination[written] = '\0';
}

/* macOS writes the expected names, but FAT cards can acquire differently-cased
 * directory entries. Discover the database rather than relying on its case. */
static bool find_type_catalogue(void)
{
    snprintf(type_catalogue_path, sizeof(type_catalogue_path), "%s/RADARDB/types.jsonl", RADAR_SD_MOUNT);
    DIR *root = opendir(RADAR_SD_MOUNT);
    if (!root) return false;
    struct dirent *entry;
    char database_dir[64] = {0};
    ESP_LOGI(TAG, "SD root contents:");
    while ((entry = readdir(root)) != NULL) {
        ESP_LOGI(TAG, "  %s", entry->d_name);
        if (strcasecmp(entry->d_name, "RADARDB") == 0) {
            snprintf(database_dir, sizeof(database_dir), "%s/%.48s", RADAR_SD_MOUNT, entry->d_name);
        }
    }
    closedir(root);
    if (!database_dir[0]) return false;

    DIR *database = opendir(database_dir);
    if (!database) return false;
    bool found = false;
    while ((entry = readdir(database)) != NULL) {
        if (strcasecmp(entry->d_name, "types.jsonl") == 0) {
            snprintf(type_catalogue_path, sizeof(type_catalogue_path), "%.64s/%.24s", database_dir, entry->d_name);
            found = true;
            break;
        }
    }
    closedir(database);
    if (found) ESP_LOGI(TAG, "SD type catalogue: %s", type_catalogue_path);
    return found;
}

bool radar_storage_init(void)
{
    esp_vfs_fat_sdmmc_mount_config_t mount_config = {
        .format_if_mount_failed = false,
        .max_files = 4,
        .allocation_unit_size = 16 * 1024,
    };
    sdmmc_host_t host = SDMMC_HOST_DEFAULT();
    sdmmc_slot_config_t slot = SDMMC_SLOT_CONFIG_DEFAULT();
    slot.width = 1;
    slot.clk = RADAR_SD_CLK;
    slot.cmd = RADAR_SD_CMD;
    slot.d0 = RADAR_SD_D0;
    slot.d1 = GPIO_NUM_NC;
    slot.d2 = GPIO_NUM_NC;
    slot.d3 = GPIO_NUM_NC;
    slot.flags |= SDMMC_SLOT_FLAG_INTERNAL_PULLUP;

    /* Waveshare routes SD D3/card-enable through the TCA9554 expander. */
    Set_EXIO(TCA9554_EXIO4, true);
    sdmmc_card_t *card = NULL;
    esp_err_t err = esp_vfs_fat_sdmmc_mount(RADAR_SD_MOUNT, &host, &slot, &mount_config, &card);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "SD card unavailable: %s", esp_err_to_name(err));
        return false;
    }
    mounted = true;
    ESP_LOGI(TAG, "SD mounted: %llu MB", ((uint64_t)card->csd.capacity * card->csd.sector_size) / (1024 * 1024));
    if (!find_type_catalogue()) ESP_LOGW(TAG, "RADARDB/types.jsonl was not found on this SD card");
    radar_storage_type_info_t test_type;
    ESP_LOGI(TAG, "SD type catalogue self-test (A320): %s",
             radar_storage_load_type("A320", &test_type) ? "PASS" : "FAIL");
    return true;
}

bool radar_storage_present(void)
{
    return mounted;
}

static void copy_json_string(cJSON *root, const char *name, char *destination, size_t size)
{
    cJSON *item = cJSON_GetObjectItemCaseSensitive(root, name);
    snprintf(destination, size, "%s", cJSON_IsString(item) ? item->valuestring : "");
}

bool radar_storage_load_type(const char *designator, radar_storage_type_info_t *info)
{
    if (!mounted || !designator || !designator[0] || !info) return false;
    char canonical[5];
    canonical_designator(designator, canonical);
    if (!canonical[0]) return false;
    FILE *file = fopen(type_catalogue_path, "rb");
    if (!file) {
        ESP_LOGW(TAG, "SD type catalogue is missing or unreadable (%s, errno %d)", type_catalogue_path, errno);
        return false;
    }
    bool found = false;
    while (fgets(type_json_buffer, sizeof(type_json_buffer), file)) {
        cJSON *root = cJSON_Parse(type_json_buffer);
        cJSON *type = root ? cJSON_GetObjectItemCaseSensitive(root, "type") : NULL;
        if (cJSON_IsString(type) && strcmp(type->valuestring, canonical) == 0) {
            memset(info, 0, sizeof(*info));
            copy_json_string(root, "manufacturer", info->manufacturer, sizeof(info->manufacturer));
            copy_json_string(root, "model", info->model, sizeof(info->model));
            copy_json_string(root, "airframe", info->airframe, sizeof(info->airframe));
            copy_json_string(root, "engine_type", info->engine_type, sizeof(info->engine_type));
            cJSON *count = cJSON_GetObjectItemCaseSensitive(root, "engine_count");
            info->engine_count = cJSON_IsNumber(count) ? (uint8_t)count->valueint : (uint8_t)atoi(cJSON_IsString(count) ? count->valuestring : "0");
            found = true;
            cJSON_Delete(root);
            break;
        }
        cJSON_Delete(root);
    }
    fclose(file);
    ESP_LOGI(TAG, "SD type lookup %s: %s", canonical, found ? "match" : "no match");
    return found;
}

bool radar_storage_load_details(const char *hex, radar_aircraft_details_t *details)
{
    if (!mounted || !hex || strlen(hex) != 6 || !details) return false;
    memset(details, 0, sizeof(*details));
    typedef struct __attribute__((packed)) { char hex[6]; uint32_t offset; } index_record_t;
    FILE *index = fopen(RADAR_SD_MOUNT "/RADARDB/aircraft.idx", "rb");
    FILE *file = fopen(RADAR_SD_MOUNT "/RADARDB/aircraft.jsonl", "rb");
    if (!index || !file) { if (index) fclose(index); if (file) fclose(file); return false; }
    fseek(index, 0, SEEK_END);
    long records = ftell(index) / (long)sizeof(index_record_t);
    long low = 0, high = records - 1;
    index_record_t record;
    bool found = false;
    while (low <= high) {
        long middle = low + (high - low) / 2;
        fseek(index, middle * (long)sizeof(record), SEEK_SET);
        if (fread(&record, sizeof(record), 1, index) != 1) break;
        int comparison = strncmp(hex, record.hex, 6);
        if (comparison == 0) { found = true; break; }
        if (comparison < 0) high = middle - 1; else low = middle + 1;
    }
    fclose(index);
    if (!found || fseek(file, record.offset, SEEK_SET) != 0) { fclose(file); return false; }
    size_t length = fread(details_json_buffer, 1, sizeof(details_json_buffer) - 1, file);
    fclose(file);
    details_json_buffer[length] = '\0';
    cJSON *root = cJSON_Parse(details_json_buffer);
    if (!root) return false;
    copy_json_string(root, "registration", details->registration, sizeof(details->registration));
    copy_json_string(root, "owner", details->owner, sizeof(details->owner));
    copy_json_string(root, "operator", details->operator_name, sizeof(details->operator_name));
    copy_json_string(root, "year", details->year, sizeof(details->year));
    copy_json_string(root, "photo", details->photo_path, sizeof(details->photo_path));
    copy_json_string(root, "source", details->source, sizeof(details->source));
    copy_json_string(root, "photo_credit", details->photo_credit, sizeof(details->photo_credit));
    copy_json_string(root, "photo_license", details->photo_license, sizeof(details->photo_license));
    cJSON_Delete(root);
    return true;
}
