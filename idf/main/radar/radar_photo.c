#include "radar_photo.h"

#include <stdio.h>
#include <string.h>

#include "cJSON.h"
#include "esp_crt_bundle.h"
#include "esp_heap_caps.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp32s3/rom/tjpgd.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define TAG "radar_photo"
#define JPEG_BUFFER_SIZE (192 * 1024)
#define JPEG_WORK_SIZE 4096
#define RADAR_PHOTO_PROXY "http://192.168.0.52:8088/photo?registration="

static uint8_t *jpeg_buffer;
static size_t jpeg_used;
static radar_photo_t pending;
static volatile bool busy, ready, discard_result;
static char requested_registration[20];
static int64_t last_request_us;
static int64_t rate_limit_until_us;

static void free_photo(void)
{
    if (pending.image.data) heap_caps_free((void *)pending.image.data);
    memset(&pending, 0, sizeof(pending));
}

static bool get_jpeg(const char *url)
{
    jpeg_buffer = heap_caps_malloc(JPEG_BUFFER_SIZE, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!jpeg_buffer) return false;
    jpeg_used = 0;
    esp_http_client_config_t config = {
        .url = url, .crt_bundle_attach = esp_crt_bundle_attach, .buffer_size = 4096,
        .timeout_ms = 20000, .user_agent = "PlaneRadar/1.0 (personal ESP32 display)",
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);
    esp_err_t result = esp_http_client_open(client, 0);
    int64_t expected = -1;
    if (result == ESP_OK) {
        expected = esp_http_client_fetch_headers(client);
        while (jpeg_used < JPEG_BUFFER_SIZE) {
            const int read = esp_http_client_read(client, (char *)jpeg_buffer + jpeg_used,
                                                  JPEG_BUFFER_SIZE - jpeg_used);
            if (read < 0) { result = ESP_FAIL; break; }
            if (read == 0) break;
            jpeg_used += (size_t)read;
        }
    }
    const int status = esp_http_client_get_status_code(client);
    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    if (result != ESP_OK || status < 200 || status >= 300 || jpeg_used < 4 ||
        jpeg_used == JPEG_BUFFER_SIZE || jpeg_buffer[0] != 0xff || jpeg_buffer[1] != 0xd8 ||
        (expected > 0 && jpeg_used != (size_t)expected)) {
        ESP_LOGW(TAG, "Commons image transfer failed: err=%s status=%d bytes=%u expected=%lld jpeg=%d",
                 esp_err_to_name(result), status, (unsigned)jpeg_used, (long long)expected,
                 jpeg_used >= 2 && jpeg_buffer[0] == 0xff && jpeg_buffer[1] == 0xd8);
        if (status == 429) {
            rate_limit_until_us = esp_timer_get_time() + 60000000;
            snprintf(pending.status, sizeof(pending.status), "Wikimedia rate-limited — try again in a minute");
        }
        heap_caps_free(jpeg_buffer);
        jpeg_buffer = NULL;
        return false;
    }
    return true;
}

typedef struct { const uint8_t *data; size_t size, offset; } jpeg_input_t;
typedef struct { lv_color_t *pixels; uint16_t width, height; } jpeg_output_t;
typedef struct { jpeg_input_t input; jpeg_output_t output; } jpeg_context_t;

static UINT jpeg_input(JDEC *decoder, BYTE *buffer, UINT length)
{
    jpeg_context_t *context = decoder->device;
    jpeg_input_t *input = &context->input;
    size_t available = input->size - input->offset;
    if (length > available) length = available;
    if (buffer) memcpy(buffer, input->data + input->offset, length);
    input->offset += length;
    return length;
}

static UINT jpeg_output(JDEC *decoder, void *bitmap, JRECT *rect)
{
    jpeg_context_t *context = decoder->device;
    jpeg_output_t *output = &context->output;
    if (rect->right >= output->width || rect->bottom >= output->height) return 0;
    const uint8_t *source = bitmap;
    const uint16_t row_width = rect->right - rect->left + 1;
    const uint16_t row_count = rect->bottom - rect->top + 1;
    for (uint16_t y = 0; y < row_count; ++y) {
        for (uint16_t x = 0; x < row_width; ++x) {
            const size_t pixel = ((size_t)y * row_width + x) * 3;
            output->pixels[(rect->top + y) * output->width + rect->left + x] =
                lv_color_make(source[pixel], source[pixel + 1], source[pixel + 2]);
        }
    }
    return 1;
}

