#include "hardware/display.h"

#include "hardware/display_font.h"
#include "hardware/st7701_waveshare_init.hpp"
#include "hardware/touch.h"

#include <Wire.h>
#include <esp_lcd_panel_ops.h>
#include <esp_lcd_panel_rgb.h>
#include <esp_heap_caps.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

LGFX tft;
namespace {
esp_lcd_panel_handle_t s_rgb_panel = nullptr;
uint16_t* s_framebuffer = nullptr;
SemaphoreHandle_t s_frame_done = nullptr;
bool onFrameDone(esp_lcd_panel_handle_t, esp_lcd_rgb_panel_event_data_t*, void*) {
  BaseType_t woken = pdFALSE;
  if (s_frame_done) xSemaphoreGiveFromISR(s_frame_done, &woken);
  return woken == pdTRUE;
}
}

uint16_t* displayFrameBuffer() { return s_framebuffer; }

void displayFlush() {
  if (!s_rgb_panel || !displayFrameBuffer()) return;

  // Do not start another whole-frame update until the RGB DMA engine has
  // completed the previous one. This prevents visible partial-frame swaps.
  if (s_frame_done) xSemaphoreTake(s_frame_done, 0);
  esp_lcd_panel_draw_bitmap(s_rgb_panel, 0, 0, 480, 480, s_framebuffer);
  if (s_frame_done) xSemaphoreTake(s_frame_done, pdMS_TO_TICKS(100));
}

void displayInit() {
  // The board's LCD reset and chip-select lines are on a TCA9554 I/O expander.
  Wire.begin(15, 7);
  Wire.setClock(400000);  // CST820 touch controller and TCA9554 use 400 kHz.
  Wire.beginTransmission(0x20);
  Wire.write(0x03);  // configuration: all expander pins as outputs
  Wire.write(0x00);
  Wire.endTransmission();
  st7701::init();

  // This is the same native RGB scanout configuration used by LandyGauge.
  // LovyanGFX draws into its framebuffer; ESP-IDF alone owns the LCD timing.
  esp_lcd_rgb_panel_config_t rgb = {};
  s_frame_done = xSemaphoreCreateBinary();
  rgb.clk_src = LCD_CLK_SRC_PLL160M;
  // 16 MHz is an exact divisor of the ESP32-S3 RGB clock in Arduino's
  // ESP-IDF 4.4 framework.  18 MHz produces horizontal timing drift here.
  rgb.timings.pclk_hz = 16000000;
  rgb.timings.h_res = 480;
  rgb.timings.v_res = 480;
  rgb.timings.hsync_pulse_width = 8;
  rgb.timings.hsync_back_porch = 10;
  rgb.timings.hsync_front_porch = 50;
  rgb.timings.vsync_pulse_width = 3;
  rgb.timings.vsync_back_porch = 8;
  rgb.timings.vsync_front_porch = 8;
  rgb.timings.flags.pclk_active_neg = false;
  rgb.data_width = 16;
  rgb.psram_trans_align = 64;
  rgb.on_frame_trans_done = onFrameDone;
  rgb.hsync_gpio_num = 38; rgb.vsync_gpio_num = 39;
  rgb.de_gpio_num = 40; rgb.pclk_gpio_num = 41;
  rgb.disp_gpio_num = -1;
  const int pins[] = {5,45,48,47,21,14,13,12,11,10,9,46,3,8,18,17};
  for (int i = 0; i < 16; ++i) rgb.data_gpio_nums[i] = static_cast<gpio_num_t>(pins[i]);
  rgb.flags.fb_in_psram = true;
  esp_lcd_new_rgb_panel(&rgb, &s_rgb_panel);
  esp_lcd_panel_reset(s_rgb_panel);
  esp_lcd_panel_init(s_rgb_panel);
  s_framebuffer = static_cast<uint16_t*>(heap_caps_malloc(480 * 480 * sizeof(uint16_t), MALLOC_CAP_SPIRAM));
  memset(s_framebuffer, 0, 480 * 480 * sizeof(uint16_t));

  ledcSetup(1, 20000, 10);
  ledcAttachPin(6, 1);
  ledcWrite(1, 900);  // LCD backlight, about 88%
  tft.init();
  tft.setRotation(0);
  tft.setBrightness(255);
  tft.setTextWrap(false);
  displayFontInit();
  touchInit();
}
