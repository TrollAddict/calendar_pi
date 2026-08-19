# Connecting your Google Calendar

`calendar_pi` reads your primary Google Calendar (read-only) via the
Calendar API. This is a one-time setup in Google Cloud Console, done from
any browser — none of it happens on the Pi. Do this before first boot, or
before running the built binary for the first time.

## 1. Create a Google Cloud project

If you don't already have one: go to the
[Google Cloud Console](https://console.cloud.google.com/), create a new
project (any name), and select it.

## 2. Enable the Calendar API

In the console, go to **APIs & Services → Library**, search for
**"Google Calendar API"**, and enable it for your project.

## 3. Configure the OAuth consent screen

Go to **APIs & Services → OAuth consent screen**.

- User type: **External** (fine for personal use — you don't need Google
  Workspace).
- Add the scope `.../auth/calendar.events.readonly` if prompted.
- Add your own Google account as a **test user** if the screen offers it.

**Important: set publishing status to "In production," not "Testing."**
This is the single most common way this integration silently breaks:
refresh tokens issued while the consent screen is in **Testing** mode
expire after **7 days**, so the Pi would authenticate fine, run for a
week, then quietly stop syncing with no on-screen error beyond the
"OFFLINE" footer. "In production" issues long-lived refresh tokens
instead. You'll see an "unverified app" warning the first time you
approve access — that's expected and safe to click through for a personal,
single-user tool like this (full Google verification is for apps used by
other people, not relevant here).

## 4. Create an OAuth client

Go to **APIs & Services → Credentials → Create Credentials → OAuth client
ID**.

- Application type: **TV and Limited Input devices** — this is Google's
  documented client type for the device-authorization flow this app uses.
  (A **Desktop app** client type is also known to work against the same
  endpoints, if you already have one of those instead.)
- Give it any name (e.g. "calendar_pi").

After creating it, note the **Client ID** and **Client Secret** shown.

## 5. Configure the Pi

On the Pi, create `~/.config/calendar_pi/client.conf`:
```
client_id=YOUR_CLIENT_ID.apps.googleusercontent.com
client_secret=YOUR_CLIENT_SECRET
```
(The directory is created automatically the first time the app runs if it
doesn't exist yet — you can create it by hand too:
`mkdir -p ~/.config/calendar_pi`.)

This file only needs to exist on the Pi — never commit it to the repo, and
it's not something you need on your dev machine.

## 6. First run: approve access

Run the built binary (`./build/calendar_pi`, interactively or via the
systemd service — see `docs/DEPLOYMENT.md`). With no stored authorization
yet, the **physical HDMI display** shows:

```
CONNECT GOOGLE CALENDAR

GO TO:
GOOGLE.COM/DEVICE

ENTER CODE:
ABCD-EFGH

EXPIRES IN 14:32
```

On your phone or laptop, open the URL shown, sign in with the Google
account whose calendar you want to display, and enter the code. Approval
usually takes a few seconds to be picked up — the Pi is polling Google in
the background. Once approved, the screen switches over to the live week
view automatically, and a refresh token is saved to
`~/.config/calendar_pi/token` (mode `0600`) so this step isn't needed
again unless that file is deleted or access is revoked from your Google
Account's [connected apps settings](https://myaccount.google.com/permissions).

If the code expires before you approve it (the countdown reaches zero),
the app requests a fresh one automatically — no restart needed.

## Troubleshooting

- **"OFFLINE" footer that never clears**: check `journalctl` (if run under
  systemd) or the terminal output (if run interactively) for
  `gcal_sync: token refresh failed` or `gcal_sync: events fetch failed` —
  usually a network issue, or a refresh token that's expired (see the
  7-day Testing-mode gotcha above) or been revoked.
- **Footer says "GOOGLE CALENDAR NOT CONFIGURED"**: `client.conf` is
  missing or malformed on the Pi — re-check step 5.
- **Want to re-authorize from scratch** (e.g. switching Google accounts):
  delete `~/.config/calendar_pi/token` and restart the app; it'll show the
  device-authorization screen again.
