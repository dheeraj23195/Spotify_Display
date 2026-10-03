#pragma once
#include <Adafruit_GFX.h>
#include <time.h>

// Draws the idle clock onto a 64x64 canvas (upright, before panel rotation).
// This is the only function main.cpp calls, so the whole look can be replaced
// by rewriting clockface.cpp.
//   now == nullptr means the time is not known yet (draw a placeholder).
// main.cpp redraws once a minute; a face that needs seconds or animation
// would also have to change that cadence in main.cpp (showClock).
void clockDraw(Adafruit_GFX &g, const struct tm *now);
