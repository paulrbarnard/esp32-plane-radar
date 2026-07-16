#include "hardware/display.h"

#include "hardware/display_font.h"

#include <Wire.h>

LGFX tft;

void displayInit() {
  // The board's LCD reset and chip-select lines are on a TCA9554 I/O expander.
  // This is the reset/enable sequence from Waveshare's official 2.1" demo.
  Wire.begin(15, 7);
  Wire.beginTransmission(0x20);
  Wire.write(0x03);  // configuration: all expander pins as outputs
  Wire.write(0x00);
  Wire.endTransmission();
  Wire.beginTransmission(0x20);
  Wire.write(0x01);  // output: CS high, reset high
  Wire.write(0x07);
  Wire.endTransmission();
  delay(10);
  Wire.beginTransmission(0x20);
  Wire.write(0x01);  // reset low, CS low
  Wire.write(0x00);
  Wire.endTransmission();
  delay(10);
  Wire.beginTransmission(0x20);
  Wire.write(0x01);  // reset high, CS remains low for ST7701 setup
  Wire.write(0x01);
  Wire.endTransmission();
  delay(50);

  ledcSetup(1, 20000, 10);
  ledcAttachPin(6, 1);
  ledcWrite(1, 900);  // LCD backlight, about 88%
  tft.init();
  tft.setRotation(0);
  tft.setBrightness(255);
  tft.setTextWrap(false);
  displayFontInit();
}
