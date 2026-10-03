#include "clockface.h"
#include <stdio.h>
#include <string.h>

static uint16_t rgb(uint8_t r, uint8_t g, uint8_t b) {
  return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
}

// Draw text horizontally centred on the 64-pixel-wide canvas
static void centred(Adafruit_GFX &g, const char *s, int y, uint8_t size, uint16_t colour) {
  int w = strlen(s) * 6 * size - size;  // 5x7 font: 6 px per letter, minus the trailing gap
  g.setTextSize(size);
  g.setTextColor(colour);
  g.setCursor((g.width() - w) / 2, y);
  g.print(s);
}

void clockDraw(Adafruit_GFX &g, const struct tm *now) {
  static const char *DAYS[] = {"SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT"};
  static const char *MONTHS[] = {"JAN", "FEB", "MAR", "APR", "MAY", "JUN",
                                 "JUL", "AUG", "SEP", "OCT", "NOV", "DEC"};
  g.fillScreen(0);

  char buf[16];
  if (now) {
    snprintf(buf, sizeof(buf), "%02d:%02d", now->tm_hour, now->tm_min);
  } else {
    strcpy(buf, "--:--");
  }
  centred(g, buf, 22, 2, rgb(63, 216, 194));

  if (now) {
    snprintf(buf, sizeof(buf), "%s %d %s", DAYS[now->tm_wday], now->tm_mday, MONTHS[now->tm_mon]);
    centred(g, buf, 42, 1, rgb(120, 110, 150));
  }
}
