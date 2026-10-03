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

// Bottom strip, top to bottom: face ends at row 45 | 3 blank | temperature rows 49-53 |
// 3 blank | AM/PM rows 57-61. The time digits (rows 49-62) start level with the temperature.
static const int TIME_Y = 49, TEMP_Y = 49, AMPM_Y = 57;

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
//  Moods: the face drifts between expressions, like Orbi on the Sphere
// =====================================================================

enum Mood { NEUTRAL, HAPPY, SURPRISED, SMUG, DISGUST, SCARED, SLEEP, HEARTS, WINK, CURIOUS, MOOD_COUNT };

// Every expression is a set of numbers; the face eases from one set to the next.
enum Param {
  P_LID,      // resting upper-lid height, 0 open .. 1 shut
  P_WIDEN,    // eye size (1 = normal)
  P_PUPIL,    // pupil size (1 = normal)
  P_BROW_L, P_BROW_R,  // brow height in pixels (+ up)
  P_TILT_L, P_TILT_R,  // inner end of the brow, pixels lower than the outer end (- = worried)
  P_SMILE,    // curve of the mouth in pixels (+ smile, - frown)
  P_MOUTH_W,  // mouth width (1 = normal)
  P_OPEN,     // 0 line .. 1 round open mouth
  P_MOUTH_X,  // mouth shifted sideways, pixels
  P_SKEW,     // one end of the mouth higher than the other (smirk)
  P_WAVE,     // wobbly mouth
  P_WINK,     // right eye shut
  P_HEARTS,   // pupils turn into hearts
  P_ZZZ,      // sleeping Z's
  P_SHAKE,    // trembling
  P_COUNT
};

enum Gaze { G_WANDER, G_UP, G_DOWN, G_CENTER, G_DART, G_SCAN, G_REST };

struct MoodDef {
  float v[P_COUNT];
  Gaze gaze;
  float minS, maxS;              // how long it lasts, seconds
  float weightDay, weightNight;  // how likely it is to be picked
};

static const MoodDef MOODS[MOOD_COUNT] = {
    //  lid    widen  pupil  browL  browR  tiltL  tiltR  smile  mouthW open   x      skew   wave  wink hearts zzz shake
    {{0.10f, 1.00f, 1.00f, 0.0f, 1.0f, 1.3f, 1.3f, 2.2f, 1.00f, 0.0f, 0.0f, 0.0f, 0.0f, 0, 0, 0, 0}, G_WANDER, 0, 0, 0, 0},            // neutral
    {{0.06f, 1.00f, 1.00f, 1.6f, 1.6f, 0.2f, 0.2f, 4.0f, 1.15f, 0.0f, 0.0f, 0.0f, 0.0f, 0, 0, 0, 0}, G_WANDER, 4, 6, 3, 1},            // happy
    {{0.00f, 1.20f, 0.65f, 2.3f, 2.3f, -0.6f, -0.6f, 0.5f, 0.70f, 1.0f, 0.0f, 0.0f, 0.0f, 0, 0, 0, 0}, G_CENTER, 2.5f, 4, 2, 1},       // surprised
    {{0.38f, 1.00f, 1.00f, 0.0f, 2.6f, 2.0f, -0.6f, 2.0f, 1.00f, 0.0f, 1.5f, -1.4f, 0.0f, 0, 0, 0, 0}, G_UP, 4, 7, 3, 1},             // smug
    {{0.34f, 1.00f, 0.90f, -0.5f, -0.5f, 2.8f, 2.8f, -1.9f, 0.90f, 0.0f, 0.0f, 0.0f, 0.9f, 0, 0, 0, 0}, G_DOWN, 3, 5, 1.5f, 0.5f},     // disgust
    {{0.00f, 1.16f, 0.55f, 2.2f, 2.2f, -2.2f, -2.2f, -0.4f, 0.90f, 0.0f, 0.0f, 0.0f, 1.2f, 0, 0, 0, 1}, G_DART, 3, 5, 1.5f, 0.5f},    // scared
    {{1.00f, 1.00f, 1.00f, -1.0f, -1.0f, 0.0f, 0.0f, 0.8f, 0.70f, 0.0f, 0.0f, 0.0f, 0.0f, 0, 0, 1, 0}, G_REST, 8, 15, 1, 8},          // asleep
    {{0.00f, 1.10f, 1.00f, 2.0f, 2.0f, 0.0f, 0.0f, 4.4f, 1.20f, 0.0f, 0.0f, 0.0f, 0.0f, 0, 1, 0, 0}, G_WANDER, 4, 6, 0.7f, 0.3f},     // heart eyes
    {{0.08f, 1.00f, 1.00f, 1.0f, 0.0f, 0.5f, 1.3f, 3.6f, 1.05f, 0.0f, 0.5f, 0.0f, 0.0f, 1, 0, 0, 0}, G_CENTER, 1.5f, 2.5f, 2, 1},     // wink
    {{0.00f, 1.12f, 1.00f, 2.0f, 2.0f, -0.4f, -0.4f, 1.6f, 0.80f, 0.0f, 0.0f, 0.0f, 0.0f, 0, 0, 0, 0}, G_SCAN, 5, 8, 3, 1},           // curious
};

