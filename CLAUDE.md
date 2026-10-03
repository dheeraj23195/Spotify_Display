# Spotify wall display (ESP32-S3 + 64x64 HUB75 LED panel)

Permanent wall-mounted display. Shows the album art of whatever is playing on Spotify, with slow
camera-orbit / pause / crossfade effects. When nothing is playing (or Spotify has been paused for a
while) it shows an animated yellow "Orbi" face (like the Las Vegas Sphere emoji) with the time and
the temperature. Settings, status and firmware upload are on a web page served by the ESP32 itself
(http://wall-display.local, home Wi-Fi only). The same page shows a token for Siri Shortcuts.

Repo: https://github.com/dheeraj23195/Spotify_Display

## Hardware (all purchased and verified working)

- Panel: Waveshare RGB-Matrix-P2.5-64x64 (64x64, 2.5 mm pitch, HUB75, 1/32 scan, FM6124 drivers, 5V/4A).
- Controller: EdgeHax ESP32-S3-WROOM-1 N16R8 (16 MB flash, 8 MB OPI PSRAM).
- Power: one 5V 4A supply feeds the panel. The ESP32 is powered from the panel's 5V via two jumper
  wires (red -> 5V pin, black -> G pin next to it). One cable to the wall powers everything.
- The board needs a USB-A-to-USB-C cable for USB power (C-to-C gives it no power).

### SAFETY RULE (important)
When the ESP32 is powered from the panel's 5V pin, NEVER plug USB into the Mac at the same time.
Pull the red 5V jumper wire first. Always remind the user before any USB upload or serial session.
Once the display is on the wall, prefer Wi-Fi (OTA) updates; USB means taking it down.

## Verified GPIO map (do not change)

R1=1, G1=2, B1=4, R2=5, G2=6, B2=7, A=8, B=9, C=10, D=11, E=12, CLK=13, LAT=14, OE=38.
Data goes to the panel's JIN connector. GND to the G pins.

## Display settings that matter (all verified)

- `cfg.driver = HUB75_I2S_CFG::FM6126A` (works with this panel's FM6124 chips)
- `cfg.clkphase = false` (otherwise the image shifts one pixel)
- `PANEL_ROTATION = 1`
- `cfg.double_buff = true` (prevents tearing)

### Wi-Fi interference fix (CRITICAL, do not remove)
The HUB75 signals interfered with the ESP32's Wi-Fi. Fixed by setting the drive strength to the
lowest level on all 14 signal pins right after `display->begin()`:
`gpio_set_drive_capability((gpio_num_t)p, GPIO_DRIVE_CAP_0)` for pins 1,2,4,5,6,7,8,9,10,11,12,13,14,38.
With the fix, signal is steady around -61 to -64 dBm with no failures. Keep the jumper bundle
away from the ESP32's antenna end.

## Build and upload (PlatformIO, Arduino framework)

- Default env `esp32s3`: USB upload. See `platformio.ini`.
- OTA env (defined in `ota.ini`, gitignored, contains the admin password):
  `pio run -e ota -t upload` uploads over Wi-Fi to wall-display.local.
- The settings site also has a browser firmware upload at `/update`.
- `include/secrets.h` (Wi-Fi, Spotify credentials, admin password, WEATHER_LAT/WEATHER_LON) and
  `ota.ini` are gitignored. NEVER commit them or print their contents. When adding a new secret,
  add a placeholder to `include/secrets.example.h` too. Without WEATHER_LAT/LON the temperature is
  just left out.
- HTTPS: never use `setInsecure()`. All TLS clients go through `tlsConfigure()` and verify against
  the root CA bundle embedded from `data/cert/x509_crt_bundle.bin`. Certificates have dates, so no
  HTTPS request is made until NTP has set the clock. Rebuild the bundle with
  `tools/update_cert_bundle.sh` (it also checks Spotify's and Open-Meteo's chains); any new HTTPS
  host must be added to `tools/check_cert_bundle.py`.
- Design previews without the display: `tools/preview_clock.sh` renders the clock face to PNG/GIF
  (LED-dot look) into `preview/` (gitignored). `clockface.cpp` must stay free of Arduino headers
  so this keeps working.
- Do not break the OTA path. A firmware that cannot be updated over Wi-Fi means taking the display
  off the wall. Anything that can reboot or hang the device (watchdogs, TLS changes) must leave
  OTA reachable.

## Source layout

- `src/main.cpp`: display setup, effects, network, Spotify polling loop. Dual-core:
  core 0 = network, core 1 = rendering at 30 fps. Never block the render core on network calls.
- `src/spotify.h/.cpp`: Spotify API, token refresh (refresh token kept in NVS via Preferences),
  persistent HTTPS connections, stats.
- `src/settings.h/.cpp`: persistent settings (Preferences).
- `src/web.h/.cpp`: settings page, login form with 30-day sessions, OTA, status page,
  `/icon.png` (LED-dot music note for the browser tab and iPhone home screen), and the
  token-protected Shortcuts API (`/api/on|off|toggle|status|brightness?value=5-255` (or `?percent=0-100`); token via
  `?token=` or `Authorization: Bearer`; generated on first boot, kept in flash, shown on the page).
- `src/clockface.h/.cpp`: the idle face. One entry point `clockDraw(rgb, info, ms)`, drawn live
  every frame by the render loop. Moods (happy, surprised, smug, disgust, scared, asleep, heart
  eyes, wink, curious) are a table at the top of the animation section; look constants are at the
  top of the file. Sleepy lids / more sleeping from 23:00 to 06:00.
- `src/weather.h/.cpp`: current temperature plus today's sunrise/sunset from Open-Meteo (no API
  key), every 15 min; `weatherNight()` tells the face whether it is night.
- `src/timesync.h/.cpp`: NTP, time zone Asia/Kolkata (IST-5:30, no DST).
- `src/tls.h/.cpp`: `tlsConfigure()`, see HTTPS rule above.
- `src/health.h/.cpp`: restart after 5 min without a real Spotify answer; never during an update;
  at most 3 restarts in a row (reset by a good answer or a power cycle) so it cannot boot-loop.
- `tools/`: `get_spotify_token.py` (one-time OAuth login; saves the refresh token to secrets.h),
  `update_cert_bundle.sh`, `check_cert_bundle.py`, `gen_crt_bundle.py` (Espressif's script),
  `preview_clock.sh/.cpp/.py`.

## Behaviour already implemented

Album art: 300x300 JPEG downscaled to 64x64 with averaging, full 24-bit colour. Crossfade between
songs. Camera-orbit effect (about +/-6 degrees, freezes on pause). Pause look: zoom out to a
black border and dim to about 45%. Spotify polled every 1.5 s (`GET /v1/me/player/currently-playing`).
Settings on the web page: brightness, display on/off, orbit strength and period, pause dim and
border, how long a pause lasts before the clock shows (10-600 s, default 60), crossfade time.

Idle screen: after the chosen pause time, or when Spotify reports nothing playing, the face and
clock replace the album art (crossfade); resuming reloads the cover. The clock is 12-hour with
AM/PM. The temperature sits above AM/PM, both on one shared left edge, with the same number of
blank rows above and below the temperature. The face glances down and smiles when the minute
changes. Self-recovery: Wi-Fi rejoin, fresh HTTPS connection after repeated failures, restart
rule above. Serial output is kept to boot lines and errors.

## Spotify API notes (2026)

- Developer Mode apps need a Premium owner account and are limited to a handful of users.
- Spotify Canvas (looping video art) is not available through the official API.
- Auth is the refresh-token flow.
- Weather: Open-Meteo `api.open-meteo.com` (free, non-commercial, no key), temperature only.

## Decisions already made

- No automatic night dimming or schedule. The user dims or turns the display off manually from the
  web page (and wants a Siri/Shortcuts-friendly way to do that).
- The face colours are accurate. Photos of the panel look greenish only because of the phone camera;
  do not "correct" them.
- The face only appears on the idle screen (never over album art), so no music-reactive face
  expressions.
- Not wanted: progress line, title scroll, liked-song heart.
- The weather is the temperature only (no icons). Day/night is shown by the face itself: the
  yellow dome turns into a cratered grey-blue moon from the real sunset to the real sunrise
  (fetched daily from Open-Meteo, 18:00-06:00 until known), fading over about 6 s.
- Display speed comes first. Do not trade it for running other projects on the board; in
  particular keep the persistent HTTPS connections (Spotify API and cover image server) open for
  fast cover loads, even though that costs about 40 KB of internal RAM while music plays.
- The frame, diffuser and mounting hardware are designed outside this repo.

## Working with this user

- Strong with software, a beginner with electronics. Explain anything about wiring, power,
  connectors or polarity from scratch. If something about the hardware is ambiguous, ask for a
  photo instead of guessing.
- Before suggesting anything that changes wiring or power, say so explicitly.
- Prefer small commits (one feature each) on a branch, and say what to test on the device.
  The user flashes and tests on the real display; do not claim a hardware behaviour works until
  they confirm it.
- Remove debug noise rather than adding more. Keep serial output short.

## Measured performance (Performance block on the settings page)

- Clock/moon face: render loop 21.2 ms avg, 22.7 ms max of the 33.3 ms frame (63% busy);
  free internal RAM 85 KB (lowest 73 KB).
- Album art: render loop 14.1 ms avg, 15.8 ms max (42% busy); free internal RAM 43 KB (lowest
  32 KB, largest block 31 KB); PSRAM about 7.8 MB free either way.
- Stack headroom: network task about 10.8 KB left of 16 KB, web task about 8.9 KB of 12 KB.
- Internal RAM is the tight resource, not CPU. A new HTTPS connection needs about 40 KB at once.
