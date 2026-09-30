#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <Adafruit_GFX.h>
#include <ESP32-HUB75-MatrixPanel-I2S-DMA.h>
#include <JPEGDEC.h>
#include "secrets.h"
#include "spotify.h"

// ---------- Panel wiring (verified) ----------
HUB75_I2S_CFG::i2s_pins pins = {
  1, 2, 4,           // R1, G1, B1
  5, 6, 7,           // R2, G2, B2
  8, 9, 10, 11, 12,  // A, B, C, D, E
  14, 38, 13         // LAT, OE, CLK
};

// ---------- Settings ----------
const uint8_t PANEL_ROTATION = 1;    // quarter-turns (0-3)
const uint8_t BRIGHTNESS = 80;       // 0-255
const unsigned long POLL_MS = 4000;  // how often to ask Spotify
const uint16_t FADE_MS = 800;        // crossfade between songs
const int FPS = 30;                  // animation frame rate
const int MAX_SRC = 320;             // largest decoded size before downscaling

// Camera orbit around the cover
const float ORBIT_DEG = 9.0f;        // how far the camera swings each way
const float ORBIT_PERIOD_S = 10.0f;  // one full left-right-left swing
const float CAM_DIST = 2.0f;         // lower = stronger perspective

// Pause effect
const float PAUSE_BORDER = 3.0f;     // black border (LEDs per side) when paused
const float PAUSE_DIM = 0.45f;       // brightness while paused (1.0 = no dimming)
const uint16_t PAUSE_ANIM_MS = 700;  // how long the zoom-out/in takes

// ---------- Buffers ----------
const int W = 64, H = 64, NPIX = W * H;
const int ART = 128;  // working resolution for effects (2x the panel)

struct Art {
  uint8_t *px = nullptr;  // ART x ART, RGB888
  bool animate = false;   // true = album art (effects), false = text/static
};

MatrixPanel_I2S_DMA *display = nullptr;
GFXcanvas16 canvas(W, H);     // for drawing text messages (network task)
JPEGDEC jpeg;                 // used by the network task only
uint16_t *srcImage = nullptr; // full-size decoded JPEG
int srcW = 0, srcH = 0;

Art artA, artB;
Art *current = &artA;         // being rendered (render loop owns this)
Art *pending = &artB;         // being prepared (network task fills this)
bool pendingReady = false;    // protected by artMutex
SemaphoreHandle_t artMutex;
volatile bool isPlaying = true;

uint8_t *outFrame = nullptr;   // RGB888 frame rendered from the art
uint8_t *blendFrame = nullptr; // RGB888 crossfade result
uint8_t *fadeFrom = nullptr;   // RGB888 snapshot of the old picture
float orbitZoom = 1.0f;        // computed at startup so edges never show

// =====================================================================
//  Art hand-over between the two cores
// =====================================================================

// Network task: wait until the pending slot is free, then return it
Art *beginArt() {
  while (true) {
    xSemaphoreTake(artMutex, portMAX_DELAY);
    bool busy = pendingReady;
    Art *slot = pending;
    xSemaphoreGive(artMutex);
    if (!busy) return slot;
    vTaskDelay(pdMS_TO_TICKS(10));
  }
}

// Network task: hand the finished art to the render loop
void publishArt() {
  xSemaphoreTake(artMutex, portMAX_DELAY);
  pendingReady = true;
  xSemaphoreGive(artMutex);
}

// =====================================================================
//  Network side (core 0)
// =====================================================================

void showMessage(const char *msg) {
  canvas.fillScreen(0);
  canvas.setTextColor(0xFFFF);
  canvas.setCursor(2, 2);
  canvas.print(msg);

  Art *a = beginArt();
  uint16_t *src = canvas.getBuffer();
  for (int y = 0; y < ART; y++) {
    for (int x = 0; x < ART; x++) {
      uint16_t c = src[(y / 2) * W + (x / 2)];  // 2x nearest upscale
      uint8_t *d = &a->px[(y * ART + x) * 3];
      d[0] = ((c >> 11) & 0x1F) * 255 / 31;
      d[1] = ((c >> 5) & 0x3F) * 255 / 63;
      d[2] = (c & 0x1F) * 255 / 31;
    }
  }
  a->animate = false;
  publishArt();
}

