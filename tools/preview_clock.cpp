// Host-side preview of the idle clock face (see tools/preview_clock.sh).
// Renders raw 64x64 RGB frames to a file; the script turns them into PNG/GIF.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../src/clockface.h"

int main(int argc, char **argv) {
  ClockInfo info;
  info.timeKnown = true;
  info.hour = 12;
  info.minute = 45;
  float seconds = 8, minuteChangeAt = -1;
  const char *out = "frames.bin";
  for (int i = 1; i + 1 < argc; i += 2) {
    const char *k = argv[i], *v = argv[i + 1];
    if (!strcmp(k, "--hour")) info.hour = atoi(v);
    else if (!strcmp(k, "--min")) info.minute = atoi(v);
    else if (!strcmp(k, "--notime")) info.timeKnown = false;
    else if (!strcmp(k, "--weather")) { info.weatherKnown = true; info.weather = (WeatherKind)atoi(v); }
    else if (!strcmp(k, "--day")) info.isDay = atoi(v) != 0;
    else if (!strcmp(k, "--temp")) info.tempC = atoi(v);
    else if (!strcmp(k, "--seconds")) seconds = atof(v);
    else if (!strcmp(k, "--minchange")) minuteChangeAt = atof(v);
    else if (!strcmp(k, "--out")) out = v;
  }
  FILE *f = fopen(out, "wb");
  static uint8_t frame[64 * 64 * 3];
  int frames = (int)(seconds * 30);
  bool changed = false;
  for (int i = 0; i < frames; i++) {
    uint32_t ms = 100000 + i * 1000 / 30;  // start away from 0 to catch wrap bugs
    if (!changed && minuteChangeAt >= 0 && i / 30.0f >= minuteChangeAt) {
      changed = true;
      if (++info.minute == 60) { info.minute = 0; info.hour = (info.hour + 1) % 24; }
    }
    clockDraw(frame, info, ms);
    fwrite(frame, 1, sizeof(frame), f);
  }
  fclose(f);
  return 0;
}
