#pragma once
#include <stdint.h>

// Idle screen: a cute yellow dome face (think Las Vegas Sphere emoji) with wandering
// eyes, blinking, and the time underneath. Weather sits in the two top corners.
//
// This file pair has no Arduino dependencies, so it also builds on a computer:
// see tools/preview_clock.sh, which renders the face to a PNG/GIF for design work.

enum class WeatherKind : uint8_t { Clear, PartlyCloudy, Cloudy, Fog, Rain, Snow, Storm };

struct ClockInfo {
  bool timeKnown = false;
  int hour = 0, minute = 0;  // local time, 24-hour
  bool weatherKnown = false;
  WeatherKind weather = WeatherKind::Clear;
  bool isDay = true;
  int tempC = 0;
};

// Draws one frame of the idle screen into rgb: 64x64 pixels, RGB888, upright (before
// panel rotation). Call it every frame from the render loop with a millisecond clock
// (millis() is fine); the face animates itself from the time between calls.
void clockDraw(uint8_t *rgb, const ClockInfo &info, uint32_t ms);
