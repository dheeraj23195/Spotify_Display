#include "web.h"
#include <WiFi.h>
#include <WebServer.h>
#include <Update.h>
#include <ArduinoOTA.h>
#include <ESPmDNS.h>
#include <Preferences.h>
#include "settings.h"
#include "secrets.h"
#include "spotify.h"
#include "icon.h"

extern volatile bool otaActive;   // defined in main.cpp
extern volatile int otaProgress;
extern volatile bool isPlaying;

static const char *HOSTNAME = "wall-display";
static const char *WEB_USER = "admin";
static const int MAX_SESSIONS = 4;       // how many browsers can stay logged in
static const uint32_t SESSION_DAYS = 30; // how long a login lasts

static WebServer server(80);
static Preferences sessPrefs;
static String sessions[MAX_SESSIONS];    // saved so logins survive restarts
static int nextSlot = 0;
static bool uploadAuthorized = false;
static volatile bool uploadActive = false;
static String apiToken;                  // for Shortcuts and other scripts

// =====================================================================
//  Shared page header (styles + icon links)
// =====================================================================

static const char HEAD[] =
    "<!DOCTYPE html><html><head><meta charset='utf-8'>"
    "<meta name='viewport' content='width=device-width,initial-scale=1'>"
    "<meta name='apple-mobile-web-app-title' content='Wall Display'>"
    "<meta name='theme-color' content='#1a1625'>"
    "<link rel='icon' type='image/png' href='/icon.png'>"
    "<link rel='apple-touch-icon' href='/apple-touch-icon.png'>"
    "<title>Wall Display</title><style>"
    ":root{--bg:#1a1625;--card:#2a2338;--line:#3a3150;--text:#f1edf8;"
    "--muted:#a59bbb;--teal:#3fd8c2;--err:#ff7a90}"
    "*{box-sizing:border-box}"
    "body{margin:0 auto;max-width:440px;padding:20px 18px 40px;background:var(--bg);"
    "color:var(--text);font:16px/1.45 ui-rounded,'SF Pro Rounded',system-ui,sans-serif;"
    "font-variant-numeric:tabular-nums}"
    "header{display:flex;align-items:center;gap:12px;margin-bottom:8px}"
    "header img{width:44px;height:44px;border-radius:10px}"
    "h1{font-size:1.35em;margin:0}"
    "h2{font-size:.95em;font-weight:600;color:var(--muted);margin:28px 0 4px}"
    "label{display:flex;justify-content:space-between;margin-top:14px}"
    "output{color:var(--teal)}"
    "input[type=range]{width:100%;accent-color:var(--teal)}"
    "input[type=checkbox]{accent-color:var(--teal);width:20px;height:20px}"
    "input[type=text],input[type=password],input[type=file]{width:100%;margin-top:6px;"
    "padding:12px;border-radius:10px;border:1px solid var(--line);background:var(--card);"
    "color:var(--text);font:inherit}"
    "button,a.btn{display:block;width:100%;margin-top:12px;padding:13px;border:0;"
    "border-radius:10px;background:var(--card);color:var(--text);font:inherit;"
    "text-align:center;text-decoration:none;cursor:pointer}"
    "button.primary{background:var(--teal);color:#10231f;font-weight:600}"
    ":focus-visible{outline:2px solid var(--teal);outline-offset:2px}"
    ".status{margin:6px 0 0;line-height:1.7}"
    ".note{min-height:1.4em;margin-top:10px;color:var(--teal)}"
    ".err{color:var(--err)}"
    ".hint{margin:4px 0 0;color:var(--muted);font-size:.9em}"
    "</style></head><body>"
    "<header><img src='/icon.png' alt=''><h1>Wall Display</h1></header>";

// =====================================================================
//  Login sessions
// =====================================================================

static String sessionKey(int i) { return "s" + String(i); }

static void loadSessions() {
  sessPrefs.begin("web", false);
  for (int i = 0; i < MAX_SESSIONS; i++) {
    String k = sessionKey(i);
    sessions[i] = sessPrefs.isKey(k.c_str()) ? sessPrefs.getString(k.c_str()) : "";
  }
  nextSlot = sessPrefs.isKey("next") ? sessPrefs.getInt("next") % MAX_SESSIONS : 0;
}

static String newToken();

// The Shortcuts token is made on the first start and kept in flash
static void loadApiToken() {
  apiToken = sessPrefs.getString("apitok", "");
  if (apiToken.length() != 32) {
    apiToken = newToken();
    sessPrefs.putString("apitok", apiToken);
  }
}

static void saveSession(int i) {
  sessPrefs.putString(sessionKey(i).c_str(), sessions[i]);
  sessPrefs.putInt("next", nextSlot);
}

