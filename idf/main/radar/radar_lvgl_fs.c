#include "radar_lvgl_fs.h"

#include <stdio.h>
#include <string.h>

#include "lvgl.h"

#define SD_IMAGE_ROOT "/sdcard"

static bool sd_ready(lv_fs_drv_t *drv)
{
    (void)drv;
    return true;
}

static void *sd_open(lv_fs_drv_t *drv, const char *path, lv_fs_mode_t mode)
{
    (void)drv;
    if ((mode & LV_FS_MODE_RD) == 0 || (mode & LV_FS_MODE_WR) != 0 ||
        !path || strncmp(path, "/RADARDB/photos/", 16) != 0 || strstr(path, "..")) {
        return NULL;
    }
    char full_path[128];
    snprintf(full_path, sizeof(full_path), "%s%.112s", SD_IMAGE_ROOT, path);
    return fopen(full_path, "rb");
}

static lv_fs_res_t sd_close(lv_fs_drv_t *drv, void *file)
{
    (void)drv;
    return fclose((FILE *)file) == 0 ? LV_FS_RES_OK : LV_FS_RES_FS_ERR;
}

static lv_fs_res_t sd_read(lv_fs_drv_t *drv, void *file, void *buffer, uint32_t bytes_to_read, uint32_t *bytes_read)
{
    (void)drv;
    const size_t read = fread(buffer, 1, bytes_to_read, (FILE *)file);
    if (bytes_read) *bytes_read = (uint32_t)read;
    return ferror((FILE *)file) ? LV_FS_RES_FS_ERR : LV_FS_RES_OK;
}

static lv_fs_res_t sd_seek(lv_fs_drv_t *drv, void *file, uint32_t position, lv_fs_whence_t whence)
{
    (void)drv;
    const int origin = whence == LV_FS_SEEK_CUR ? SEEK_CUR : whence == LV_FS_SEEK_END ? SEEK_END : SEEK_SET;
    return fseek((FILE *)file, (long)position, origin) == 0 ? LV_FS_RES_OK : LV_FS_RES_FS_ERR;
}

static lv_fs_res_t sd_tell(lv_fs_drv_t *drv, void *file, uint32_t *position)
{
    (void)drv;
    long current = ftell((FILE *)file);
    if (current < 0) return LV_FS_RES_FS_ERR;
    *position = (uint32_t)current;
    return LV_FS_RES_OK;
}

void radar_lvgl_fs_init(void)
{
    static lv_fs_drv_t driver;
    lv_fs_drv_init(&driver);
    driver.letter = 'S';
    driver.ready_cb = sd_ready;
    driver.open_cb = sd_open;
    driver.close_cb = sd_close;
    driver.read_cb = sd_read;
    driver.seek_cb = sd_seek;
    driver.tell_cb = sd_tell;
    lv_fs_drv_register(&driver);
}
