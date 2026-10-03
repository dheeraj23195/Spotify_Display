#include "clockface.h"
#include <math.h>
#include <string.h>
#include <initializer_list>

// =====================================================================
//  Look: tweak the numbers here (all in panel pixels, 64x64)
// =====================================================================

static const int W = 64, H = 64;

// Head: a dome (circle cut flat at the bottom) like the Sphere on its base
static const float HEAD_CX = 32, HEAD_CY = 36, HEAD_R = 31, HEAD_BASE = 46;

// Eyes
static const float EYE_DX = 11.0f, EYE_Y = 26.0f;  // distance from centre line, height
static const float EYE_RX = 6.6f, EYE_RY = 7.4f, PUPIL_R = 3.6f;
static const float LID_AWAKE = 0.10f;  // how far the upper lid covers the eye normally (0-1)
static const float LID_SLEEPY = 0.50f; // between 23:00 and 06:00

// Colours (r, g, b)
static const float YELLOW_LIGHT[3] = {255, 224, 64}, YELLOW_DARK[3] = {250, 176, 10};
static const float EYE_WHITE[3] = {255, 242, 247}, INK[3] = {16, 9, 4}, BROW[3] = {10, 5, 2};
static const float TIME_COL[3] = {246, 238, 222}, AMPM_COL[3] = {205, 165, 85};
static const float TEMP_COL[3] = {190, 215, 240};

// =====================================================================
//  Drawing helpers
// =====================================================================

static uint8_t *fb;

static inline float clamp01(float v) { return v < 0 ? 0 : (v > 1 ? 1 : v); }
static inline float mixf(float a, float b, float t) { return a + (b - a) * t; }

// Blend a colour over a pixel; a = coverage 0-1
static inline void blend(int x, int y, const float c[3], float a) {
  if (a <= 0.0f || x < 0 || y < 0 || x >= W || y >= H) return;
  uint8_t *p = &fb[(y * W + x) * 3];
  for (int i = 0; i < 3; i++) p[i] = (uint8_t)(p[i] + (c[i] - p[i]) * a + 0.5f);
}

// Fill every pixel in the box where sd(x, y) < 0 (sd = signed distance, in pixels).
// The 1-pixel soft edge is what makes the shapes look smooth on a coarse panel.
template <typename F>
static void shape(float x0, float y0, float x1, float y1, F sd, const float c[3], float alpha = 1.0f) {
  for (int y = (int)floorf(y0); y <= (int)ceilf(y1); y++)
    for (int x = (int)floorf(x0); x <= (int)ceilf(x1); x++)
      blend(x, y, c, clamp01(0.5f - sd(x + 0.5f, y + 0.5f)) * alpha);
}

static inline float distSeg(float px, float py, float ax, float ay, float bx, float by) {
  float dx = bx - ax, dy = by - ay;
  float t = clamp01(((px - ax) * dx + (py - ay) * dy) / (dx * dx + dy * dy + 1e-6f));
  float qx = ax + dx * t - px, qy = ay + dy * t - py;
  return sqrtf(qx * qx + qy * qy);
}

static inline float distCircle(float px, float py, float cx, float cy, float r) {
  float dx = px - cx, dy = py - cy;
  return sqrtf(dx * dx + dy * dy) - r;
}

// Approximate distance to an axis-aligned ellipse (good enough at this size)
static inline float distEllipse(float px, float py, float cx, float cy, float rx, float ry) {
  float qx = (px - cx) / rx, qy = (py - cy) / ry;
  return (sqrtf(qx * qx + qy * qy) - 1.0f) * (rx < ry ? rx : ry);
}

static void capsule(float ax, float ay, float bx, float by, float r, const float c[3], float alpha = 1.0f) {
  float x0 = (ax < bx ? ax : bx) - r - 1, x1 = (ax > bx ? ax : bx) + r + 1;
  float y0 = (ay < by ? ay : by) - r - 1, y1 = (ay > by ? ay : by) + r + 1;
  shape(x0, y0, x1, y1,
        [=](float x, float y) { return distSeg(x, y, ax, ay, bx, by) - r; }, c, alpha);
}

