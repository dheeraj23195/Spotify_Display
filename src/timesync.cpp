#include "timesync.h"
#include <Arduino.h>

// POSIX rule for UTC+5:30 (the sign is inverted in POSIX TZ strings), no DST
static const char *TZ_RULE = "IST-5:30";

// Any time before this means the clock still has its power-on default (1970)
static const time_t MIN_VALID = 1700000000;  // Nov 2023

void timeBegin() {
  configTzTime(TZ_RULE, "pool.ntp.org", "time.google.com", "time.cloudflare.com");
}

void timeTick() {
  static unsigned long lastTry = millis();
  if (timeIsSet() || millis() - lastTry < 30000) return;
  lastTry = millis();
  timeBegin();
}

bool timeIsSet() {
  return time(nullptr) > MIN_VALID;
}

bool timeNow(struct tm &out) {
  time_t t = time(nullptr);
  if (t <= MIN_VALID) return false;
  localtime_r(&t, &out);
  return true;
}