bool connectWiFi() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.print("Connecting to Wi-Fi");
  for (int i = 0; i < 40 && WiFi.status() != WL_CONNECTED; i++) {
    vTaskDelay(pdMS_TO_TICKS(500));
    Serial.print(".");
  }
  Serial.println();
  if (WiFi.status() == WL_CONNECTED) {
    Serial.print("Connected, IP: ");
    Serial.println(WiFi.localIP());
    return true;
  }
  Serial.println("Wi-Fi FAILED");
  return false;
}

// Download a file into PSRAM. Returns buffer (caller frees) or nullptr.
uint8_t *downloadFile(const char *url, size_t &outLen) {
  WiFiClientSecure client;
  client.setInsecure();  // TODO: verify certificates (hardening step)
  HTTPClient http;
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  if (!http.begin(client, url)) {
    Serial.println("HTTP begin failed");
    return nullptr;
  }
  int code = http.GET();
  if (code != HTTP_CODE_OK) {
    Serial.printf("HTTP error %d\n", code);
    http.end();
    return nullptr;
  }

  const size_t MAX_SIZE = 200 * 1024;
  int len = http.getSize();
  size_t cap = (len > 0 && (size_t)len <= MAX_SIZE) ? len : MAX_SIZE;
  uint8_t *buf = (uint8_t *)ps_malloc(cap);
  if (!buf) {
    Serial.println("Out of memory");
    http.end();
    return nullptr;
  }

  WiFiClient *stream = http.getStreamPtr();
  size_t got = 0;
  unsigned long start = millis();
  while ((http.connected() || stream->available()) && got < cap &&
         millis() - start < 10000) {
    size_t avail = stream->available();
    if (avail) {
      got += stream->readBytes(buf + got, min(avail, cap - got));
    } else {
      vTaskDelay(1);
    }
  }
  http.end();

  if (got == 0 || (len > 0 && got != (size_t)len)) {
    Serial.printf("Incomplete download: %u bytes\n", got);
    free(buf);
    return nullptr;
  }
  outLen = got;
  return buf;
}

// JPEGDEC calls this with blocks of decoded pixels: copy them into srcImage
int jpegDraw(JPEGDRAW *p) {
  int w = min((int)p->iWidth, srcW - p->x);
  int h = min((int)p->iHeight, srcH - p->y);
  if (w <= 0 || h <= 0) return 1;
  for (int row = 0; row < h; row++) {
    memcpy(&srcImage[(p->y + row) * srcW + p->x],
           &p->pPixels[row * p->iWidth], w * sizeof(uint16_t));
  }
  return 1;
}

// Shrink srcImage to ART x ART by averaging the source pixels in each cell
void downscaleToArt(uint8_t *dst) {
  for (int ty = 0; ty < ART; ty++) {
    int y0 = ty * srcH / ART, y1 = (ty + 1) * srcH / ART;
    if (y1 <= y0) y1 = y0 + 1;
    for (int tx = 0; tx < ART; tx++) {
      int x0 = tx * srcW / ART, x1 = (tx + 1) * srcW / ART;
      if (x1 <= x0) x1 = x0 + 1;
      uint32_t r = 0, g = 0, b = 0, n = 0;
      for (int y = y0; y < y1; y++) {
        for (int x = x0; x < x1; x++) {
          uint16_t c = srcImage[y * srcW + x];
          r += (c >> 11) & 0x1F;
          g += (c >> 5) & 0x3F;
          b += c & 0x1F;
          n++;
        }
      }
      uint8_t *d = &dst[(ty * ART + tx) * 3];
      d[0] = r * 255 / (31 * n);
      d[1] = g * 255 / (63 * n);
      d[2] = b * 255 / (31 * n);
    }
  }
}