static void disc(float cx, float cy, float r, const float c[3], float alpha = 1.0f) {
  shape(cx - r - 1, cy - r - 1, cx + r + 1, cy + r + 1,
        [=](float x, float y) { return distCircle(x, y, cx, cy, r); }, c, alpha);
}

// =====================================================================
//  Animation: where the eyes look, blinking, mood. Kept between frames.
// =====================================================================

struct Pose {
  float gx, gy;   // gaze, -1..1 (right / down are positive)
  float blink;    // 0 open .. 1 shut
  float lid;      // resting lid height (higher when sleepy)
  float happy;    // 0..1, widens the smile and lifts the brows
  float breathe;  // -1..1
};

static uint32_t rngState = 2463534242u;
static float rnd() {  // 0..1, cheap and repeatable
  rngState = rngState * 1664525u + 1013904223u;
  return (rngState >> 8) / 16777216.0f;
}

static Pose animate(const ClockInfo &info, uint32_t ms) {
  static bool started = false;
  static uint32_t lastMs, nextLook, nextBlink, blinkStart;
  static bool blinking = false;
  static float gx = 0, gy = 0, tx = 0, ty = 0, lid = LID_AWAKE, happy = 0;
  static int lastMinute = -1;
  if (!started) {
    started = true;
    lastMs = ms;
    nextLook = ms + 800;
    nextBlink = ms + 2500;
  }
  float dt = (ms - lastMs) * 0.001f;
  if (dt > 0.1f) dt = 0.1f;  // after a pause (cover art was showing) don't jump
  lastMs = ms;

  bool sleepy = info.timeKnown && (info.hour >= 23 || info.hour < 6);

  // New minute: glance down at the time and smile
  if (info.timeKnown) {
    if (lastMinute >= 0 && info.minute != lastMinute) {
      tx = 0;
      ty = 1.0f;
      nextLook = ms + 1800;
      happy = 1.0f;
    }
    lastMinute = info.minute;
  }

  // Now and then, look somewhere else
  if ((int32_t)(ms - nextLook) >= 0) {
    float r = rnd();
    if (info.weatherKnown && r < 0.12f) {  // peek up at the weather in the corners
      tx = rnd() < 0.5f ? -1.0f : 1.0f;
      ty = -0.9f;
    } else if (r < 0.45f) {  // back to looking at you
      tx = (rnd() - 0.5f) * 0.3f;
      ty = (rnd() - 0.5f) * 0.3f;
    } else {
      tx = (rnd() - 0.5f) * 2.0f;
      ty = (rnd() - 0.5f) * 1.5f;
    }
    if (sleepy) { tx *= 0.5f; ty *= 0.5f; }
    nextLook = ms + 1200 + (uint32_t)(rnd() * 2600);
  }
  float k = 1.0f - expf(-dt / 0.06f);  // quick, slightly springy eye jumps
  gx += (tx - gx) * k;
  gy += (ty - gy) * k;
  happy = happy > dt * 0.6f ? happy - dt * 0.6f : 0.0f;

  // Blinking (sometimes twice in a row; slower when sleepy)
  if (!blinking && (int32_t)(ms - nextBlink) >= 0) {
    blinking = true;
    blinkStart = ms;
  }
  float blink = 0;
  if (blinking) {
    float e = (ms - blinkStart) * 0.001f, dur = sleepy ? 0.32f : 0.18f;
    if (e >= dur) {
      blinking = false;
      nextBlink = ms + (rnd() < 0.18f ? 220u : 2200u + (uint32_t)(rnd() * 4200));
    } else {
      float u = e / dur;
      blink = u < 0.45f ? u / 0.45f : (1.0f - u) / 0.55f;
    }
  }

  lid += ((sleepy ? LID_SLEEPY : LID_AWAKE) - lid) * (1.0f - expf(-dt / 1.0f));

  Pose p;
  p.gx = gx;
  p.gy = gy;
  p.blink = blink;
  p.lid = lid;
  p.happy = happy;
  p.breathe = sinf(ms * 0.001f * 1.5708f);  // one breath every 4 s
  return p;
}

