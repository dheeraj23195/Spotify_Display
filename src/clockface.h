#pragma once
#include <stdint.h>

// Idle screen: a cute yellow dome face (think Las Vegas Sphere emoji) with wandering
// eyes, blinking, and the time underneath. The temperature sits above the AM/PM letters.
//
// This file pair has no Arduino dependencies, so it also builds on a computer:
// see tools/preview_clock.sh, which renders the face to a PNG/GIF for design work.

struct ClockInfo {
  bool timeKnown = false;
  int hour = 0, minute = 0;  // local time, 24-hour
  bool weatherKnown = false;  // false = no temperature to show
  int tempC = 0;
};

// Draws one frame of the idle screen into rgb: 64x64 pixels, RGB888, upright (before
// panel rotation). Call it every frame from the render loop with a millisecond clock
// (millis() is fine); the face animates itself from the time between calls.
void clockDraw(uint8_t *rgb, const ClockInfo &info, uint32_t ms);

// For previews and tests: hold one expression (0 neutral, 1 happy, 2 surprised, 3 smug,
// 4 disgust, 5 scared, 6 asleep, 7 heart eyes, 8 wink, 9 curious). -1 = random, the default.
void clockForceMood(int mood);
