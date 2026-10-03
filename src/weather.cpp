#include "weather.h"
#include <Arduino.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
#include "secrets.h"
#include "tls.h"
#include "timesync.h"

static const uint32_t REFRESH_MS = 15UL * 60 * 1000;  // how often to ask
static const uint32_t RETRY_MS = 60UL * 1000;          // after a failed attempt
static const uint32_t STALE_MS = 3UL * 60 * 60 * 1000;  // older than this is not shown

static portMUX_TYPE lock = portMUX_INITIALIZER_UNLOCKED;  // network task writes, render loop reads
static WeatherNow latest;
static bool haveReading = false;
static uint32_t readingAt = 0;
static int lastCode = 0;
static uint32_t tries = 0, failures = 0;
static int sunriseMin = -1, sunsetMin = -1;  // minutes after midnight, local; -1 = not known yet

#if defined(WEATHER_LAT) && defined(WEATHER_LON)
static bool fetch() {
  WiFiClientSecure client;
  tlsConfigure(client);
  HTTPClient http;
  http.useHTTP10(true);
  http.setTimeout(5000);
  if (!http.begin(client, "https://api.open-meteo.com/v1/forecast?latitude=" WEATHER_LAT
                          "&longitude=" WEATHER_LON "&current=temperature_2m&daily=sunrise,sunset&timezone=auto&forecast_days=1")) {
    lastCode = -100;
    return false;
  }
  int code = http.GET();
  if (code != 200) {
    Serial.printf("Weather failed: HTTP %d\n", code);
    lastCode = code == 0 ? -1 : code;
    http.end();
    return false;
  }
  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, http.getStream());
  http.end();
  JsonObject cur = doc["current"];
  if (err || cur["temperature_2m"].isNull()) {
    Serial.println("Weather reply not understood");
    lastCode = -200;
    return false;
  }
  // "2026-10-03T18:04" -> minutes after midnight
  auto minutesOf = [](const char *iso) {
    return (iso && strlen(iso) >= 16) ? atoi(iso + 11) * 60 + atoi(iso + 14) : -1;
  };
  int rise = minutesOf(doc["daily"]["sunrise"][0] | (const char *)nullptr);
  int set = minutesOf(doc["daily"]["sunset"][0] | (const char *)nullptr);
  WeatherNow w;
  w.tempC = (int)lroundf(cur["temperature_2m"].as<float>());
  portENTER_CRITICAL(&lock);
  latest = w;
  if (rise >= 0 && set >= 0) {
    sunriseMin = rise;
    sunsetMin = set;
  }
  haveReading = true;
  readingAt = millis();
  lastCode = 200;
  portEXIT_CRITICAL(&lock);
  return true;
}
#endif

void weatherTick() {
#if defined(WEATHER_LAT) && defined(WEATHER_LON)
  static uint32_t nextTry = 0;
  if (!timeIsSet() || (int32_t)(millis() - nextTry) < 0) return;  // certificates need the date
  bool ok = fetch();
  tries++;
  if (!ok) failures++;
  nextTry = millis() + (ok ? REFRESH_MS : RETRY_MS);
#else
  static bool said = false;
  if (!said) {
    said = true;
    Serial.println("Weather off: add WEATHER_LAT and WEATHER_LON to secrets.h");
  }
#endif
}

bool weatherGet(WeatherNow &out) {
  bool ok;
  portENTER_CRITICAL(&lock);
  ok = haveReading && millis() - readingAt < STALE_MS;
  if (ok) out = latest;
  portEXIT_CRITICAL(&lock);
  return ok;
}

bool weatherNight(int hour, int minute) {
  int now = hour * 60 + minute, rise = 6 * 60, set = 18 * 60;
  portENTER_CRITICAL(&lock);
  if (sunriseMin >= 0 && sunsetMin >= 0) {
    rise = sunriseMin;
    set = sunsetMin;
  }
  portEXIT_CRITICAL(&lock);
  return now >= set || now < rise;
}

WeatherStatus weatherStatus() {
  WeatherStatus s;
  portENTER_CRITICAL(&lock);
  s.everOk = haveReading;
  s.okAgoS = haveReading ? (millis() - readingAt) / 1000 : 0;
  s.lastCode = lastCode;
  s.tries = tries;
  s.failures = failures;
  portEXIT_CRITICAL(&lock);
#if !(defined(WEATHER_LAT) && defined(WEATHER_LON))
  s.lastCode = -300;
#endif
  return s;
}
