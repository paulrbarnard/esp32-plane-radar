#include "hardware/touch.h"
#include <Wire.h>

namespace {
bool s_down = false;
unsigned long s_release_started = 0;
volatile bool s_touch_irq = false;
void IRAM_ATTR onTouchInterrupt() { s_touch_irq = true; }
void exio(uint8_t value) { Wire.beginTransmission(0x20); Wire.write(0x01); Wire.write(value); Wire.endTransmission(); }
bool read(uint8_t reg, uint8_t* data, size_t n) {
  Wire.beginTransmission(0x15); Wire.write(reg);
  if (Wire.endTransmission(false) != 0) return false;
  return Wire.requestFrom(0x15, static_cast<uint8_t>(n)) == n && ([&](){ for(size_t i=0;i<n;++i) data[i]=Wire.read(); return true; })();
}
void writeReg(uint8_t reg, uint8_t value) {
  Wire.beginTransmission(0x15); Wire.write(reg); Wire.write(value); Wire.endTransmission();
}
}
void touchInit() {
  exio(0x05); delay(10); exio(0x07); delay(50);
  // CST820 INT is GPIO16. Latch its falling edge so short taps are not lost
  // while Wi-Fi or display code is running.
  pinMode(16, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(16), onTouchInterrupt, FALLING);
}
bool touchConsumeTap(int* x, int* y) {
  if (!s_touch_irq && !s_down) return false;
  s_touch_irq = false;
  // CST820's controller-specific wake/acknowledge sequence from Waveshare's
  // working driver. Without it the controller keeps reporting no points.
  Wire.beginTransmission(0x15); Wire.write(0x01); Wire.endTransmission();
  writeReg(0xFE, 0x01);
  uint8_t count = 0;
  if (!read(0x02, &count, 1)) return false;
  if ((count & 0x0f) == 0) {
    writeReg(0x02, 0x00);
    if (s_down) {
      if (s_release_started == 0) s_release_started = millis();
      // Require a brief, continuous release before accepting the next tap.
      if (millis() - s_release_started >= 60) s_down = false;
    }
    return false;
  }
  s_release_started = 0;
  uint8_t point[6];
  if (s_down || !read(0x03, point, sizeof(point))) return false;
  writeReg(0x03, 0xAB);
  writeReg(0x02, 0x00);
  s_down = true;
  *x = ((point[0] & 0x0f) << 8) | point[1];
  *y = ((point[2] & 0x0f) << 8) | point[3];
  return *x < 480 && *y < 480;
}