static int forcedMood = -1;  // preview/testing only
void clockForceMood(int mood) { forcedMood = mood; }

// =====================================================================
//  Animation: where the eyes look, blinking, moods. Kept between frames.
// =====================================================================

struct Pose {
  float gx, gy;      // gaze, -1..1 (right / down are positive)
  float blink;       // 0 open .. 1 shut
  float happy;       // 0..1 extra smile for a moment (new minute)
  float breathe;     // -1..1
  float beat;        // -1..1, heartbeat for heart eyes
  float v[P_COUNT];  // the current expression
};

static uint32_t rngState = 2463534242u;
static float rnd() {  // 0..1, cheap and repeatable
  rngState = rngState * 1664525u + 1013904223u;
  return (rngState >> 8) / 16777216.0f;
}

static int pickMood(bool night) {
  float total = 0;
  for (int i = 1; i < MOOD_COUNT; i++) total += night ? MOODS[i].weightNight : MOODS[i].weightDay;
  float r = rnd() * total;
  for (int i = 1; i < MOOD_COUNT; i++) {
    r -= night ? MOODS[i].weightNight : MOODS[i].weightDay;
    if (r <= 0) return i;
  }
  return HAPPY;
}

static Pose animate(const ClockInfo &info, uint32_t ms) {
  static bool started = false;
  static uint32_t lastMs, nextLook, nextBlink, blinkStart, moodUntil;
  static bool blinking = false;
  static float gx = 0, gy = 0, tx = 0, ty = 0, happy = 0;
  static float cur[P_COUNT];
  static int mood = NEUTRAL, lastMood = -1, lastMinute = -1;
  if (!started) {
    started = true;
    lastMs = ms;
    nextLook = ms + 800;
    nextBlink = ms + 2500;
    moodUntil = ms + 5000;
    memcpy(cur, MOODS[NEUTRAL].v, sizeof(cur));
  }
  float dt = (ms - lastMs) * 0.001f;
  if (dt > 0.1f) dt = 0.1f;  // after a pause (cover art was showing) don't jump
  lastMs = ms;

  bool night = info.timeKnown && (info.hour >= 23 || info.hour < 6);

  // Choose the mood: mostly neutral, now and then something else for a few seconds
  if (forcedMood >= 0) {
    mood = forcedMood;
    moodUntil = ms + 1000000;
  } else if ((int32_t)(ms - moodUntil) >= 0) {
    if (mood != NEUTRAL) {
      mood = NEUTRAL;
      moodUntil = ms + (night ? 3000u : 5000u) + (uint32_t)(rnd() * (night ? 5000 : 9000));
    } else {
      mood = pickMood(night);
      moodUntil = ms + (uint32_t)((MOODS[mood].minS + rnd() * (MOODS[mood].maxS - MOODS[mood].minS)) * 1000);
    }
  }
  const MoodDef &def = MOODS[mood];
  if (mood != lastMood) {  // new mood: look the way it wants to straight away
    lastMood = mood;
    nextLook = ms;
  }

  // New minute: glance down at the time and smile (not while asleep)
  if (info.timeKnown) {
    if (lastMinute >= 0 && info.minute != lastMinute && mood != SLEEP) {
      tx = 0;
      ty = 1.0f;
      nextLook = ms + 1800;
      happy = 1.0f;
    }
    lastMinute = info.minute;
  }

  // Where to look next
  bool sleepyNeutral = night && mood == NEUTRAL;
  if ((int32_t)(ms - nextLook) >= 0) {
    uint32_t wait = 1200 + (uint32_t)(rnd() * 2600);
    switch (def.gaze) {
      case G_UP:     tx = (rnd() - 0.5f) * 0.8f; ty = -0.9f; wait = 900 + (uint32_t)(rnd() * 900); break;
      case G_DOWN:   tx = -0.3f + (rnd() - 0.5f) * 0.4f; ty = 0.9f; wait = 1200; break;
      case G_CENTER: tx = (rnd() - 0.5f) * 0.2f; ty = (rnd() - 0.5f) * 0.2f; wait = 1500; break;
      case G_DART:   tx = rnd() < 0.5f ? -0.8f : 0.8f; ty = 0.6f; wait = 250 + (uint32_t)(rnd() * 300); break;
      case G_SCAN:   tx = (rnd() - 0.5f) * 2.2f; ty = (rnd() - 0.5f) * 1.4f; wait = 500 + (uint32_t)(rnd() * 700); break;
      case G_REST:   tx = 0; ty = 0.4f; wait = 2000; break;
      default: {  // wander
        float r = rnd();
        if (info.weatherKnown && r < 0.12f) {  // peek down-right at the temperature above PM
          tx = 1.0f;
          ty = 0.9f;
        } else if (r < 0.45f) {  // back to looking at you
          tx = (rnd() - 0.5f) * 0.3f;
          ty = (rnd() - 0.5f) * 0.3f;
        } else {
          tx = (rnd() - 0.5f) * 2.0f;
          ty = (rnd() - 0.5f) * 1.5f;
        }
      }
    }
    if (sleepyNeutral) { tx *= 0.5f; ty *= 0.5f; }
    nextLook = ms + wait;
  }
  float kLook = 1.0f - expf(-dt / 0.06f);  // quick, slightly springy eye jumps
  gx += (tx - gx) * kLook;
  gy += (ty - gy) * kLook;
  happy = happy > dt * 0.6f ? happy - dt * 0.6f : 0.0f;

  // Ease every expression number toward the current mood
  float kMood = 1.0f - expf(-dt / 0.14f);
  for (int i = 0; i < P_COUNT; i++) {
    float target = def.v[i];
    if (i == P_LID && sleepyNeutral) target = LID_SLEEPY;
    cur[i] += (target - cur[i]) * kMood;
  }

  // Blinking (sometimes twice in a row; slower when sleepy). Not while asleep.
  if (!blinking && (int32_t)(ms - nextBlink) >= 0) {
    blinking = true;
    blinkStart = ms;
  }
  float blink = 0;
  if (blinking) {
    float e = (ms - blinkStart) * 0.001f, dur = sleepyNeutral ? 0.32f : 0.18f;
    if (e >= dur) {
      blinking = false;
      nextBlink = ms + (rnd() < 0.18f ? 220u : 2200u + (uint32_t)(rnd() * 4200));
    } else {
      float u = e / dur;
      blink = u < 0.45f ? u / 0.45f : (1.0f - u) / 0.55f;
    }
  }

  Pose p;
  p.gx = gx;
  p.gy = gy;
  p.blink = mood == SLEEP ? 0.0f : blink;
  p.happy = mood == SLEEP ? 0.0f : happy;
  p.breathe = sinf(ms * 0.001f * 1.5708f);                       // one breath every 4 s
  p.beat = sinf((ms % 1000) * 0.001f * 6.2832f * 2.0f);          // two beats a second
  memcpy(p.v, cur, sizeof(cur));
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

static float breatheScale(const Pose &p) {  // deeper breaths when asleep
  return 1.0f + 0.008f * (1.0f + 1.5f * p.v[P_ZZZ]) * p.breathe;
}

static void drawHead(const Pose &p) {
  float R = HEAD_R * breatheScale(p);
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

// Signed distance to a triangle given in clockwise screen order (negative inside)
static float distTri(float px, float py, const float v[3][2]) {
  float worst = -1e9f;
  for (int i = 0; i < 3; i++) {
    const float *a = v[i], *b = v[(i + 1) % 3];
    float dx = b[0] - a[0], dy = b[1] - a[1];
    float len = sqrtf(dx * dx + dy * dy);
    float d = (dy * (px - a[0]) - dx * (py - a[1])) / len;  // outward normal is (dy, -dx)
    if (d > worst) worst = d;
  }
  return worst;
}

static void drawHeart(float cx, float cy, float sz, float alpha) {
  static const float RED[3] = {255, 64, 108}, GLINT[3] = {255, 200, 215};
  float r = 0.52f * sz, lobeDx = 0.48f * sz, lobeY = cy - 0.3f * sz;
  float tri[3][2] = {{cx - 0.97f * sz, lobeY + 0.12f * sz}, {cx + 0.97f * sz, lobeY + 0.12f * sz}, {cx, cy + 1.0f * sz}};
  shape(cx - sz - 1, cy - sz - 1, cx + sz + 1, cy + sz + 1,
        [=](float x, float y) {
          float d1 = distCircle(x, y, cx - lobeDx, lobeY, r);
          float d2 = distCircle(x, y, cx + lobeDx, lobeY, r);
          float d3 = distTri(x, y, tri);
          float d = d1 < d2 ? d1 : d2;
          return d < d3 ? d : d3;
        },
        RED, alpha);
  disc(cx - 0.45f * sz, lobeY - 0.1f * sz, 0.5f, GLINT, alpha * 0.9f);
}

static void drawEye(float cx, float cy, const Pose &p, float closure) {
  const float rx = EYE_RX * p.v[P_WIDEN], ry = EYE_RY * p.v[P_WIDEN];
  const float pupilR = PUPIL_R * p.v[P_PUPIL];
  float hearts = p.v[P_HEARTS];
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
      blend(x, y, INK, clamp01(0.5f - distCircle(px, py, pcx, pcy, pupilR)) * eye * (1.0f - hearts));
      blend(x, y, EYE_WHITE, clamp01(0.5f - distCircle(px, py, pcx - 1.1f, pcy - 1.3f, 0.9f)) * eye * 0.9f * (1.0f - hearts));
    }
  }
  if (hearts > 0.02f && closure < 0.6f) {  // heart eyes, beating a little
    drawHeart(pcx, pcy - 0.2f, 4.1f * (1.0f + 0.08f * p.beat), hearts * (1.0f - closure));
  }
  if (closure > 0.55f) {  // fully shut: a little line
    float a = clamp01((closure - 0.55f) / 0.3f);
    float ly = lidY < cy + ry * 0.6f ? lidY : cy + ry * 0.6f;
    capsule(cx - 4.5f, ly, cx + 4.5f, ly, 0.9f, INK, a);
  }
}

