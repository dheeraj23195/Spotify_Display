#include "weather.h"
#include <Arduino.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
#include "secrets.h"
#include "tls.h"
#include "timesync.h"

static const uint32_t REFRESH_MS = 15UL * 60 * 1000;  // how often to ask
static const uint32_t RETRY_MS = 3UL * 60 * 1000;     // after a failed attempt
static const uint32_t STALE_MS = 60UL * 60 * 1000;    // older than this is not shown

static portMUX_TYPE lock = portMUX_INITIALIZER_UNLOCKED;  // network task writes, render loop reads
static WeatherNow latest;
static bool haveReading = false;
static uint32_t readingAt = 0;

#if defined(WEATHER_LAT) && defined(WEATHER_LON)
static bool fetch() {
  WiFiClientSecure client;
  tlsConfigure(client);
  HTTPClient http;
  http.useHTTP10(true);
  http.setTimeout(5000);
  if (!http.begin(client, "https://api.open-meteo.com/v1/forecast?latitude=" WEATHER_LAT
                          "&longitude=" WEATHER_LON "&current=temperature_2m")) {
    return false;
  }
  int code = http.GET();
  if (code != 200) {
    Serial.printf("Weather failed: HTTP %d\n", code);
    http.end();
    return false;
  }
  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, http.getStream());
  http.end();
  JsonObject cur = doc["current"];
  if (err || cur["temperature_2m"].isNull()) {
    Serial.println("Weather reply not understood");
    return false;
  }
  WeatherNow w;
  w.tempC = (int)lroundf(cur["temperature_2m"].as<float>());
  portENTER_CRITICAL(&lock);
  latest = w;
  haveReading = true;
  readingAt = millis();
  portEXIT_CRITICAL(&lock);
  return true;
}
#endif

void weatherTick() {
#if defined(WEATHER_LAT) && defined(WEATHER_LON)
  static uint32_t nextTry = 0;
  if (!timeIsSet() || (int32_t)(millis() - nextTry) < 0) return;  // certificates need the date
  nextTry = millis() + (fetch() ? REFRESH_MS : RETRY_MS);
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