// =====================================================================
//  The face
// =====================================================================

// Dome colour at a point: light up top-left, deeper amber toward the rim and the base
static void domeColor(float x, float y, float out[3]) {
  float hx = x - 22.0f, hy = y - 17.0f;
  float t = clamp01(sqrtf(hx * hx + hy * hy) / (HEAD_R * 1.5f));
  float shade = 0.74f + 0.26f * clamp01((HEAD_BASE - y) / 6.0f);
  float glow = 0.22f * expf(-(hx * hx + hy * hy) / 72.0f);
  for (int i = 0; i < 3; i++) {
    float c = mixf(YELLOW_LIGHT[i], YELLOW_DARK[i], t) * shade;
    out[i] = mixf(c, 255.0f, glow);
  }
}

static void drawHead(const Pose &p) {
  float R = HEAD_R * (1.0f + 0.008f * p.breathe);
  for (int y = 0; y < (int)HEAD_BASE; y++) {
    for (int x = 0; x < W; x++) {
      float px = x + 0.5f, py = y + 0.5f;
      float sd = distCircle(px, py, HEAD_CX, HEAD_CY, R);
      float cut = py - HEAD_BASE;
      if (cut > sd) sd = cut;
      float a = clamp01(0.5f - sd);
      if (a <= 0.0f) continue;
      float c[3];
      domeColor(px, py, c);
      blend(x, y, c, a);
    }
  }
}

static void drawEye(float cx, float cy, const Pose &p, float closure) {
  const float rx = EYE_RX, ry = EYE_RY;
  float lidY = cy - ry + closure * 2.0f * ry;  // everything above this line is hidden
  float pcx = cx + p.gx * (rx - PUPIL_R - 0.3f);
  float pcy = cy + p.gy * (ry - PUPIL_R - 0.3f) + 0.3f;

  for (int y = (int)floorf(cy - ry - 1); y <= (int)ceilf(cy + ry + 1); y++) {
    for (int x = (int)floorf(cx - rx - 1); x <= (int)ceilf(cx + rx + 1); x++) {
      float px = x + 0.5f, py = y + 0.5f;
      float vis = clamp01(py - lidY + 0.5f);
      float eye = clamp01(0.5f - distEllipse(px, py, cx, cy, rx, ry)) * vis;
      if (eye <= 0.0f) continue;
      blend(x, y, EYE_WHITE, eye);
      blend(x, y, INK, clamp01(0.5f - distCircle(px, py, pcx, pcy, PUPIL_R)) * eye);
      blend(x, y, EYE_WHITE, clamp01(0.5f - distCircle(px, py, pcx - 1.1f, pcy - 1.3f, 0.9f)) * eye * 0.9f);
    }
  }
  if (closure > 0.55f) {  // fully shut: a little curved line
    float a = clamp01((closure - 0.55f) / 0.3f);
    float ly = lidY < cy + ry * 0.6f ? lidY : cy + ry * 0.6f;
    capsule(cx - 4.5f, ly, cx + 4.5f, ly, 0.9f, INK, a);
  }
}

