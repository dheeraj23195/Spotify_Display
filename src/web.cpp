#include "web.h"
#include <WiFi.h>
#include <WebServer.h>
#include <HTTPUpdateServer.h>
#include <ArduinoOTA.h>
#include <ESPmDNS.h>
#include "settings.h"
#include "secrets.h"

extern volatile bool otaActive;   // defined in main.cpp
extern volatile int otaProgress;

static const char *HOSTNAME = "wall-display";
static const char *WEB_USER = "admin";

static WebServer server(80);
static HTTPUpdateServer httpUpdater;

static bool checkAuth() {
  if (server.authenticate(WEB_USER, ADMIN_PASSWORD)) return true;
  server.requestAuthentication();
  return false;
}

static String slider(const char *name, const char *label, float value,
                     float mn, float mx, float step) {
  char buf[420];
  snprintf(buf, sizeof(buf),
           "<label>%s <output id='%s_o'>%g</output></label>"
           "<input type='range' name='%s' min='%g' max='%g' step='%g' value='%g' "
           "oninput=\"document.getElementById('%s_o').value=this.value\">",
           label, name, value, name, mn, mx, step, value, name);
  return String(buf);
}

static String page() {
  String h;
  h.reserve(5000);
  h += F("<!DOCTYPE html><html><head><meta charset='utf-8'>"
         "<meta name='viewport' content='width=device-width,initial-scale=1'>"
         "<title>Wall Display</title><style>"
         "body{font-family:-apple-system,system-ui,sans-serif;max-width:480px;margin:auto;"
         "padding:16px;background:#111;color:#eee}"
         "h1{font-size:1.4em}h2{font-size:1em;color:#999;margin-top:28px}"
         "label{display:flex;justify-content:space-between;margin-top:16px}"
         "input[type=range]{width:100%}"
         "button,a.btn{display:block;box-sizing:border-box;width:100%;margin-top:14px;padding:12px;"
         "border:0;border-radius:8px;background:#333;color:#eee;font-size:1em;"
         "text-align:center;text-decoration:none}"
         "#st{margin-top:12px;color:#1db954;min-height:1.2em}"
         "</style></head><body><h1>Wall Display</h1><form id='f'>");

  h += "<label>Display on <input type='checkbox' name='displayOn'";
  h += settings.displayOn ? " checked" : "";
  h += "></label>";
  h += slider("brightness", "Brightness", settings.brightness, 5, 255, 1);

  h += F("<h2>Camera orbit</h2>");
  h += slider("orbitDeg", "Swing (degrees)", settings.orbitDeg, 0, 15, 0.5);
  h += slider("orbitPeriodS", "Swing time (seconds)", settings.orbitPeriodS, 6, 60, 1);

  h += F("<h2>Pause look</h2>");
  h += slider("pauseDim", "Brightness when paused", settings.pauseDim, 0.1, 1, 0.05);
  h += slider("pauseBorder", "Border when paused (LEDs)", settings.pauseBorder, 0, 8, 1);

  h += F("<h2>Song change</h2>");
  h += slider("fadeMs", "Crossfade (ms)", settings.fadeMs, 0, 3000, 100);

  h += F("</form><div id='st'></div>"
         "<h2>Maintenance</h2>"
         "<a class='btn' href='/update'>Upload firmware file</a>"
         "<button onclick=\"if(confirm('Restart the display?'))"
         "fetch('/restart',{method:'POST'})\">Restart</button>"
         "<script>"
         "const f=document.getElementById('f'),st=document.getElementById('st');"
         "f.addEventListener('change',()=>{"
         "fetch('/save',{method:'POST',body:new URLSearchParams(new FormData(f))})"
         ".then(r=>st.textContent=r.ok?'Saved':'Save failed')"
         ".catch(()=>st.textContent='Save failed')});"
         "</script></body></html>");
  return h;
}

static void handleRoot() {
  if (!checkAuth()) return;
  server.send(200, "text/html", page());
}

static void handleSave() {
  if (!checkAuth()) return;
  // Checkboxes are only sent when ticked
  settings.displayOn = server.hasArg("displayOn");
  if (server.hasArg("brightness"))
    settings.brightness = constrain(server.arg("brightness").toInt(), 5, 255);
  if (server.hasArg("orbitDeg"))
    settings.orbitDeg = constrain(server.arg("orbitDeg").toFloat(), 0.0f, 15.0f);
  if (server.hasArg("orbitPeriodS"))
    settings.orbitPeriodS = constrain(server.arg("orbitPeriodS").toFloat(), 6.0f, 60.0f);
  if (server.hasArg("pauseDim"))
    settings.pauseDim = constrain(server.arg("pauseDim").toFloat(), 0.1f, 1.0f);
  if (server.hasArg("pauseBorder"))
    settings.pauseBorder = constrain(server.arg("pauseBorder").toFloat(), 0.0f, 8.0f);
  if (server.hasArg("fadeMs"))
    settings.fadeMs = constrain(server.arg("fadeMs").toInt(), 0, 3000);
  settingsSave();
  server.send(200, "text/plain", "OK");
}

static void handleRestart() {
  if (!checkAuth()) return;
  server.send(200, "text/plain", "Restarting");
  delay(500);
  ESP.restart();
}

static void webTask(void *) {
  for (;;) {
    server.handleClient();
    ArduinoOTA.handle();
    vTaskDelay(2);
  }
}

void webBegin() {
  // Code push from VS Code over Wi-Fi
  ArduinoOTA.setHostname(HOSTNAME);
  ArduinoOTA.setPassword(ADMIN_PASSWORD);
  ArduinoOTA.onStart([]() {
    otaProgress = 0;
    otaActive = true;
    Serial.println("Wi-Fi update started");
  });
  ArduinoOTA.onProgress([](unsigned int done, unsigned int total) {
    otaProgress = done * 100 / total;
  });
  ArduinoOTA.onEnd([]() {
    otaProgress = 100;
    Serial.println("Wi-Fi update finished, restarting");
  });
  ArduinoOTA.onError([](ota_error_t e) {
    otaActive = false;
    Serial.printf("Wi-Fi update error %u\n", e);
  });
  ArduinoOTA.begin();  // also makes the device reachable as wall-display.local

  // Settings page and firmware-file upload page
  httpUpdater.setup(&server, "/update", WEB_USER, ADMIN_PASSWORD);
  server.on("/", HTTP_GET, handleRoot);
  server.on("/save", HTTP_POST, handleSave);
  server.on("/restart", HTTP_POST, handleRestart);
  server.begin();
  MDNS.addService("http", "tcp", 80);

  xTaskCreatePinnedToCore(webTask, "web", 12288, nullptr, 1, nullptr, 0);
  Serial.printf("Settings page: http://%s.local  (or http://%s)\n",
                HOSTNAME, WiFi.localIP().toString().c_str());
}