#include "spotify.h"
#include <WiFiClientSecure.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <Preferences.h>
#include "mbedtls/base64.h"
#include "secrets.h"
#include "tls.h"
#include "timesync.h"

static String accessToken;
static unsigned long tokenObtainedAt = 0;
static unsigned long tokenLifetimeMs = 0;
static String refreshToken;
static Preferences prefs;
SpotifyStats spotifyStats;

// One long-lived connection to api.spotify.com, reused between polls.
// Skipping a fresh secure handshake on every poll saves a lot of time.
static WiFiClientSecure apiClient;
static HTTPClient apiHttp;

static String base64Encode(const String &in) {
  unsigned char out[256];
  size_t outLen = 0;
  mbedtls_base64_encode(out, sizeof(out), &outLen,
                        (const unsigned char *)in.c_str(), in.length());
  return String((char *)out);
}

void spotifyBegin() {
  refreshToken = SPOTIFY_REFRESH_TOKEN;
  prefs.begin("spotify", false);
  // If Spotify rotated our refresh token earlier, use the saved one,
  // unless secrets.h has a newer token since then.
  if (prefs.getString("seed", "") == SPOTIFY_REFRESH_TOKEN) {
    String saved = prefs.getString("refresh", "");
    if (saved.length()) refreshToken = saved;
  } else {
    prefs.putString("seed", SPOTIFY_REFRESH_TOKEN);
    prefs.remove("refresh");
  }

  tlsConfigure(apiClient);
  apiHttp.setReuse(true);   // keep the connection open between requests
  apiHttp.setTimeout(5000);
}

void spotifyResetConnection() {
  apiClient.stop();
}

static bool refreshAccessToken() {
  WiFiClientSecure client;
  tlsConfigure(client);
  HTTPClient http;
  http.useHTTP10(true);
  if (!http.begin(client, "https://accounts.spotify.com/api/token")) return false;
  http.addHeader("Authorization",
                 "Basic " + base64Encode(String(SPOTIFY_CLIENT_ID) + ":" + SPOTIFY_CLIENT_SECRET));
  http.addHeader("Content-Type", "application/x-www-form-urlencoded");
  int code = http.POST("grant_type=refresh_token&refresh_token=" + refreshToken);
  if (code != 200) {
    Serial.printf("Token refresh failed: HTTP %d\n", code);
    Serial.println(http.getString());
    http.end();
    return false;
  }
  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, http.getStream());
  http.end();
  if (err) {
    Serial.printf("Token JSON error: %s\n", err.c_str());
    return false;
  }
  accessToken = doc["access_token"] | "";
  int expiresIn = doc["expires_in"] | 3600;
  tokenObtainedAt = millis();
  tokenLifetimeMs = (expiresIn - 60) * 1000UL;  // refresh a minute early
  if (doc["refresh_token"].is<const char *>()) {
    refreshToken = doc["refresh_token"].as<String>();
    prefs.putString("refresh", refreshToken);
    Serial.println("Spotify issued a new refresh token; saved it");
  }
  return accessToken.length() > 0;
}

SpotifyResult spotifyGetNowPlaying(NowPlaying &np) {
  if (!timeIsSet()) return SpotifyResult::NoClock;  // certificates can't be checked without the date

  if (accessToken.isEmpty() || millis() - tokenObtainedAt > tokenLifetimeMs) {
    if (!refreshAccessToken()) return SpotifyResult::AuthError;
  }

  unsigned long t0 = millis();
  if (!apiHttp.begin(apiClient, "https://api.spotify.com/v1/me/player/currently-playing")) {
    apiClient.stop();
    return SpotifyResult::NetworkError;
  }
  apiHttp.addHeader("Authorization", "Bearer " + accessToken);
  int code = apiHttp.GET();
  String body;
  if (code > 0 && code != 204) body = apiHttp.getString();  // read fully so the connection can be reused
  apiHttp.end();  // with reuse on, this keeps the connection open
  uint32_t took = millis() - t0;
  spotifyStats.polls++;
  spotifyStats.lastCode = code;
  spotifyStats.lastMs = took;
  spotifyStats.totalMs += took;
  if (took > spotifyStats.worstMs) spotifyStats.worstMs = took;
  if (code < 0) spotifyStats.failures++;

  if (code < 0) { apiClient.stop(); return SpotifyResult::NetworkError; }  // start fresh next time
  if (code == 204) { np.active = false; return SpotifyResult::NothingPlaying; }
  if (code == 401) { accessToken = ""; return SpotifyResult::AuthError; }
  if (code == 429) return SpotifyResult::RateLimited;
  if (code != 200) return SpotifyResult::NetworkError;

  // Keep only the fields we need (the full reply is large)
  JsonDocument filter;
  filter["is_playing"] = true;
  filter["progress_ms"] = true;
  filter["item"]["id"] = true;
  filter["item"]["name"] = true;
  filter["item"]["duration_ms"] = true;
  filter["item"]["artists"][0]["name"] = true;
  filter["item"]["album"]["images"][0]["url"] = true;
  filter["item"]["album"]["images"][0]["width"] = true;

  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, body, DeserializationOption::Filter(filter));
  if (err) {
    Serial.printf("Spotify JSON error: %s\n", err.c_str());
    return SpotifyResult::NetworkError;
  }

  JsonObject item = doc["item"];
  if (item.isNull()) { np.active = false; return SpotifyResult::NothingPlaying; }

  np.active = true;
  np.isPlaying = doc["is_playing"] | false;
  np.progressMs = doc["progress_ms"] | 0;
  np.trackId = item["id"] | "";
  np.title = item["name"] | "";
  np.durationMs = item["duration_ms"] | 0;

  np.artist = "";
  for (JsonObject a : item["artists"].as<JsonArray>()) {
    if (np.artist.length()) np.artist += ", ";
    np.artist += a["name"] | "";
  }

  // Pick Spotify's ~300x300 image (we downscale on the device for quality);
  // fall back to the largest image available
  const int ART_MIN_WIDTH = 256;
  np.artUrl = "";
  int best = 100000, largest = 0;
  String largestUrl;
  JsonArray images = item["album"]["images"].as<JsonArray>();
  for (JsonObject img : images) {
    int w = img["width"] | 0;
    if (w >= ART_MIN_WIDTH && w < best) { best = w; np.artUrl = img["url"] | ""; }
    if (w > largest) { largest = w; largestUrl = img["url"] | ""; }
  }
  if (np.artUrl.isEmpty()) np.artUrl = largestUrl;
  if (np.artUrl.isEmpty() && images.size() > 0) np.artUrl = images[0]["url"] | "";
  return SpotifyResult::Ok;
}