static String newToken() {
  char buf[33];
  for (int i = 0; i < 4; i++) sprintf(buf + i * 8, "%08x", (unsigned)esp_random());
  return String(buf);
}

static String cookieToken() {
  if (!server.hasHeader("Cookie")) return "";
  String c = server.header("Cookie");
  int i = c.indexOf("session=");
  if (i < 0) return "";
  i += 8;
  int end = c.indexOf(';', i);
  return c.substring(i, end < 0 ? c.length() : end);
}

static bool isLoggedIn() {
  String t = cookieToken();
  if (t.length() != 32) return false;
  for (int i = 0; i < MAX_SESSIONS; i++) {
    if (sessions[i] == t) return true;
  }
  return false;
}

static void redirect(const char *to) {
  server.sendHeader("Location", to);
  server.send(303);
}

// Pages: send the browser to the login form if needed
static bool requirePage() {
  if (isLoggedIn()) return true;
  redirect("/login");
  return false;
}

// Background requests from the page: just report "not logged in"
static bool requireApi() {
  if (isLoggedIn()) return true;
  server.send(401, "text/plain", "Login required");
  return false;
}

// =====================================================================
//  Login / logout
// =====================================================================

static void handleLoginPage() {
  if (isLoggedIn()) { redirect("/"); return; }
  String h = HEAD;
  h += F("<form method='POST' action='/login'>"
         "<label for='u'>Username</label>"
         "<input id='u' type='text' name='username' autocomplete='username' "
         "autocapitalize='none' autocorrect='off' required>"
         "<label for='p'>Password</label>"
         "<input id='p' type='password' name='password' autocomplete='current-password' required>"
         "<button class='primary' type='submit'>Log in</button></form>");
  if (server.hasArg("failed")) {
    h += F("<p class='note err'>Wrong username or password. Try again.</p>");
  }
  h += F("</body></html>");
  server.send(200, "text/html", h);
}

static void handleLogin() {
  if (server.arg("username") == WEB_USER && server.arg("password") == ADMIN_PASSWORD) {
    String t = newToken();
    int slot = nextSlot;
    sessions[slot] = t;
    nextSlot = (nextSlot + 1) % MAX_SESSIONS;
    saveSession(slot);
    server.sendHeader("Set-Cookie", "session=" + t + "; Path=/; Max-Age=" +
                                        String(SESSION_DAYS * 86400UL) +
                                        "; HttpOnly; SameSite=Strict");
    redirect("/");
  } else {
    delay(1000);  // slows down password guessing
    redirect("/login?failed=1");
  }
}

static void handleLogout() {
  String t = cookieToken();
  for (int i = 0; i < MAX_SESSIONS; i++) {
    if (sessions[i].length() && sessions[i] == t) {
      sessions[i] = "";
      saveSession(i);
    }
  }
  server.sendHeader("Set-Cookie", "session=; Path=/; Max-Age=0");
  redirect("/login");
}

// =====================================================================
//  Settings page
// =====================================================================

static String slider(const char *name, const char *label, float value,
                     float mn, float mx, float step) {
  char buf[420];
  snprintf(buf, sizeof(buf),
           "<label for='%s'>%s <output id='%s_o'>%g</output></label>"
           "<input type='range' id='%s' name='%s' min='%g' max='%g' step='%g' value='%g' "
           "oninput=\"document.getElementById('%s_o').value=this.value\">",
           name, label, name, value, name, name, mn, mx, step, value, name);
  return String(buf);
}

