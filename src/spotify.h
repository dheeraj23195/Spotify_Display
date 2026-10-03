#pragma once
#include <Arduino.h>

struct NowPlaying {
  bool active = false;     // something is loaded in the player
  bool isPlaying = false;  // playing (true) or paused (false)
  String trackId;
  String title;
  String artist;
  String artUrl;           // smallest album image (Spotify's 64x64)
  uint32_t progressMs = 0;
  uint32_t durationMs = 0;
};

enum class SpotifyResult { Ok, NothingPlaying, AuthError, RateLimited, NetworkError, NoClock };

void spotifyBegin();
void spotifyResetConnection();  // drop the kept-open HTTPS connection; the next poll reconnects
SpotifyResult spotifyGetNowPlaying(NowPlaying &np);
struct SpotifyStats {
  uint32_t polls = 0;     // requests made since start-up
  uint32_t failures = 0;  // requests that got no answer
  int lastCode = 0;       // last HTTP result (200 = OK)
  uint32_t lastMs = 0;    // how long the last request took
  uint32_t totalMs = 0;   // for the average
  uint32_t worstMs = 0;   // slowest request
};
extern SpotifyStats spotifyStats;