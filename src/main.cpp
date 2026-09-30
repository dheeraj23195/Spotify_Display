#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <Adafruit_GFX.h>
#include <ESP32-HUB75-MatrixPanel-I2S-DMA.h>
#include <JPEGDEC.h>
#include "secrets.h"

// ---------- Panel wiring (verified) ----------
HUB75_I2S_CFG::i2s_pins pins = {
  1, 2, 4,           // R1, G1, B1
  5, 6, 7,           // R2, G2, B2
  8, 9, 10, 11, 12,  // A, B, C, D, E
  14, 38, 13         // LAT, OE, CLK
};

// ---------- Orientation ----------
// Quarter-turns needed to match how the panel sits (0, 1, 2 or 3)
const uint8_t PANEL_ROTATION = 1;

// ---------- Test image source (random 64x64 photo) ----------
const char *IMAGE_URL = "https://picsum.photos/64";

const int W = 64, H = 64;
MatrixPanel_I2S_DMA *display = nullptr;
GFXcanvas16 canvas(W, H);  // we draw here, then push to the panel
JPEGDEC jpeg;

// Copy the canvas to the panel, applying the rotation
void pushCanvas() {
  uint16_t *buf = canvas.getBuffer();
  for (int y = 0; y < H; y++) {
    for (int x = 0; x < W; x++) {
      int px, py;
      switch (PANEL_ROTATION) {
        case 1:  px = y;         py = H - 1 - x; break;
        case 2:  px = W - 1 - x; py = H - 1 - y; break;
        case 3:  px = W - 1 - y; py = x;         break;
        default: px = x;         py = y;         break;
      }
      display->drawPixel(px, py, buf[y * W + x]);
    }
  }
}

void showMessage(const char *msg) {
  canvas.fillScreen(0);
  canvas.setTextColor(display->color565(255, 255, 255));
  canvas.setCursor(2, 2);
  canvas.print(msg);
  pushCanvas();
}

bool connectWiFi() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.print("Connecting to Wi-Fi");
  for (int i = 0; i < 40 && WiFi.status() != WL_CONNECTED; i++) {
    delay(500);
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
  client.setInsecure();  // skips certificate check; fine for public test images
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
  int len = http.getSize();  // -1 if the server doesn't say
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
      delay(1);
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

// JPEGDEC calls this with blocks of decoded pixels
int jpegDraw(JPEGDRAW *p) {
  canvas.drawRGBBitmap(p->x, p->y, p->pPixels, p->iWidth, p->iHeight);
  return 1;
}

bool showJpegFromUrl(const char *url) {
  size_t len = 0;
  uint8_t *data = downloadFile(url, len);
  if (!data) return false;

  bool ok = false;
  if (jpeg.openRAM(data, len, jpegDraw)) {
    Serial.printf("Downloaded %u bytes, image %dx%d\n",
                  len, jpeg.getWidth(), jpeg.getHeight());
    canvas.fillScreen(0);
    ok = jpeg.decode(0, 0, 0);
    jpeg.close();
    if (ok) pushCanvas();
  }
  if (!ok) Serial.printf("JPEG decode failed, error %d\n", jpeg.getLastError());
  free(data);
  return ok;
}

void setup() {
  Serial.begin(115200);
  delay(1000);

  HUB75_I2S_CFG cfg(W, H, 1, pins);
  cfg.driver = HUB75_I2S_CFG::FM6126A;  // panel uses FM6124 chips
  cfg.clkphase = false;                 // fixes the one-pixel shift
  display = new MatrixPanel_I2S_DMA(cfg);
  if (!display->begin()) {
    Serial.println("Display init FAILED");
    while (true) delay(1000);
  }
  display->setBrightness8(40);
  display->clearScreen();
  Serial.println("Display started");

  showMessage("WIFI...");
  if (!connectWiFi()) {
    showMessage("NO WIFI");
    while (true) delay(1000);
  }
}

void loop() {
  if (!showJpegFromUrl(IMAGE_URL)) {
    showMessage("IMG ERR");
  }
  delay(10000);  // new photo every 10 seconds
}