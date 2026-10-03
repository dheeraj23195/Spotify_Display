#!/bin/sh
# Rebuilds data/cert/x509_crt_bundle.bin (the root CAs the display trusts for HTTPS)
# from Mozilla's list as published by curl.se, then checks it against Spotify's servers.
# Run from anywhere. Needs python3 and internet. Flash the result with a normal build.
set -e
root="$(cd "$(dirname "$0")/.." && pwd)"
tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT

curl -sSfL -o "$tmp/cacert.pem" https://curl.se/ca/cacert.pem
curl -sSfL -o "$tmp/cacert.pem.sha256" https://curl.se/ca/cacert.pem.sha256
(cd "$tmp" && shasum -a 256 -c cacert.pem.sha256)

python3 -m venv "$tmp/venv"
"$tmp/venv/bin/pip" -q install cryptography
# gen_crt_bundle.py is Espressif's script (ESP-IDF v4.4.7); it writes x509_crt_bundle to the current folder
(cd "$tmp" && "$tmp/venv/bin/python" "$root/tools/gen_crt_bundle.py" -q -i cacert.pem 2>/dev/null)

mkdir -p "$root/data/cert"
cp "$tmp/x509_crt_bundle" "$root/data/cert/x509_crt_bundle.bin"
"$tmp/venv/bin/python" "$root/tools/check_cert_bundle.py" "$root/data/cert/x509_crt_bundle.bin" 2>/dev/null