static String settingsPage() {
  String h = HEAD;
  h.reserve(8000);
  h += F("<form id='f'>");

  h += F("<h2>Display</h2>");
  h += "<label for='on'>Display on <input type='checkbox' id='on' name='displayOn'";
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
  h += F("</form><p class='note' id='st'></p>");

  {
    char buf[420];
    uint32_t up = millis() / 1000;
    uint32_t avg = spotifyStats.polls ? spotifyStats.totalMs / spotifyStats.polls : 0;
    snprintf(buf, sizeof(buf),
             "<h2>Status</h2><p class='status'>"
             "Wi-Fi signal: %d dBm<br>"
             "Spotify checks: %u (failed: %u)<br>"
             "Last: %u ms (HTTP %d)<br>"
             "Average: %u ms, slowest: %u ms<br>"
             "Running for: %uh %um</p>",
             WiFi.RSSI(), spotifyStats.polls, spotifyStats.failures,
             spotifyStats.lastMs, spotifyStats.lastCode,
             avg, spotifyStats.worstMs, up / 3600, (up / 60) % 60);
    h += buf;
  }

  h += F("<h2>Shortcuts</h2>"
         "<p class='hint'>Token for iOS Shortcuts and scripts. "
         "Anyone who has it can switch the display on and off.</p>"
         "<input type='text' id='tok' readonly value='");
  h += apiToken;
  h += F("'><button type='button' id='cp'>Copy token</button>"
         "<button type='button' id='nt'>Make a new token</button>");

  h += F("<h2>Maintenance</h2>"
         "<a class='btn' href='/update'>Upload firmware file</a>"
         "<button type='button' id='rs'>Restart display</button>"
         "<a class='btn' href='/logout'>Log out</a>"
         "<script>"
         "const f=document.getElementById('f'),st=document.getElementById('st');"
         "f.addEventListener('change',()=>{"
         "fetch('/save',{method:'POST',body:new URLSearchParams(new FormData(f))})"
         ".then(r=>{if(r.status==401){location='/login';return;}"
         "st.className='note'+(r.ok?'':' err');"
         "st.textContent=r.ok?'Saved':'Could not save. Check the display is on Wi-Fi.';})"
         ".catch(()=>{st.className='note err';"
         "st.textContent='Could not save. Check the display is on Wi-Fi.';});});"
         "const tk=document.getElementById('tok');"
         "document.getElementById('cp').onclick=()=>{"
         "tk.focus();tk.select();tk.setSelectionRange(0,99);let ok=false;"
         "try{ok=document.execCommand('copy');}catch(e){}"
         "st.className='note'+(ok?'':' err');"
         "st.textContent=ok?'Token copied':'Could not copy. Hold the token to copy it.';};"
         "document.getElementById('nt').onclick=()=>{"
         "if(confirm('Make a new token? Shortcuts using the old one stop working.'))"
         "fetch('/token/new',{method:'POST'}).then(r=>r.ok?r.text():Promise.reject())"
         ".then(t=>{tk.value=t;st.className='note';st.textContent='New token ready';})"
         ".catch(()=>{st.className='note err';st.textContent='Could not make a new token.';});};"
         "document.getElementById('rs').onclick=()=>{"
         "if(confirm('Restart the display?'))"
         "fetch('/restart',{method:'POST'}).then(()=>{st.className='note';"
         "st.textContent='Restarting. Reload this page in about 15 seconds.';});};"
         "</script></body></html>");
  return h;
}

static void handleRoot() {
  if (!requirePage()) return;
  server.send(200, "text/html", settingsPage());
}