static bool decode_jpeg(void)
{
    uint8_t *work = heap_caps_malloc(JPEG_WORK_SIZE, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (!work) return false;
    JDEC decoder;
    jpeg_context_t context = {.input = {.data = jpeg_buffer, .size = jpeg_used}};
    if (jd_prepare(&decoder, jpeg_input, work, JPEG_WORK_SIZE, &context) != JDR_OK) {
        heap_caps_free(work);
        return false;
    }
    /* Decode at the JPEG's native thumbnail dimensions. TJpgDec's output
     * callback coordinates remain native on some ROM builds even with a
     * scaling request, which corrupts a smaller output buffer. LVGL scales
     * the safe full-size image for the panel afterwards. */
    uint16_t width = decoder.width;
    uint16_t height = decoder.height;
    if (!width || !height || width > 320 || height > 240) {
        heap_caps_free(work);
        return false;
    }
    lv_color_t *pixels = heap_caps_malloc((size_t)width * height * sizeof(lv_color_t),
                                          MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!pixels) { heap_caps_free(work); return false; }
    context.output = (jpeg_output_t){.pixels = pixels, .width = width, .height = height};
    if (jd_decomp(&decoder, jpeg_output, 0) != JDR_OK) {
        heap_caps_free(pixels);
        heap_caps_free(work);
        return false;
    }
    /* Keep LVGL on its simple native-image draw path. Scaling an in-memory
     * PSRAM image through lv_img_set_zoom proved unreliable on this RGB
     * display, whereas a compact pre-scaled RGB565 buffer is straightforward. */
    uint16_t scaled_width = width;
    uint16_t scaled_height = height;
    if (scaled_width > 160) {
        scaled_height = (uint16_t)((uint32_t)scaled_height * 160 / scaled_width);
        scaled_width = 160;
    }
    if (scaled_height > 120) {
        scaled_width = (uint16_t)((uint32_t)scaled_width * 120 / scaled_height);
        scaled_height = 120;
    }
    if (scaled_width != width || scaled_height != height) {
        lv_color_t *scaled = heap_caps_malloc((size_t)scaled_width * scaled_height * sizeof(lv_color_t),
                                              MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!scaled) { heap_caps_free(pixels); heap_caps_free(work); return false; }
        for (uint16_t y = 0; y < scaled_height; ++y) {
            const uint16_t source_y = (uint16_t)((uint32_t)y * height / scaled_height);
            for (uint16_t x = 0; x < scaled_width; ++x) {
                const uint16_t source_x = (uint16_t)((uint32_t)x * width / scaled_width);
                scaled[(size_t)y * scaled_width + x] = pixels[(size_t)source_y * width + source_x];
            }
        }
        heap_caps_free(pixels);
        pixels = scaled;
        width = scaled_width;
        height = scaled_height;
    }
    pending.image.header.always_zero = 0;
    pending.image.header.w = width;
    pending.image.header.h = height;
    pending.image.header.cf = LV_IMG_CF_TRUE_COLOR;
    pending.image.data_size = (uint32_t)width * height * sizeof(lv_color_t);
    pending.image.data = (const uint8_t *)pixels;
    ESP_LOGI(TAG, "JPEG pixels %04x %04x %04x", (unsigned)pixels[0].full,
             (unsigned)pixels[(size_t)(height / 2) * width + width / 2].full,
             (unsigned)pixels[(size_t)(height - 1) * width + width - 1].full);
    heap_caps_free(work);
    return true;
}

static bool lookup(const char *registration)
{
    char url[128];
    snprintf(url, sizeof(url), "%s%s", RADAR_PHOTO_PROXY, registration);
    if (!get_jpeg(url)) { ESP_LOGW(TAG, "LAN photo proxy request failed for %s", registration); return false; }
    if (!decode_jpeg()) { ESP_LOGW(TAG, "Proxy JPEG decode failed for %s (%u bytes)", registration, (unsigned)jpeg_used); return false; }
    snprintf(pending.credit, sizeof(pending.credit), "Wikimedia Commons via LAN proxy");
    pending.found = true;
    ESP_LOGI(TAG, "Proxy photo ready for %s (%ux%u)", registration,
             pending.image.header.w, pending.image.header.h);
    return true;
}

static void worker(void *arg)
{
    char registration[sizeof(requested_registration)];
    snprintf(registration, sizeof(registration), "%s", (const char *)arg);
    free_photo();
    if (!lookup(registration) && !pending.status[0]) {
        snprintf(pending.status, sizeof(pending.status), "Photo not available from Wikimedia");
    }
    if (jpeg_buffer) { heap_caps_free(jpeg_buffer); jpeg_buffer = NULL; }
    if (discard_result) free_photo(); else ready = true;
    busy = false;
    vTaskDelete(NULL);
}

bool radar_photo_start(const char *registration)
{
    if (busy || !registration || !registration[0]) return false;
    ready = false;
    discard_result = false;
    const int64_t now = esp_timer_get_time();
    /* Commons asked clients to back off after HTTP 429. A one-shot panel does
     * not need rapid repeated calls, and this avoids treating throttling as a
     * missing photograph. */
    if ((rate_limit_until_us && now < rate_limit_until_us) ||
        (last_request_us && now - last_request_us < 15000000)) {
        free_photo();
        snprintf(pending.status, sizeof(pending.status), "Wikimedia cooldown — try again shortly");
        ready = true;
        return true;
    }
    last_request_us = now;
    snprintf(requested_registration, sizeof(requested_registration), "%s", registration);
    /* The certificate check can spend several seconds in CPU-heavy elliptic
     * curve arithmetic. Core 0 hosts Wi-Fi/idle watchdog work, so keep this
     * optional UI request on core 1. */
    busy = xTaskCreatePinnedToCore(worker, "commons_photo", 12288, requested_registration,
                                   5, NULL, 1) == pdPASS;
    return busy;
}

bool radar_photo_take(radar_photo_t *out)
{
    if (!ready) return false;
    ready = false;
    *out = pending;
    return true;
}

void radar_photo_clear(void)
{
    discard_result = true;
    ready = false;
    if (!busy) free_photo();
}
