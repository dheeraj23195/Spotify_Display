#!/bin/sh
# Renders the idle clock face on this computer: no display needed.
#   tools/preview_clock.sh              -> preview/sheet.png (stills), moods.png (every expression), day.gif and moods.gif (animation)
# Needs a C++ compiler and python3 (installs pillow + numpy into a temporary venv).
set -e
root="$(cd "$(dirname "$0")/.." && pwd)"
out="${1:-$root/preview}"
tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT
mkdir -p "$out"

c++ -std=gnu++11 -O2 -Wall -o "$tmp/pc" "$root/tools/preview_clock.cpp" "$root/src/clockface.cpp"
r() { name="$1"; shift; "$tmp/pc" "$@" --out "$tmp/$name.bin"; }

r day     --hour 12 --min 45 --seconds 8 --minchange 4
r morning --hour 9  --min 7  --seconds 6
r night   --hour 2  --min 10 --seconds 6
r notime  --notime 1         --seconds 3
# weather: kinds 0..6 = clear, partly cloudy, cloudy, fog, rain, snow, storm
r w0 --hour 14 --min 15 --weather 0 --day 1 --temp 36 --seconds 8
r w1 --hour 14 --min 15 --weather 1 --day 1 --temp 28 --seconds 8
r w2 --hour 14 --min 15 --weather 2 --day 1 --temp 24 --seconds 8
r w3 --hour 14 --min 15 --weather 3 --day 1 --temp 12 --seconds 8
r w4 --hour 14 --min 15 --weather 4 --day 1 --temp 19 --seconds 8
r w5 --hour 14 --min 15 --weather 5 --day 1 --temp -2 --seconds 8
r w6 --hour 14 --min 15 --weather 6 --day 1 --temp 22 --seconds 8
r n0 --hour 22 --min 15 --weather 0 --day 0 --temp 18 --seconds 8
r n1 --hour 22 --min 15 --weather 1 --day 0 --temp 18 --seconds 8

# every expression, held for 3 s each (0 neutral, 1 happy, 2 surprised, 3 smug, 4 disgust,
# 5 scared, 6 asleep, 7 heart eyes, 8 wink, 9 curious), with weather showing
r moods --hour 14 --min 15 --weather 1 --day 1 --temp 28 --cycle 3 --seconds 30
for m in 0 1 2 3 4 5 6 7 8 9; do r m$m --hour 14 --min 15 --weather 0 --day 1 --temp 31 --mood $m --seconds 3; done

python3 -m venv "$tmp/venv"
"$tmp/venv/bin/pip" -q install pillow numpy 2>/dev/null
OUT="$out" TMP="$tmp" "$tmp/venv/bin/python" "$root/tools/preview_clock.py"
echo "Wrote $out/sheet.png, $out/moods.png and the GIFs day.gif / moods.gif"
