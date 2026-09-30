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

enum class SpotifyResult { Ok, NothingPlaying, AuthError, RateLimited, NetworkError };

void spotifyBegin();
SpotifyResult spotifyGetNowPlaying(NowPlaying &np);