#!/usr/bin/env python3
"""One-time Spotify login: gets a refresh token and saves it into include/secrets.h."""
import base64, http.server, json, re, secrets, sys
import urllib.error, urllib.parse, urllib.request, webbrowser
from pathlib import Path

SECRETS_FILE = Path(__file__).resolve().parent.parent / "include" / "secrets.h"
REDIRECT_URI = "http://127.0.0.1:8888/callback"
SCOPES = "user-read-currently-playing user-read-playback-state"


def read_define(text, name):
    m = re.search(rf'#define\s+{name}\s+"([^"]*)"', text)
    return m.group(1) if m else None


text = SECRETS_FILE.read_text()
client_id = read_define(text, "SPOTIFY_CLIENT_ID")
client_secret = read_define(text, "SPOTIFY_CLIENT_SECRET")
if not client_id or not client_secret or "paste" in client_id:
    sys.exit("Put SPOTIFY_CLIENT_ID and SPOTIFY_CLIENT_SECRET in include/secrets.h first.")

state = secrets.token_urlsafe(16)
result = {}


class Handler(http.server.BaseHTTPRequestHandler):
    def do_GET(self):
        url = urllib.parse.urlparse(self.path)
        if url.path != "/callback":
            self.send_response(404)
            self.end_headers()
            return
        q = urllib.parse.parse_qs(url.query)
        if q.get("state", [None])[0] != state:
            result["error"] = "state mismatch"
        elif "error" in q:
            result["error"] = q["error"][0]
        else:
            result["code"] = q["code"][0]
        self.send_response(200)
        self.send_header("Content-Type", "text/html")
        self.end_headers()
        self.wfile.write(b"<h2>Done. Close this tab and go back to the terminal.</h2>")

    def log_message(self, *args):
        pass


server = http.server.HTTPServer(("127.0.0.1", 8888), Handler)
auth_url = "https://accounts.spotify.com/authorize?" + urllib.parse.urlencode({
    "client_id": client_id,
    "response_type": "code",
    "redirect_uri": REDIRECT_URI,
    "scope": SCOPES,
    "state": state,
})
print("Opening your browser for Spotify login...")
print("If it doesn't open, visit this link:\n" + auth_url)
webbrowser.open(auth_url)
while not result:
    server.handle_request()
server.server_close()
if "error" in result:
    sys.exit("Login failed: " + result["error"])

# Exchange the one-time code for tokens
basic = base64.b64encode(f"{client_id}:{client_secret}".encode()).decode()
body = urllib.parse.urlencode({
    "grant_type": "authorization_code",
    "code": result["code"],
    "redirect_uri": REDIRECT_URI,
}).encode()
req = urllib.request.Request(
    "https://accounts.spotify.com/api/token", data=body,
    headers={"Authorization": "Basic " + basic,
             "Content-Type": "application/x-www-form-urlencoded"})
try:
    tokens = json.load(urllib.request.urlopen(req))
except urllib.error.HTTPError as e:
    sys.exit(f"Token request failed: {e.code} {e.read().decode()}")

# Save the refresh token into secrets.h
line = f'#define SPOTIFY_REFRESH_TOKEN "{tokens["refresh_token"]}"'
if read_define(text, "SPOTIFY_REFRESH_TOKEN") is not None:
    text = re.sub(r'#define\s+SPOTIFY_REFRESH_TOKEN\s+"[^"]*"', lambda m: line, text)
else:
    text = text.rstrip("\n") + "\n" + line + "\n"
SECRETS_FILE.write_text(text)
print("Saved refresh token to include/secrets.h")

# Prove it works: ask Spotify what's playing
req = urllib.request.Request(
    "https://api.spotify.com/v1/me/player/currently-playing",
    headers={"Authorization": "Bearer " + tokens["access_token"]})
try:
    with urllib.request.urlopen(req) as r:
        if r.status == 204:
            print("Login works! Nothing is playing right now.")
        else:
            item = json.load(r).get("item") or {}
            artists = ", ".join(a["name"] for a in item.get("artists", []))
            print(f"Login works! Now playing: {item.get('name')} - {artists}")
            for img in item.get("album", {}).get("images", []):
                print(f"  album art available: {img['width']}x{img['height']}")
except urllib.error.HTTPError as e:
    print(f"Token saved, but the test request failed: {e.code} {e.read().decode()}")