# Connecting your Google Calendar and Tasks

`calendar_pi` reads your primary Google Calendar (read-only) via the
Calendar API for the top pane, and your default Google Tasks list
(read-only) via the Tasks API for the to-do pane. Both ride on the same
OAuth client and refresh token. Setup has two parts: a one-time Google
Cloud Console configuration (any browser), and a one-time authorization
step run on a machine with a real browser (your laptop/desktop — **not**
the Pi, which has no browser and no keyboard/monitor by design).

If you already set this up for Calendar only before the to-do pane
existed, see **"Already authorized before the to-do pane existed?"** in
Troubleshooting below — you'll need to re-authorize once to pick up the
Tasks scope.

## Why this isn't a "code appears on the Pi's screen" flow

An earlier version of this doc described a device-authorization flow
(enter a short code shown on the Pi into a URL on your phone). That
doesn't work: Google's OAuth device-authorization endpoint only supports
a small fixed allowlist of scopes (`openid`/`email`/`profile`, two narrow
Drive scopes, two YouTube scopes) and Calendar API scopes aren't on it —
confirmed against
[Google's own docs](https://developers.google.com/identity/protocols/oauth2/limited-input-device).
No amount of consent-screen configuration fixes this; it's a hard
platform restriction. So instead, authorization happens once, out of
band, using the standard browser-based Authorization Code flow via a
small script — `tools/authorize_gcal.py` — that you run on your own
computer. It hands you a refresh token, which you copy to the Pi. From
then on, the Pi only ever does ordinary token *refreshes* (not the
restricted device flow), which works fine for any scope.

## 1. Create a Google Cloud project

If you don't already have one: go to the
[Google Cloud Console](https://console.cloud.google.com/), create a new
project (any name), and select it.

## 2. Enable the Calendar and Tasks APIs

In the console, go to **APIs & Services → Library**, search for
**"Google Calendar API"**, enable it for your project, then repeat for
**"Google Tasks API"** (this powers the to-do pane).

## 3. Configure the OAuth consent screen

Go to **APIs & Services → OAuth consent screen**.

- User type: **External** (fine for personal use — you don't need Google
  Workspace).
- **Add both of these scopes — required, not optional:**
  `https://www.googleapis.com/auth/calendar.events.readonly` and
  `https://www.googleapis.com/auth/tasks.readonly`. Go to the
  **Data Access** (or "Scopes") section, click **Add or Remove Scopes**,
  and either find them by searching "Google Calendar API" / "Google Tasks
  API" in the filtered list, or paste the full scope URLs into the
  "manually add scopes" box if they don't show up.
- Add your own Google account as a **test user** if the screen offers it.

**Set publishing status to "In production," not "Testing."** This now
lives under the **Audience** tab in the left sidebar — click **Publish
App** there, then **Confirm**. This is the single most common way this
integration silently breaks later: refresh tokens issued while still in
**Testing** mode expire after **7 days**, so sync would work fine, run
for a week, then quietly die with no error beyond the "OFFLINE" footer.
"In production" issues long-lived refresh tokens instead. You'll see an
"unverified app" warning the first time you approve access — expected and
safe to click through for a personal, single-user tool like this (full
Google verification is only relevant for apps used by people other than
you).

## 4. Create an OAuth client

Go to **APIs & Services → Credentials → Create Credentials → OAuth client
ID**.

- Application type: **Desktop app** — this is what supports the
  browser-based Authorization Code flow `tools/authorize_gcal.py` uses.
  (If you previously created a "TV and Limited Input devices" client
  while following an older version of this doc, it won't work here —
  create a new Desktop app client instead.)
- Give it any name (e.g. "calendar_pi").

After creating it, note the **Client ID** and **Client Secret** shown.

## 5. Authorize, on your own computer

On your laptop/desktop (needs Python 3, no other dependencies), from a
checkout of this repo:
```sh
python3 tools/authorize_gcal.py --client-id YOUR_CLIENT_ID.apps.googleusercontent.com --client-secret YOUR_CLIENT_SECRET
```
It opens your browser to Google's consent screen (or prints a URL to open
by hand if it can't launch one). Sign in with the Google account whose
calendar you want to display and approve access. The script then prints
a **refresh token** and also saves it to `./gcal_refresh_token.txt`.

## 6. Configure the Pi

On the Pi, create `~/.config/calendar_pi/client.conf` with the **same**
client ID/secret from step 4:
```
client_id=YOUR_CLIENT_ID.apps.googleusercontent.com
client_secret=YOUR_CLIENT_SECRET
```
(The directory is created automatically the first time the app runs if it
doesn't exist yet — you can create it by hand too:
`mkdir -p ~/.config/calendar_pi`.)

Then copy the refresh token from step 5 onto the Pi as the token file
(mode `0600`):
```sh
scp gcal_refresh_token.txt your_username@<pi-address>:~/.config/calendar_pi/token
ssh your_username@<pi-address> chmod 600 ~/.config/calendar_pi/token
```

Neither file should ever be committed to the repo.

## 7. Run it

`./build/calendar_pi` (or restart the systemd service). With both files
in place, both the calendar and to-do panes go straight to live data — no
on-screen prompt needed, since authorization already happened on your
computer in step 5. If the token file is still missing, each pane shows
its own static "not yet authorized" message pointing back at this doc
rather than trying (and failing) to negotiate anything itself.

## Troubleshooting

- **"OFFLINE" footer that never clears**: check `journalctl` (if run under
  systemd) or the terminal output (if run interactively) for
  `gcal_sync: token refresh failed` or `gcal_sync: events fetch failed` —
  usually a network issue, or a refresh token that's expired (see the
  7-day Testing-mode gotcha above) or been revoked.
- **Footer says "GOOGLE CALENDAR NOT CONFIGURED"**: `client.conf` is
  missing or malformed on the Pi — re-check step 6.
- **Screen says "NOT YET AUTHORIZED"**: the token file isn't in place yet
  — re-check step 6's `scp`/`chmod`, or redo step 5.
- **Want to re-authorize from scratch** (e.g. switching Google accounts):
  delete `~/.config/calendar_pi/token` on the Pi, rerun
  `tools/authorize_gcal.py` on your computer, and copy the new token over
  (step 6).
- **Already authorized before the to-do pane existed?** Your existing
  token was only ever scoped for Calendar, so the to-do pane will show
  "TO-DO NOT YET AUTHORIZED" even though the calendar pane works fine.
  Fix: delete `~/.config/calendar_pi/token` on the Pi, rerun
  `tools/authorize_gcal.py` (it now requests both scopes), and copy the
  new token over (step 6) — the calendar pane keeps working throughout.
