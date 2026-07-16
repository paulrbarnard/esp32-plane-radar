#pragma once

#define LGFX_USE_V1
#include <LovyanGFX.hpp>
#include <lgfx/v1/platforms/esp32s3/Bus_RGB.hpp>
#include <lgfx/v1/platforms/esp32s3/Panel_RGB.hpp>

#include "config.h"

/** LovyanGFX device: Waveshare ESP32-S3-Touch-LCD-2.1 ST7701 RGB panel. */
class LGFX : public lgfx::LGFX_Device {
  lgfx::Bus_RGB _bus;
  lgfx::Panel_ST7701 _panel;

public:
  LGFX() {
    {
      auto cfg = _panel.config();
      cfg.memory_width = config::kDisplayWidth;
      cfg.memory_height = config::kDisplayHeight;
      cfg.panel_width = config::kDisplayWidth;
      cfg.panel_height = config::kDisplayHeight;
      _panel.config(cfg);
    }
    {
      auto cfg = _panel.config_detail();
      cfg.pin_cs = -1;  // CS is controlled by the TCA9554 expander during init.
      cfg.pin_sclk = GPIO_NUM_2;
      cfg.pin_mosi = GPIO_NUM_1;
      _panel.config_detail(cfg);
    }
    {
      auto cfg = _bus.config();
      cfg.panel = &_panel;
      cfg.pin_d0 = GPIO_NUM_5; cfg.pin_d1 = GPIO_NUM_45;
      cfg.pin_d2 = GPIO_NUM_48; cfg.pin_d3 = GPIO_NUM_47;
      cfg.pin_d4 = GPIO_NUM_21; cfg.pin_d5 = GPIO_NUM_14;
      cfg.pin_d6 = GPIO_NUM_13; cfg.pin_d7 = GPIO_NUM_12;
      cfg.pin_d8 = GPIO_NUM_11; cfg.pin_d9 = GPIO_NUM_10;
      cfg.pin_d10 = GPIO_NUM_9; cfg.pin_d11 = GPIO_NUM_46;
      cfg.pin_d12 = GPIO_NUM_3; cfg.pin_d13 = GPIO_NUM_8;
      cfg.pin_d14 = GPIO_NUM_18; cfg.pin_d15 = GPIO_NUM_17;
      cfg.pin_hsync = GPIO_NUM_38; cfg.pin_vsync = GPIO_NUM_39;
      cfg.pin_henable = GPIO_NUM_40; cfg.pin_pclk = GPIO_NUM_41;
      cfg.freq_write = 16000000;
      cfg.hsync_pulse_width = 8; cfg.hsync_back_porch = 10; cfg.hsync_front_porch = 50;
      cfg.vsync_pulse_width = 3; cfg.vsync_back_porch = 8; cfg.vsync_front_porch = 8;
      cfg.pclk_active_neg = false; cfg.de_idle_high = true;
      _bus.config(cfg);
      _panel.setBus(&_bus);
    }
    setPanel(&_panel);
  }
};