bool loadCover(const char *url) {
  size_t len = 0;
  uint8_t *data = downloadFile(url, len);
  if (!data) return false;

  bool ok = false;
  if (jpeg.openRAM(data, len, jpegDraw)) {
    int w = jpeg.getWidth(), h = jpeg.getHeight();
    int scale = 0, shift = 0;  // let the decoder pre-shrink very large images
    if (w > MAX_SRC * 4 || h > MAX_SRC * 4)      { scale = JPEG_SCALE_EIGHTH;  shift = 3; }
    else if (w > MAX_SRC * 2 || h > MAX_SRC * 2) { scale = JPEG_SCALE_QUARTER; shift = 2; }
    else if (w > MAX_SRC || h > MAX_SRC)         { scale = JPEG_SCALE_HALF;    shift = 1; }
    srcW = w >> shift;
    srcH = h >> shift;

    if (srcW > MAX_SRC || srcH > MAX_SRC || srcW == 0 || srcH == 0) {
      Serial.printf("Image too large: %dx%d\n", w, h);
    } else {
      Serial.printf("Image %dx%d (decoded %dx%d), %u bytes\n", w, h, srcW, srcH, len);
      ok = jpeg.decode(0, 0, scale);
    }
    jpeg.close();
  }
  free(data);
  if (!ok) {
    Serial.printf("JPEG decode failed, error %d\n", jpeg.getLastError());
    return false;
  }

  Art *a = beginArt();
  downscaleToArt(a->px);
  a->animate = true;
  publishArt();
  return true;
}

void networkTask(void *) {
  showMessage("WIFI...");
  while (!connectWiFi()) {
    showMessage("NO WIFI");
    vTaskDelay(pdMS_TO_TICKS(10000));
  }
  showMessage("SPOTIFY");
  spotifyBegin();

  String shownTrackId;
  for (;;) {
    if (WiFi.status() != WL_CONNECTED) {
      showMessage("NO WIFI");
      shownTrackId = "";
      WiFi.reconnect();
      vTaskDelay(pdMS_TO_TICKS(5000));
      continue;
    }

    NowPlaying np;
    switch (spotifyGetNowPlaying(np)) {
      case SpotifyResult::Ok:
        isPlaying = np.isPlaying;
        if (np.trackId != shownTrackId) {
          Serial.printf("Now playing: %s - %s\n", np.title.c_str(), np.artist.c_str());
          if (np.artUrl.isEmpty()) {
            showMessage("NO ART");
            shownTrackId = np.trackId;
          } else if (loadCover(np.artUrl.c_str())) {
            shownTrackId = np.trackId;
          }  // if the download failed, we retry on the next poll
        }
        break;
      case SpotifyResult::NothingPlaying:
        isPlaying = false;
        if (shownTrackId != "idle") {
          showMessage("NO MUSIC");
          shownTrackId = "idle";
        }
        break;
      case SpotifyResult::RateLimited:
        Serial.println("Rate limited by Spotify, waiting 30 s");
        vTaskDelay(pdMS_TO_TICKS(30000));
        break;
      case SpotifyResult::AuthError:
        Serial.println("Spotify auth problem, will retry");
        break;
      default:
        break;  // network hiccup: keep the current picture
    }
    vTaskDelay(pdMS_TO_TICKS(POLL_MS));
  }
}

// =====================================================================
//  Render side (core 1)
// =====================================================================

// Send an RGB888 frame to the panel, applying the rotation
void pushFrame(const uint8_t *f) {
  for (int y = 0; y < H; y++) {
    for (int x = 0; x < W; x++) {
      int px, py;
      switch (PANEL_ROTATION) {
        case 1:  px = y;         py = H - 1 - x; break;
        case 2:  px = W - 1 - x; py = H - 1 - y; break;
        case 3:  px = W - 1 - y; py = x;         break;
        default: px = x;         py = y;         break;
      }
      const uint8_t *p = &f[(y * W + x) * 3];
      display->drawPixelRGB888(px, py, p[0], p[1], p[2]);
    }
  }
}

// Map a screen point (X, Y in -1..1) to cover coordinates (s, t in -1..1)
// for a camera swung by 'theta' around a flat cover. Returns lambda (depth).
inline float projectColumn(float X, float ct, float st, float f, float &s) {
  float lam = CAM_DIST * ct / (ct - st * X / f);
  s = lam * X / f * ct + (lam - CAM_DIST) * st;
  return lam;
}

