#pragma once
#include "clockface.h"

struct WeatherNow {
  int tempC;
  WeatherKind kind;
};

// Network task: call every loop. Fetches the current weather from Open-Meteo (free, no
// API key) every 15 minutes, once the clock is set. Needs WEATHER_LAT and WEATHER_LON in
// secrets.h; without them the weather is simply left out.
void weatherTick();

// Render loop: the latest reading, or false if there is none yet or it is over an hour old.
bool weatherGet(WeatherNow &out);