static void drawFace(const Pose &p) {
  float s = 1.0f + 0.008f * p.breathe;
  float turnX = p.gx * 1.6f, turnY = p.gy * 1.0f;  // the whole face turns a little toward where it looks
  float closure = clamp01(p.lid + p.blink * (1.0f - p.lid));

  float browLift = -p.gy * 0.8f + p.happy * 1.2f - (p.lid - LID_AWAKE) * 4.0f;
  float eyeTop = EYE_Y - EYE_RY;

  for (int side = -1; side <= 1; side += 2) {
    float cx = HEAD_CX + side * EYE_DX * s + turnX;
    float cy = EYE_Y + turnY;
    drawEye(cx, cy, p, closure);

    // Brow: inner end lower for a smug look; the right one sits a touch higher
    float by = eyeTop + turnY - 3.2f - browLift - (side > 0 ? 1.0f : 0.0f);
    float outerX = cx + side * 7.4f, innerX = cx - side * 6.4f;
    capsule(outerX, by - 0.2f, innerX, by + 1.3f, 1.35f, BROW);
  }

  // Small smile, a bit off-centre
  float mx = 33.5f + turnX, my = 39.5f + turnY;
  float hw = 5.5f, amp = 2.2f + 1.5f * p.happy;
  shape(mx - hw - 2, my - 2, mx + hw + 2, my + amp + 3,
        [=](float x, float y) {
          float u = (x - mx) / hw;
          float d;
          if (u >= -1.0f && u <= 1.0f) {
            float yc = my + amp * (1.0f - u * u);
            float slope = -2.0f * amp * u / hw;
            d = fabsf(y - yc) / sqrtf(1.0f + slope * slope);
          } else {
            d = distCircle(x, y, mx + (u > 0 ? hw : -hw), my, 0.0f);
          }
          return d - 1.0f;
        },
        INK);
}

// =====================================================================
//  Time (bottom strip) and weather (top corners)
// =====================================================================

// 5x7 glyphs, one byte per column, bit 0 = top row
static const uint8_t DIGITS[11][5] = {
    {0x3E, 0x51, 0x49, 0x45, 0x3E}, {0x00, 0x42, 0x7F, 0x40, 0x00}, {0x42, 0x61, 0x51, 0x49, 0x46},
    {0x21, 0x41, 0x45, 0x4B, 0x31}, {0x18, 0x14, 0x12, 0x7F, 0x10}, {0x27, 0x45, 0x45, 0x45, 0x39},
    {0x3C, 0x4A, 0x49, 0x49, 0x30}, {0x01, 0x71, 0x09, 0x05, 0x03}, {0x36, 0x49, 0x49, 0x49, 0x36},
    {0x06, 0x49, 0x49, 0x29, 0x1E}, {0x08, 0x08, 0x08, 0x08, 0x08}};  // index 10 = dash

// 3x5 letters for AM / PM, one row per entry, bit 2 = left column
static const uint8_t MICRO_A[5] = {2, 5, 7, 5, 5}, MICRO_P[5] = {6, 5, 6, 4, 4}, MICRO_M[5] = {5, 7, 7, 5, 5};

static void glyph(int x, int y, int idx, int scale, const float c[3]) {
  for (int col = 0; col < 5; col++)
    for (int row = 0; row < 7; row++)
      if (DIGITS[idx][col] & (1 << row))
        for (int dy = 0; dy < scale; dy++)
          for (int dx = 0; dx < scale; dx++) blend(x + col * scale + dx, y + row * scale + dy, c, 1.0f);
}

static void micro(int x, int y, const uint8_t rows[5], const float c[3]) {
  for (int row = 0; row < 5; row++)
    for (int col = 0; col < 3; col++)
      if (rows[row] & (4 >> col)) blend(x + col, y + row, c, 1.0f);
}

