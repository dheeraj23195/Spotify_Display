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
# temperature: normal, single digit, negative, three characters
r w36  --hour 2 --min 15 --temp 36 --seconds 3
r w5   --hour 9 --min 7  --temp 5  --seconds 3
r wm2  --hour 2 --min 15 --temp -2 --seconds 3
r wm12 --hour 10 --min 5 --temp -12 --seconds 3

# every expression, held for 3 s each (0 neutral, 1 happy, 2 surprised, 3 smug, 4 disgust,
# 5 scared, 6 asleep, 7 heart eyes, 8 wink, 9 curious), with weather showing
r moods --hour 14 --min 15 --temp 28 --cycle 3 --seconds 30
for m in 0 1 2 3 4 5 6 7 8 9; do r m$m --hour 14 --min 15 --temp 31 --mood $m --seconds 3; done

python3 -m venv "$tmp/venv"
"$tmp/venv/bin/pip" -q install pillow numpy 2>/dev/null
OUT="$out" TMP="$tmp" "$tmp/venv/bin/python" "$root/tools/preview_clock.py"
echo "Wrote $out/sheet.png, $out/moods.png and the GIFs day.gif / moods.gif"