// Floating Z's while asleep
static void drawZzz(float amount, uint32_t ms) {
  if (amount < 0.02f) return;
  static const float ZCOL[3] = {255, 255, 255}, ZSHADE[3] = {40, 55, 140};
  float t = (ms % 60000) * 0.001f;  // 3 s per Z, repeats cleanly within 60 s
  for (int i = 0; i < 3; i++) {
    float f = fmodf(t / 3.0f + i / 3.0f, 1.0f);
    float x = 46.0f + f * 9.0f, y = 29.0f - f * 15.0f, s = 2.0f + f * 2.4f;
    float a = amount * clamp01(sinf(f * 3.1416f) * 2.0f);  // quick fade in and out, solid in between
    float r = 0.6f + s * 0.1f;
    for (int pass = 0; pass < 2; pass++) {  // dark shadow first so the white reads on yellow
      float o = pass == 0 ? 0.9f : 0.0f;
      const float *col = pass == 0 ? ZSHADE : ZCOL;
      capsule(x - s + o, y - s + o, x + s + o, y - s + o, r, col, a);
      capsule(x + s + o, y - s + o, x - s + o, y + s + o, r, col, a);
      capsule(x - s + o, y + s + o, x + s + o, y + s + o, r, col, a);
    }
  }
}

static void drawFace(const Pose &p, uint32_t ms) {
  float s = breatheScale(p);
  float shake = p.v[P_SHAKE] * sinf((ms % 1000) * 0.001f * 6.2832f * 13.0f) * 0.6f;
  float turnX = p.gx * 1.6f + shake, turnY = p.gy * 1.0f;  // the whole face turns a little toward where it looks
  float closure = clamp01(p.v[P_LID] + p.blink * (1.0f - p.v[P_LID]));
  float eyeTop = EYE_Y - EYE_RY * p.v[P_WIDEN];

  for (int side = -1; side <= 1; side += 2) {
    float cx = HEAD_CX + side * EYE_DX * s + turnX;
    float cy = EYE_Y + turnY;
    float c = side > 0 ? (closure > p.v[P_WINK] ? closure : p.v[P_WINK]) : closure;  // wink = right eye
    drawEye(cx, cy, p, c);

    // Brows: height and tilt come from the mood; looking up lifts them a little
    float lift = (side < 0 ? p.v[P_BROW_L] : p.v[P_BROW_R]) - p.gy * 0.8f + p.happy * 1.2f;
    lift -= (p.v[P_LID] - LID_AWAKE) * 3.0f;  // droopy when sleepy
    float tilt = side < 0 ? p.v[P_TILT_L] : p.v[P_TILT_R];
    float by = eyeTop + turnY - 3.2f - lift;
    float outerX = cx + side * 7.4f, innerX = cx - side * 6.4f;
    capsule(outerX, by - 0.2f, innerX, by + tilt, 1.35f, BROW);
  }

  // Mouth: a curve (smile / frown / smirk / wobble) that opens into an "o" when surprised
  float mx = 33.5f + p.v[P_MOUTH_X] + turnX, my = 39.5f + turnY;
  float hw = 5.5f * p.v[P_MOUTH_W], amp = p.v[P_SMILE] + 1.5f * p.happy;
  float skew = p.v[P_SKEW], wave = p.v[P_WAVE], open = p.v[P_OPEN];
  float phase = (ms % 1000) * 0.001f * 6.2832f * 3.0f;
  float reach = (amp < 0 ? -amp : amp) + (skew < 0 ? -skew : skew) + wave + 3.0f;
  if (open < 0.95f) {
    shape(mx - hw - 2, my - reach, mx + hw + 2, my + reach + 1,
          [=](float x, float y) {
            float u = (x - mx) / hw;
            float d;
            if (u >= -1.0f && u <= 1.0f) {
              float yc = my + amp * (1.0f - u * u) + skew * u + wave * 0.7f * sinf(u * 9.0f + phase);
              float slope = -2.0f * amp * u / hw + skew / hw + wave * 0.7f * 9.0f * cosf(u * 9.0f + phase) / hw;
              d = fabsf(y - yc) / sqrtf(1.0f + slope * slope);
            } else {
              float ex = mx + (u > 0 ? hw : -hw);
              float ey = my + skew * (u > 0 ? 1.0f : -1.0f) + wave * 0.7f * sinf((u > 0 ? 9.0f : -9.0f) + phase);
              d = distCircle(x, y, ex, ey, 0.0f);
            }
            return d - 1.0f;
          },
          INK, 1.0f - open);
  }
  if (open > 0.05f) {
    float orx = 1.0f + 1.3f * open, ory = 0.8f + 2.0f * open, ocy = my + 1.5f;
    shape(mx - orx - 1, ocy - ory - 1, mx + orx + 1, ocy + ory + 1,
          [=](float x, float y) { return distEllipse(x, y, mx, ocy, orx, ory); }, INK, open);
  }

  drawZzz(p.v[P_ZZZ], ms);
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

// ---- Temperature: 3x5 digits so "35" and the degree ring fit next to a 2-digit hour ----
static const uint8_t MICRO_DIGITS[11][5] = {{7, 5, 5, 5, 7}, {2, 6, 2, 2, 7}, {7, 1, 7, 4, 7}, {7, 1, 7, 1, 7},
                                            {5, 5, 7, 1, 1}, {7, 4, 7, 1, 7}, {7, 4, 7, 5, 7}, {7, 1, 2, 2, 2},
                                            {7, 5, 7, 5, 7}, {7, 5, 7, 1, 7}, {0, 0, 7, 0, 0}};  // 10 = minus

// Characters to draw for the temperature (digits, 10 = minus); returns how many.
// *ring says whether the degree ring fits (three characters, e.g. -12, leave no room).
static int tempChars(int tempC, int digits[3], bool *ring) {
  int v = tempC, n = 0;
  if (v < 0) digits[n++] = 10;
  if (v < 0) v = -v;
  if (v > 99) v = 99;
  if (v >= 10) digits[n++] = v / 10;
  digits[n++] = v % 10;
  *ring = n < 3;
  return n;
}

static int tempWidth(const ClockInfo &info) {
  if (!info.weatherKnown) return 0;
  int digits[3];
  bool ring;
  int n = tempChars(info.tempC, digits, &ring);
  return n * 4 - 1 + (ring ? 4 : 0);
}

// Drawn with its left edge at x, level with the top of the time digits
static void drawTemp(const ClockInfo &info, int x) {
  int digits[3];
  bool ring;
  int n = tempChars(info.tempC, digits, &ring);
  for (int i = 0; i < n; i++, x += 4) micro(x, TEMP_Y, MICRO_DIGITS[digits[i]], TEMP_COL);
  if (ring) {
    static const uint8_t degree[5] = {2, 5, 2, 0, 0};  // 3x3 ring at the top
    micro(x, TEMP_Y, degree, TEMP_COL);
  }
}

// The time with a column to its right: AM/PM at the bottom, the temperature above it, both
// starting on the same vertical line. The whole group is centred on the panel, so the time
// makes way when the column is wide. Returns the x of that line, or -1 if the time is unknown.
static int drawTime(const ClockInfo &info) {
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
  int tw = tempWidth(info), column = tw > 7 ? tw : 7;  // AM/PM is 7 wide
  if (info.timeKnown) width += 3 + column;
  int x = (W - width) / 2, y = TIME_Y;

  int i = 0;
  for (int k = 0; k < hourDigits; k++, i++, x += 12) glyph(x, y, d[i], 2, TIME_COL);
  for (int dy : {3, 9}) {  // colon sits in the gap left by the last hour digit
    for (int py = 0; py < 2; py++)
      for (int px = 0; px < 2; px++) blend(x + px, y + dy + py, TIME_COL, 1.0f);
  }
  x += 4;
  for (int k = 0; k < 2; k++, i++, x += 12) glyph(x, y, d[i], 2, TIME_COL);

  if (!info.timeKnown) return -1;
  x += 1;  // 3 clear pixels after the last digit (it already has 2 of its own)
  micro(x, AMPM_Y, info.hour < 12 ? MICRO_A : MICRO_P, AMPM_COL);
  micro(x + 4, AMPM_Y, MICRO_M, AMPM_COL);
  return x;
}

void clockDraw(uint8_t *rgb, const ClockInfo &info, uint32_t ms) {
  fb = rgb;
  memset(rgb, 0, W * H * 3);
  Pose pose = animate(info, ms);
  drawHead(pose);
  drawFace(pose, ms);
  int columnX = drawTime(info);
  if (info.weatherKnown) drawTemp(info, columnX >= 0 ? columnX : 53);
}