static void drawTime(const ClockInfo &info) {
  int d[4], hourDigits = 2;
  if (info.timeKnown) {
    int h = info.hour % 12;
    if (h == 0) h = 12;
    hourDigits = h >= 10 ? 2 : 1;
    int n = 0;
    if (h >= 10) d[n++] = h / 10;
    d[n++] = h % 10;
    d[n++] = info.minute / 10;
    d[n++] = info.minute % 10;
  } else {
    d[0] = d[1] = d[2] = d[3] = 10;  // "--:--"
  }

  // digit 10 wide, 2 gap; colon 2 wide; then "AM"/"PM" 7 wide
  int width = hourDigits * 12 - 2 + 2 + 2 + 2 + 22;
  if (info.timeKnown) width += 3 + 7;
  int x = (W - width) / 2, y = 49;

  int i = 0;
  for (int k = 0; k < hourDigits; k++, i++, x += 12) glyph(x, y, d[i], 2, TIME_COL);
  x += 0;  // colon sits in the gap left by the last hour digit
  for (int dy : {3, 9}) {
    for (int py = 0; py < 2; py++)
      for (int px = 0; px < 2; px++) blend(x + px, y + dy + py, TIME_COL, 1.0f);
  }
  x += 4;
  for (int k = 0; k < 2; k++, i++, x += 12) glyph(x, y, d[i], 2, TIME_COL);

  if (info.timeKnown) {
    x += 1;
    micro(x, y + 9, info.hour < 12 ? MICRO_A : MICRO_P, AMPM_COL);
    micro(x + 4, y + 9, MICRO_M, AMPM_COL);
  }
}

// ---- Weather icons (11x11 box in the top-left corner) ----
static const float SUN_COL[3] = {255, 206, 40}, MOON_COL[3] = {255, 244, 205};
static const float CLOUD_COL[3] = {214, 224, 242}, CLOUD_GREY[3] = {165, 176, 198}, CLOUD_DARK[3] = {120, 130, 152};
static const float RAIN_COL[3] = {90, 165, 255}, SNOW_COL[3] = {245, 248, 255};
static const float BOLT_COL[3] = {255, 226, 60}, FOG_COL[3] = {175, 186, 200};

// Cloud inside the 11x11 icon box whose top-left is (ox, oy), scaled by sc
static void cloud(float ox, float oy, float sc, const float col[3]) {
  shape(ox, oy + 1.5f * sc, ox + 11 * sc, oy + 10 * sc,
        [=](float x, float y) {
          float lx = (x - ox) / sc, ly = (y - oy) / sc;
          float d = distCircle(lx, ly, 3.6f, 7.0f, 2.6f);
          float d2 = distCircle(lx, ly, 6.0f, 5.4f, 3.2f);
          float d3 = distCircle(lx, ly, 8.4f, 7.0f, 2.4f);
          float bx = fabsf(lx - 6.0f) - 2.4f, by = fabsf(ly - 8.3f) - 1.3f;
          float d4 = bx > by ? bx : by;
          if (d2 < d) d = d2;
          if (d3 < d) d = d3;
          if (d4 < d) d = d4;
          return d * sc;
        },
        col);
}

static void sun(float cx, float cy, float r, float rayR, float t) {
  disc(cx, cy, r, SUN_COL);
  for (int i = 0; i < 8; i++) {
    float a = i * 0.7854f + t * 0.314f;  // one slow turn every 20 s
    disc(cx + cosf(a) * rayR, cy + sinf(a) * rayR, 0.6f, SUN_COL, 0.9f);
  }
}

static void moon(float cx, float cy, float r) {
  shape(cx - r - 1, cy - r - 1, cx + r + 1, cy + r + 1,
        [=](float x, float y) {
          float d = distCircle(x, y, cx, cy, r);
          float cut = -distCircle(x, y, cx + r * 0.5f, cy - r * 0.35f, r * 0.82f);
          return d > cut ? d : cut;
        },
        MOON_COL);
}