// Smallest zoom that keeps the cover filling the screen at the widest swing
float computeOrbitZoom() {
  float th = ORBIT_DEG * DEG_TO_RAD, ct = cosf(th), st = sinf(th);
  for (float k = 1.0f; k < 1.5f; k += 0.002f) {
    float f = CAM_DIST * k;
    bool ok = true;
    for (int side = -1; side <= 1; side += 2) {
      float s;
      float lam = projectColumn((float)side, ct, st, f, s);
      if (fabsf(s) > 1.0f || lam / f > 1.0f) ok = false;
    }
    if (ok) return k;
  }
  return 1.5f;
}

// Render the cover as seen by the orbiting camera.
// shrink < 1 adds a black border; gain < 1 dims the picture.
void renderView(const Art *a, float theta, float zoom, float shrink, float gain,
                uint8_t *out) {
  const float f = CAM_DIST * zoom;
  const float ct = cosf(theta), st = sinf(theta);
  const float half = W / 2.0f;
  const float hs = half * shrink;  // half-size of the visible cover, in LEDs

  float colLam[W], colS[W], colAlpha[W];
  for (int x = 0; x < W; x++) {
    float dx = x + 0.5f - half;
    colAlpha[x] = constrain(hs - fabsf(dx) + 0.5f, 0.0f, 1.0f);  // soft edge
    colLam[x] = projectColumn(dx / hs, ct, st, f, colS[x]);
  }

  for (int y = 0; y < H; y++) {
    float dy = y + 0.5f - half;
    float ay = constrain(hs - fabsf(dy) + 0.5f, 0.0f, 1.0f);
    float Y = dy / hs;
    for (int x = 0; x < W; x++) {
      uint8_t *o = &out[(y * W + x) * 3];
      float alpha = colAlpha[x] * ay * gain;
      if (alpha <= 0.0f) {
        o[0] = o[1] = o[2] = 0;
        continue;
      }
      float s = colS[x];
      float t = colLam[x] * Y / f;
      float u = (s + 1.0f) * 0.5f * ART - 0.5f;
      float v = (t + 1.0f) * 0.5f * ART - 0.5f;

      int iu = (int)floorf(u), iv = (int)floorf(v);
      float fu = u - iu, fv = v - iv;
      int xa = constrain(iu, 0, ART - 1), xb = constrain(iu + 1, 0, ART - 1);
      int ya = constrain(iv, 0, ART - 1), yb = constrain(iv + 1, 0, ART - 1);
      const uint8_t *p00 = &a->px[(ya * ART + xa) * 3];
      const uint8_t *p10 = &a->px[(ya * ART + xb) * 3];
      const uint8_t *p01 = &a->px[(yb * ART + xa) * 3];
      const uint8_t *p11 = &a->px[(yb * ART + xb) * 3];
      for (int c = 0; c < 3; c++) {  // bilinear blend = smooth sub-pixel motion
        float top = p00[c] + (p10[c] - p00[c]) * fu;
        float bot = p01[c] + (p11[c] - p01[c]) * fu;
        o[c] = (uint8_t)((top + (bot - top) * fv) * alpha + 0.5f);
      }
    }
  }
}

// Type a number (0-255) in the Serial Monitor and press Enter to change brightness
void handleSerial() {
  static String input;
  while (Serial.available()) {
    char c = Serial.read();
    if (c == '\n' || c == '\r') {
      if (input.length()) {
        int b = constrain(input.toInt(), 0, 255);
        display->setBrightness8(b);
        Serial.printf("Brightness set to %d\n", b);
        input = "";
      }
    } else if (isDigit(c)) {
      input += c;
    }
  }
}

// =====================================================================
//  Setup and render loop
// =====================================================================

