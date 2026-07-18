#include "LVGL_Driver.h"

static const char *LVGL_TAG = "LVGL";   
lv_disp_draw_buf_t disp_buf; // contains internal graphic buffer(s) called draw buffer(s)
lv_disp_drv_t disp_drv;      // contains callback functions

lv_indev_drv_t indev_drv;
esp_timer_handle_t lvgl_tick_timer = NULL;


static void *buf1 = NULL;
static void *buf2 = NULL;             

/* Keep RGB scan-out on its fixed frame buffer. Full-screen PSRAM transfers
 * during active scan-out can starve the LCD peripheral and shift the image. */
#define LVGL_DRAW_BUFFER_LINES 40


void example_lvgl_flush_cb(lv_disp_drv_t *drv, const lv_area_t *area, lv_color_t *color_map)
{
    esp_lcd_panel_handle_t panel_handle = (esp_lcd_panel_handle_t) drv->user_data;
    int offsetx1 = area->x1;
    int offsetx2 = area->x2;
    int offsety1 = area->y1;
    int offsety2 = area->y2;
    // `color_map` is one of the RGB driver's own full-screen frame buffers.
    // Handing it back selects that complete buffer for the next scan-out.
    esp_lcd_panel_draw_bitmap(panel_handle, offsetx1, offsety1, offsetx2 + 1, offsety2 + 1, color_map);
    lv_disp_flush_ready(drv);
}

void example_increase_lvgl_tick(void *arg)
{
    /* Tell LVGL how many milliseconds has elapsed */
    lv_tick_inc(EXAMPLE_LVGL_TICK_PERIOD_MS);
}

/*Read the touchpad*/
void example_touchpad_read( lv_indev_drv_t * drv, lv_indev_data_t * data )
{
    static bool was_pressed;
    static uint32_t last_sample_ms;
    uint16_t touchpad_x[1] = {0};
    uint16_t touchpad_y[1] = {0};
    uint8_t touchpad_cnt = 0;

    /* Read touch controller data */
    esp_lcd_touch_read_data(drv->user_data);

    /* Get coordinates */
    bool touchpad_pressed = esp_lcd_touch_get_coordinates(drv->user_data, touchpad_x, touchpad_y, NULL, &touchpad_cnt, 1);

    if (touchpad_pressed && touchpad_cnt > 0) {
        data->point.x = touchpad_x[0];
        data->point.y = touchpad_y[0];
        data->state = LV_INDEV_STATE_PR;
        was_pressed = true;
        last_sample_ms = lv_tick_get();
    } else if (was_pressed && lv_tick_elaps(last_sample_ms) < 50) {
        /* CST820 reports a tap as a short sample. Keep it pressed long enough
         * for LVGL to produce one clean click, then release it. */
        data->state = LV_INDEV_STATE_PR;
    } else {
        data->state = LV_INDEV_STATE_REL;
        was_pressed = false;
    }
}
void LVGL_Init(void)
{
    ESP_LOGI(LVGL_TAG, "Initialize LVGL library");
    lv_init();
#if CONFIG_EXAMPLE_DOUBLE_FB
    ESP_LOGI(LVGL_TAG, "Use RGB panel frame buffers as LVGL draw buffers");
    ESP_ERROR_CHECK(esp_lcd_rgb_panel_get_frame_buffer(panel_handle, 2, &buf1, &buf2));
    // initialize LVGL draw buffers - full screen size
    lv_disp_draw_buf_init(&disp_buf, buf1, buf2, EXAMPLE_LCD_H_RES * EXAMPLE_LCD_V_RES);
#else
    ESP_LOGI(LVGL_TAG, "Allocate two PSRAM partial render buffers (%d lines)",
             LVGL_DRAW_BUFFER_LINES);
    buf1 = heap_caps_malloc(EXAMPLE_LCD_H_RES * LVGL_DRAW_BUFFER_LINES * sizeof(lv_color_t), MALLOC_CAP_SPIRAM);
    assert(buf1);
    buf2 = heap_caps_malloc(EXAMPLE_LCD_H_RES * LVGL_DRAW_BUFFER_LINES * sizeof(lv_color_t), MALLOC_CAP_SPIRAM);
    assert(buf2);
    lv_disp_draw_buf_init(&disp_buf, buf1, buf2, EXAMPLE_LCD_H_RES * LVGL_DRAW_BUFFER_LINES);
#endif // CONFIG_EXAMPLE_DOUBLE_FB

    ESP_LOGI(LVGL_TAG, "Register display driver to LVGL");
    lv_disp_drv_init(&disp_drv);
    disp_drv.hor_res = EXAMPLE_LCD_H_RES;
    disp_drv.ver_res = EXAMPLE_LCD_V_RES;
    disp_drv.flush_cb = example_lvgl_flush_cb;
    disp_drv.draw_buf = &disp_buf;
    disp_drv.user_data = panel_handle;
#if CONFIG_EXAMPLE_DOUBLE_FB
    disp_drv.full_refresh = true;
#endif
    lv_disp_t *disp = lv_disp_drv_register(&disp_drv);

    ESP_LOGI(LVGL_TAG, "Install LVGL tick timer");
    // Tick interface for LVGL (using esp_timer to generate 2ms periodic event)
    const esp_timer_create_args_t lvgl_tick_timer_args = {
        .callback = &example_increase_lvgl_tick,
        .name = "lvgl_tick"
    };

    /********************* LVGL *********************/
    ESP_LOGI(LVGL_TAG,"Register display indev to LVGL");
    lv_indev_drv_init ( &indev_drv );
    indev_drv.type = LV_INDEV_TYPE_POINTER;
    indev_drv.disp = disp;
    indev_drv.read_cb = example_touchpad_read;
    indev_drv.user_data = tp;
    lv_indev_drv_register( &indev_drv );

    ESP_ERROR_CHECK(esp_timer_create(&lvgl_tick_timer_args, &lvgl_tick_timer));
    ESP_ERROR_CHECK(esp_timer_start_periodic(lvgl_tick_timer, EXAMPLE_LVGL_TICK_PERIOD_MS * 1000));

}
