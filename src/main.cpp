#include <Arduino.h>
#include <WiFi.h>
#include <Adafruit_GFX.h>
#include <ESP32-HUB75-MatrixPanel-I2S-DMA.h>
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

const int W = 64, H = 64;
MatrixPanel_I2S_DMA *display = nullptr;
GFXcanvas16 canvas(W, H);  // we draw here, then push to the panel

bool wifiOk = false;

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

  wifiOk = connectWiFi();
}

void loop() {
  uint16_t red    = display->color565(255, 0, 0);
  uint16_t green  = display->color565(0, 255, 0);
  uint16_t blue   = display->color565(0, 0, 255);
  uint16_t yellow = display->color565(255, 255, 0);
  uint16_t white  = display->color565(255, 255, 255);

  canvas.fillScreen(0);
  canvas.drawFastHLine(0, 0, W, red);          // top
  canvas.drawFastVLine(W - 1, 0, H, green);    // right
  canvas.drawFastHLine(0, H - 1, W, blue);     // bottom
  canvas.drawFastVLine(0, 0, H, yellow);       // left
  canvas.setTextColor(white);
  canvas.setCursor(3, 3);
  canvas.print(wifiOk ? "WIFI OK" : "NO WIFI");
  pushCanvas();

  Serial.println(wifiOk ? "Wi-Fi connected" : "No Wi-Fi");
  delay(5000);
}