#!/usr/bin/env python3
"""One-time Google Calendar authorization helper for calendar_pi.

Run this on a machine with a real browser (your laptop/desktop, NOT the
Pi) to obtain a refresh token, then copy that token to the Pi at
~/.config/calendar_pi/token (mode 0600). See docs/GOOGLE_CALENDAR_SETUP.md
for the full walkthrough, including why this exists: Google's OAuth
device-authorization flow (the "enter this code on another device" UX)
does not support Calendar API scopes at all, so the standard browser-based
Authorization Code flow is used instead, just run manually and only once.

Usage:
    python3 tools/authorize_gcal.py --client-id ID --client-secret SECRET

Both --client-id/--client-secret come from a Google Cloud Console OAuth
client of type "Desktop app" (Credentials -> Create Credentials -> OAuth
client ID). If omitted, you'll be prompted for them interactively.

No third-party packages required -- standard library only.
"""
import argparse
import base64
import hashlib
import http.server
import json
import secrets
import sys
import urllib.parse
import urllib.request
import webbrowser

AUTH_URL = "https://accounts.google.com/o/oauth2/v2/auth"
TOKEN_URL = "https://oauth2.googleapis.com/token"
SCOPE = "https://www.googleapis.com/auth/calendar.events.readonly"


def make_pkce_pair():
    verifier = secrets.token_urlsafe(64)
    digest = hashlib.sha256(verifier.encode("ascii")).digest()
    challenge = base64.urlsafe_b64encode(digest).rstrip(b"=").decode("ascii")
    return verifier, challenge


class CallbackHandler(http.server.BaseHTTPRequestHandler):
    def do_GET(self):
        query = urllib.parse.urlparse(self.path).query
        params = urllib.parse.parse_qs(query)
        self.server.result = params  # type: ignore[attr-defined]

        self.send_response(200)
        self.send_header("Content-Type", "text/html")
        self.end_headers()
        if "code" in params:
            body = "<html><body><h2>calendar_pi: authorization received.</h2>You can close this tab and go back to your terminal.</body></html>"
        else:
            body = "<html><body><h2>calendar_pi: authorization failed.</h2>Check your terminal for details.</body></html>"
        self.wfile.write(body.encode("utf-8"))

    def log_message(self, fmt, *args):
        pass  # keep stdout clean; we do our own status printing


def run_local_callback_server():
    server = http.server.HTTPServer(("127.0.0.1", 0), CallbackHandler)
    server.result = None  # type: ignore[attr-defined]
    port = server.server_address[1]
    return server, port


def exchange_code_for_tokens(client_id, client_secret, code, redirect_uri, code_verifier):
    form = urllib.parse.urlencode({
        "client_id": client_id,
        "client_secret": client_secret,
        "code": code,
        "redirect_uri": redirect_uri,
        "grant_type": "authorization_code",
        "code_verifier": code_verifier,
    }).encode("ascii")

    req = urllib.request.Request(TOKEN_URL, data=form, method="POST")
    try:
        with urllib.request.urlopen(req, timeout=15) as resp:
            return json.loads(resp.read().decode("utf-8"))
    except urllib.error.HTTPError as e:
        return json.loads(e.read().decode("utf-8"))


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--client-id", help="OAuth client ID (Desktop app type)")
    parser.add_argument("--client-secret", help="OAuth client secret")
    args = parser.parse_args()

    client_id = args.client_id or input("Client ID: ").strip()
    client_secret = args.client_secret or input("Client secret: ").strip()
    if not client_id or not client_secret:
        print("client-id and client-secret are both required.", file=sys.stderr)
        sys.exit(1)

    server, port = run_local_callback_server()
    redirect_uri = f"http://127.0.0.1:{port}/"

    verifier, challenge = make_pkce_pair()
    state = secrets.token_urlsafe(16)

    auth_url = AUTH_URL + "?" + urllib.parse.urlencode({
        "client_id": client_id,
        "redirect_uri": redirect_uri,
        "response_type": "code",
        "scope": SCOPE,
        "access_type": "offline",
        "prompt": "consent",
        "code_challenge": challenge,
        "code_challenge_method": "S256",
        "state": state,
    })

    print("Opening your browser to authorize calendar_pi against your Google Calendar.")
    print("If it doesn't open automatically, visit this URL:\n")
    print(f"  {auth_url}\n")
    webbrowser.open(auth_url)

    print(f"Waiting for the browser redirect on {redirect_uri} ...")
    server.handle_request()  # blocks for exactly one request
    result = server.result

    if result is None or "code" not in result:
        error = (result or {}).get("error", ["unknown error"])[0]
        print(f"Authorization failed: {error}", file=sys.stderr)
        sys.exit(1)

    if result.get("state", [None])[0] != state:
        print("State mismatch -- possible interference, aborting.", file=sys.stderr)
        sys.exit(1)

    code = result["code"][0]
    tokens = exchange_code_for_tokens(client_id, client_secret, code, redirect_uri, verifier)

    if "refresh_token" not in tokens:
        print("No refresh_token in the response:", file=sys.stderr)
        print(json.dumps(tokens, indent=2), file=sys.stderr)
        sys.exit(1)

    refresh_token = tokens["refresh_token"]
    with open("gcal_refresh_token.txt", "w") as f:
        f.write(refresh_token + "\n")

    print("\nSuccess. Refresh token saved to ./gcal_refresh_token.txt and printed below:\n")
    print(refresh_token)
    print("\nNext step -- copy it to the Pi, e.g.:")
    print("  scp gcal_refresh_token.txt your_username@<pi-address>:~/.config/calendar_pi/token")
    print("  ssh your_username@<pi-address> chmod 600 ~/.config/calendar_pi/token")


if __name__ == "__main__":
    main()