static void drawWeatherIcon(const ClockInfo &info, uint32_t ms) {
  const float ox = 1.0f, oy = 1.0f;
  float t = (ms % 60000) * 0.001f;  // every motion below repeats within 60 s
  switch (info.weather) {
    case WeatherKind::Clear:
      if (info.isDay) sun(ox + 5.5f, oy + 5.5f, 2.7f, 4.6f, t);
      else moon(ox + 5.5f, oy + 5.5f, 4.0f);
      break;
    case WeatherKind::PartlyCloudy:
      if (info.isDay) sun(ox + 3.6f, oy + 3.6f, 2.0f, 3.6f, t);
      else moon(ox + 3.8f, oy + 3.8f, 3.0f);
      cloud(ox + 1.6f, oy + 2.4f, 0.85f, CLOUD_COL);
      break;
    case WeatherKind::Cloudy:
      cloud(ox, oy + 0.5f, 1.0f, CLOUD_COL);
      break;
    case WeatherKind::Fog:
      for (int i = 0; i < 3; i++) {
        float dx = sinf((t / 10.0f + i * 0.2f) * 6.2832f) * 0.8f;
        capsule(ox + 1.5f + dx, oy + 2.5f + i * 2.8f, ox + 9.5f + dx, oy + 2.5f + i * 2.8f, 0.7f, FOG_COL, 0.9f);
      }
      break;
    case WeatherKind::Rain:
      cloud(ox, oy - 0.5f, 0.9f, CLOUD_GREY);
      for (int i = 0; i < 3; i++) {
        float f = fmodf(t * 1.5f + i * 0.37f, 1.0f);
        float x = ox + 3.2f + i * 2.8f, y = oy + 6.6f + f * 3.6f;
        capsule(x, y, x - 0.4f, y + 1.3f, 0.5f, RAIN_COL, 1.0f - f * 0.6f);
      }
      break;
    case WeatherKind::Snow:
      cloud(ox, oy - 0.5f, 0.9f, CLOUD_GREY);
      for (int i = 0; i < 3; i++) {
        float f = fmodf(t * 0.5f + i * 0.33f, 1.0f);
        float x = ox + 3.2f + i * 2.8f + sinf((t / 6.0f + i * 0.3f) * 6.2832f) * 0.6f, y = oy + 6.8f + f * 3.4f;
        disc(x, y, 0.65f, SNOW_COL, 1.0f - f * 0.5f);
      }
      break;
    case WeatherKind::Storm:
      cloud(ox, oy - 0.5f, 0.9f, CLOUD_DARK);
      capsule(ox + 6.8f, oy + 6.9f, ox + 5.3f, oy + 8.9f, 0.65f, BOLT_COL);
      capsule(ox + 5.3f, oy + 8.9f, ox + 6.9f, oy + 8.9f, 0.65f, BOLT_COL);
      capsule(ox + 6.9f, oy + 8.9f, ox + 5.2f, oy + 10.8f, 0.65f, BOLT_COL);
      break;
  }
}

// ---- Temperature (top-right corner), e.g. "31" and a degree ring ----
static void drawTemp(const ClockInfo &info) {
  int v = info.tempC;
  int digits[3], n = 0;
  if (v < 0) digits[n++] = 10;
  if (v < 0) v = -v;
  if (v > 99) v = 99;
  if (v >= 10) digits[n++] = v / 10;
  digits[n++] = v % 10;
  int width = n * 6 - 1 + 1 + 3;
  int x = W - 1 - width, y = 2;
  for (int i = 0; i < n; i++, x += 6) glyph(x, y, digits[i], 1, TEMP_COL);
  x += 0;
  static const uint8_t ring[3][3] = {{0, 1, 0}, {1, 0, 1}, {0, 1, 0}};
  for (int r = 0; r < 3; r++)
    for (int c = 0; c < 3; c++)
      if (ring[r][c]) blend(x + c, y + r, TEMP_COL, 1.0f);
}

void clockDraw(uint8_t *rgb, const ClockInfo &info, uint32_t ms) {
  fb = rgb;
  memset(rgb, 0, W * H * 3);
  Pose pose = animate(info, ms);
  drawHead(pose);
  drawFace(pose);
  drawTime(info);
  if (info.weatherKnown) {
    drawWeatherIcon(info, ms);
    drawTemp(info);
  }
}
