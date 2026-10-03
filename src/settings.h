#pragma once
#include <Arduino.h>

struct Settings {
  uint8_t brightness = 80;     // 0-255
  bool displayOn = true;
  float orbitDeg = 6.0f;       // camera swing each way (degrees)
  float orbitPeriodS = 18.0f;  // one full left-right-left swing
  float pauseDim = 0.45f;      // brightness while paused (0-1)
  float pauseBorder = 3.0f;    // black border when paused (LEDs)
  uint16_t fadeMs = 800;       // crossfade between songs
  uint16_t clockAfterPauseS = 60;  // paused this long -> show the clock instead
};

extern Settings settings;
void settingsLoad();
void settingsSave();