static void handleSave() {
  if (!requireApi()) return;
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

static void handleNewToken() {
  if (!requireApi()) return;
  apiToken = newToken();
  sessPrefs.putString("apitok", apiToken);
  server.send(200, "text/plain", apiToken);
}

static void handleRestart() {
  if (!requireApi()) return;
  server.send(200, "text/plain", "Restarting");
  delay(500);
  ESP.restart();
}

// =====================================================================
//  Token API for Shortcuts (GET or POST, no login cookie needed)
//    /api/on  /api/off  /api/toggle  /api/status  /api/brightness?percent=40
//  The token goes in ?token=... or in an "Authorization: Bearer ..." header.
// =====================================================================

static bool requireToken() {
  String given = server.arg("token");
  if (given.isEmpty() && server.hasHeader("Authorization")) {
    String a = server.header("Authorization");
    if (a.startsWith("Bearer ")) given = a.substring(7);
  }
  given.trim();
  // Compare every character so the time taken doesn't reveal how much matched
  uint8_t diff = given.length() != apiToken.length();
  for (size_t i = 0; i < apiToken.length(); i++) {
    diff |= (i < given.length() ? given[i] : 0) ^ apiToken[i];
  }
  if (diff == 0) return true;
  delay(1000);  // slows down guessing
  server.send(401, "text/plain", "Bad token");
  return false;
}

static void sendApiStatus() {
  char buf[96];
  snprintf(buf, sizeof(buf),
           "{\"on\":%s,\"brightness\":%u,\"percent\":%u,\"playing\":%s}",
           settings.displayOn ? "true" : "false", settings.brightness,
           (settings.brightness * 100u + 127) / 255, isPlaying ? "true" : "false");
  server.send(200, "application/json", buf);
}

static void apiSetOn(bool on) {
  settings.displayOn = on;
  settingsSave();
  sendApiStatus();
}

static void handleApiOn()  { if (requireToken()) apiSetOn(true); }
static void handleApiOff() { if (requireToken()) apiSetOn(false); }
static void handleApiToggle() { if (requireToken()) apiSetOn(!settings.displayOn); }
static void handleApiStatus() { if (requireToken()) sendApiStatus(); }

static void handleApiBrightness() {
  if (!requireToken()) return;
  String v = server.arg("percent");
  if (v.isEmpty() || !isDigit(v[0])) {
    server.send(400, "text/plain", "Use ?percent=0-100");
    return;
  }
  float pct = constrain(v.toFloat(), 0.0f, 100.0f);
  settings.brightness = constrain((int)(pct * 2.55f + 0.5f), 5, 255);  // same floor as the slider
  settingsSave();
  sendApiStatus();
}

// =====================================================================
//  Firmware file upload (login required)
// =====================================================================

static void handleUpdatePage() {
  if (!requirePage()) return;
  String h = HEAD;
  h += F("<h2>Upload firmware</h2>"
         "<p>Choose the file <b>firmware.bin</b> from your project's "
         ".pio/build/esp32s3 folder. The display restarts when it's done.</p>"
         "<form method='POST' action='/update' enctype='multipart/form-data'>"
         "<input type='file' name='firmware' accept='.bin' required>"
         "<button class='primary' type='submit'>Upload and restart</button></form>"
         "<a class='btn' href='/'>Back to settings</a></body></html>");
  server.send(200, "text/html", h);
}

static void handleUpdateUpload() {
  HTTPUpload &up = server.upload();
  if (up.status == UPLOAD_FILE_START) {
    uploadAuthorized = isLoggedIn();
    if (!uploadAuthorized) return;
    uploadActive = true;
    Serial.printf("Firmware upload started: %s\n", up.filename.c_str());
    if (!Update.begin(UPDATE_SIZE_UNKNOWN)) Update.printError(Serial);
  } else if (up.status == UPLOAD_FILE_WRITE) {
    if (!uploadAuthorized) return;
    if (Update.write(up.buf, up.currentSize) != up.currentSize) Update.printError(Serial);
  } else if (up.status == UPLOAD_FILE_END) {
    uploadActive = false;
    if (!uploadAuthorized) return;
    if (Update.end(true)) {
      Serial.printf("Firmware upload finished: %u bytes\n", up.totalSize);
    } else {
      Update.printError(Serial);
    }
  } else if (up.status == UPLOAD_FILE_ABORTED) {
    uploadActive = false;
    if (uploadAuthorized) Update.abort();
  }
}

static void handleUpdateDone() {
  if (!uploadAuthorized) {
    server.send(401, "text/plain", "Login required");
    return;
  }
  bool ok = !Update.hasError();
  String h = HEAD;
  if (ok) {
    h += F("<h2>Update installed</h2><p>The display is restarting. "
           "Reload the settings page in about 15 seconds.</p></body></html>");
  } else {
    h += F("<h2>Update failed</h2><p class='err'>The file was not installed, and the "
           "display keeps running its current version. Check that you chose "
           "firmware.bin and try again.</p>"
           "<a class='btn' href='/update'>Try again</a></body></html>");
  }
  server.send(200, "text/html", h);
  if (ok) {
    delay(1000);
    ESP.restart();
  }
}

// =====================================================================
//  Icon (public, so the home-screen icon loads without logging in)
// =====================================================================

static void handleIcon() {
  server.sendHeader("Cache-Control", "max-age=86400");
  server.send_P(200, "image/png", (const char *)ICON_PNG, ICON_PNG_LEN);
}

// =====================================================================
//  Start-up
// =====================================================================

static void webTask(void *) {
  for (;;) {
    server.handleClient();
    ArduinoOTA.handle();
    vTaskDelay(2);
  }
}

bool webUploadActive() { return uploadActive; }

void webBegin() {
  loadSessions();
  loadApiToken();

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

  const char *headerKeys[] = {"Cookie", "Authorization"};
  server.collectHeaders(headerKeys, 2);

  server.on("/", HTTP_GET, handleRoot);
  server.on("/login", HTTP_GET, handleLoginPage);
  server.on("/login", HTTP_POST, handleLogin);
  server.on("/logout", HTTP_GET, handleLogout);
  server.on("/save", HTTP_POST, handleSave);
  server.on("/restart", HTTP_POST, handleRestart);
  server.on("/token/new", HTTP_POST, handleNewToken);
  server.on("/api/on", HTTP_ANY, handleApiOn);
  server.on("/api/off", HTTP_ANY, handleApiOff);
  server.on("/api/toggle", HTTP_ANY, handleApiToggle);
  server.on("/api/status", HTTP_ANY, handleApiStatus);
  server.on("/api/brightness", HTTP_ANY, handleApiBrightness);
  server.on("/update", HTTP_GET, handleUpdatePage);
  server.on("/update", HTTP_POST, handleUpdateDone, handleUpdateUpload);
  server.on("/icon.png", HTTP_GET, handleIcon);
  server.on("/apple-touch-icon.png", HTTP_GET, handleIcon);
  server.on("/favicon.ico", HTTP_GET, handleIcon);
  server.begin();
  MDNS.addService("http", "tcp", 80);

  xTaskCreatePinnedToCore(webTask, "web", 12288, nullptr, 1, nullptr, 0);
  Serial.printf("Settings page: http://%s.local  (or http://%s)\n",
                HOSTNAME, WiFi.localIP().toString().c_str());
}
