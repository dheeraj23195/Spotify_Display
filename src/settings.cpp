#include "settings.h"
#include <Preferences.h>

Settings settings;
static Preferences prefs;

void settingsLoad() {
  prefs.begin("settings", false);
  settings.brightness   = prefs.getUChar("bright", settings.brightness);
  settings.displayOn    = prefs.getBool("on", settings.displayOn);
  settings.orbitDeg     = prefs.getFloat("orbitDeg", settings.orbitDeg);
  settings.orbitPeriodS = prefs.getFloat("orbitPer", settings.orbitPeriodS);
  settings.pauseDim     = prefs.getFloat("pauseDim", settings.pauseDim);
  settings.pauseBorder  = prefs.getFloat("pauseBrd", settings.pauseBorder);
  settings.fadeMs       = prefs.getUShort("fadeMs", settings.fadeMs);
  settings.clockAfterPauseS = prefs.getUShort("clkPause", settings.clockAfterPauseS);
}

void settingsSave() {
  prefs.putUChar("bright", settings.brightness);
  prefs.putBool("on", settings.displayOn);
  prefs.putFloat("orbitDeg", settings.orbitDeg);
  prefs.putFloat("orbitPer", settings.orbitPeriodS);
  prefs.putFloat("pauseDim", settings.pauseDim);
  prefs.putFloat("pauseBrd", settings.pauseBorder);
  prefs.putUShort("fadeMs", settings.fadeMs);
  prefs.putUShort("clkPause", settings.clockAfterPauseS);
}