void setup() {
  Serial.begin(115200);
  delay(1000);

  HUB75_I2S_CFG cfg(W, H, 1, pins);
  cfg.driver = HUB75_I2S_CFG::FM6126A;  // panel uses FM6124 chips
  cfg.clkphase = false;                 // fixes the one-pixel shift
  cfg.double_buff = true;               // draw off-screen, then swap: no tearing
  display = new MatrixPanel_I2S_DMA(cfg);
  if (!display->begin()) {
    Serial.println("Display init FAILED");
    while (true) delay(1000);
  }
  display->setBrightness8(BRIGHTNESS);
  display->clearScreen();

  artA.px    = (uint8_t *)ps_calloc(ART * ART * 3, 1);
  artB.px    = (uint8_t *)ps_calloc(ART * ART * 3, 1);
  outFrame   = (uint8_t *)ps_calloc(NPIX * 3, 1);
  blendFrame = (uint8_t *)ps_calloc(NPIX * 3, 1);
  fadeFrom   = (uint8_t *)ps_calloc(NPIX * 3, 1);
  srcImage   = (uint16_t *)ps_malloc(MAX_SRC * MAX_SRC * sizeof(uint16_t));
  if (!artA.px || !artB.px || !outFrame || !blendFrame || !fadeFrom || !srcImage) {
    Serial.println("Buffer allocation FAILED");
    while (true) delay(1000);
  }

  orbitZoom = computeOrbitZoom();
  Serial.printf("Orbit zoom: %.3f\n", orbitZoom);

  artMutex = xSemaphoreCreateMutex();
  xTaskCreatePinnedToCore(networkTask, "network", 16384, nullptr, 1, nullptr, 0);
  Serial.println("Display started");
}

void loop() {
  static unsigned long lastFrame = 0;
  static float animTime = 0;
  static float pauseAmt = 0;  // 0 = playing look, 1 = paused look
  static bool fading = false;
  static unsigned long fadeStart = 0;
  static const uint8_t *lastShown = nullptr;

  handleSerial();
  unsigned long now = millis();
  if (lastFrame != 0 && now - lastFrame < 1000UL / FPS) {
    delay(1);
    return;
  }
  float dt = lastFrame ? (now - lastFrame) / 1000.0f : 0;
  lastFrame = now;

  // Pick up new art from the network task
  bool newArt = false;
  xSemaphoreTake(artMutex, portMAX_DELAY);
  if (pendingReady) {
    Art *t = current;
    current = pending;
    pending = t;
    pendingReady = false;
    newArt = true;
  }
  xSemaphoreGive(artMutex);

  if (newArt) {
    if (lastShown) memcpy(fadeFrom, lastShown, NPIX * 3);
    fading = true;
    fadeStart = now;
    animTime = 0;  // each new cover starts facing the camera
  }

  if (current->animate) {
    // Ease smoothly between the playing and paused looks
    bool paused = !isPlaying;
    float step = dt * 1000.0f / PAUSE_ANIM_MS;
    pauseAmt = paused ? min(1.0f, pauseAmt + step) : max(0.0f, pauseAmt - step);
    float pe = pauseAmt * pauseAmt * (3.0f - 2.0f * pauseAmt);  // ease-in-out

    animTime += dt * (1.0f - pe);  // camera glides to a stop when paused
    float theta = ORBIT_DEG * DEG_TO_RAD * sinf(TWO_PI * animTime / ORBIT_PERIOD_S);
    float shrink = 1.0f - pe * PAUSE_BORDER / (W / 2.0f);
    float gain = 1.0f - pe * (1.0f - PAUSE_DIM);
    renderView(current, theta, orbitZoom, shrink, gain, outFrame);
  } else {
    pauseAmt = 0;
    renderView(current, 0.0f, 1.0f, 1.0f, 1.0f, outFrame);  // text: no effects
  }

  const uint8_t *show = outFrame;
  if (fading) {
    unsigned long e = now - fadeStart;
    if (e >= FADE_MS) {
      fading = false;
    } else {
      int t = e * 255 / FADE_MS;
      t = t * t * (765 - 2 * t) / 65025;  // ease-in-out curve
      for (int i = 0; i < NPIX * 3; i++) {
        blendFrame[i] = fadeFrom[i] + (outFrame[i] - fadeFrom[i]) * t / 255;
      }
      show = blendFrame;
    }
  }

  pushFrame(show);
  display->flipDMABuffer();  // show the finished frame all at once
  lastShown = show;
}