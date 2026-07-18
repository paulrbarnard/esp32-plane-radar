#pragma once

#define LGFX_USE_V1
#include <LovyanGFX.hpp>
#include <lgfx/v1/panel/Panel_FlexibleFrameBuffer.hpp>

#include "config.h"

// Owned by display.cpp and scanned out by ESP-IDF's native RGB panel driver.
uint16_t* displayFrameBuffer();

class WaveshareFrameBufferPanel : public lgfx::Panel_FlexibleFrameBuffer {
 protected:
  uint32_t _read_pixel_inner(uint_fast16_t x, uint_fast16_t y) override {
    return displayFrameBuffer()[y * config::kDisplayWidth + x];
  }
  void _draw_pixel_inner(uint_fast16_t x, uint_fast16_t y, uint32_t color) override {
    displayFrameBuffer()[y * config::kDisplayWidth + x] = static_cast<uint16_t>(color);
  }
  void _fill_rect_inner(uint_fast16_t x, uint_fast16_t y, uint_fast16_t w,
                        uint_fast16_t h, uint32_t color) override {
    uint16_t* pixels = displayFrameBuffer();
    for (uint_fast16_t row = 0; row < h; ++row) {
      uint16_t* dst = pixels + (y + row) * config::kDisplayWidth + x;
      for (uint_fast16_t col = 0; col < w; ++col) dst[col] = static_cast<uint16_t>(color);
    }
  }
};

class LGFX : public lgfx::LGFX_Device {
  WaveshareFrameBufferPanel _panel;
 public:
  LGFX() {
    auto cfg = _panel.config();
    cfg.memory_width = config::kDisplayWidth;
    cfg.memory_height = config::kDisplayHeight;
    cfg.panel_width = config::kDisplayWidth;
    cfg.panel_height = config::kDisplayHeight;
    _panel.config(cfg);
    setPanel(&_panel);
  }
};
