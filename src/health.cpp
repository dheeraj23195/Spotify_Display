#include <Arduino.h>
#include <Preferences.h>
#include "health.h"

static const uint32_t RESTART_AFTER_MS = 5UL * 60 * 1000;  // no answer for this long -> restart
static const uint8_t MAX_RESTARTS = 3;  // in a row, with no good answer in between

static Preferences prefs;
static volatile uint32_t lastOk = 0;
static uint8_t restartsInARow = 0;

void healthBegin() {
  prefs.begin("health", false);
  // Unplugging the display is a fresh start: forget earlier recovery restarts
  if (esp_reset_reason() == ESP_RST_POWERON) prefs.putUChar("rst", 0);
  restartsInARow = prefs.getUChar("rst", 0);
  if (restartsInARow) Serial.printf("Recovery restart %u of %u\n", restartsInARow, MAX_RESTARTS);
  lastOk = millis();
}

void healthPollOk() {
  lastOk = millis();
  if (restartsInARow) {
    restartsInARow = 0;
    prefs.putUChar("rst", 0);
  }
}

uint32_t healthSinceOkMs() {
  return millis() - lastOk;
}

void healthCheck(bool busy) {
  if (busy) {  // an update is running: start counting again once it is over
    lastOk = millis();
    return;
  }
  if (healthSinceOkMs() < RESTART_AFTER_MS || restartsInARow >= MAX_RESTARTS) return;
  prefs.putUChar("rst", ++restartsInARow);
  Serial.println("No Spotify answer for 5 min, restarting");
  delay(100);
  ESP.restart();
}
