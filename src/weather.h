#pragma once
struct WeatherNow {
  int tempC;
};

// Network task: call every loop. Fetches the current temperature from Open-Meteo (free, no
// API key) every 15 minutes, once the clock is set. Needs WEATHER_LAT and WEATHER_LON in
// secrets.h; without them the temperature is simply left out.
void weatherTick();

// Render loop: true between sunset and sunrise at the configured place. Uses today's real
// sunrise/sunset once fetched, and 18:00-06:00 until then (or if weather is not configured).
bool weatherNight(int hour, int minute);

// Render loop: the latest reading, or false if there is none yet or it is over an hour old.
bool weatherGet(WeatherNow &out);
