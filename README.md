# calendar_pi

A full-screen dashboard for a Raspberry Pi 4, rendered with OpenGL ES
2.0 directly to the HDMI output via DRM/KMS + GBM + EGL — no X11/Wayland
desktop required. The screen is split into three panes: a live Google
Calendar week view across the top, a Google Tasks to-do list in the
bottom-left, and a Reolink camera feed (periodic snapshots, not full
video — see `docs/REOLINK_SETUP.md`) in the bottom-right. Controlled
remotely: run it from an SSH session and use the arrow keys in your
terminal to navigate the calendar; the to-do and camera panes are
passive, read-only displays.

- **Left / Right** — move calendar selection by a day
- **Up / Down** — move calendar selection by a week
- **Enter / Space** — jump back to today
- **q / Ctrl-C / Esc** — quit

Run with no controlling terminal attached (e.g. launched by systemd at
boot) and it doesn't exit — it falls back to a passive, read-only display
instead (still fully synced with Google Calendar/Tasks and polling the
camera). See `docs/DEPLOYMENT.md` for running it that way persistently,
plus the full story on getting a headless Pi to this point in the first
place (SD card pre-seeding, Wi-Fi, the works).

## How it's built

- `src/drm_display.*` — opens `/dev/dri/cardN`, finds the connected display
  and its preferred mode, and sets up a GBM window surface + EGL/GLES2
  context targeting it. Page flips are synced to vblank.
- `src/gl_renderer.*` — a tiny batched 2D quad renderer (one draw call for
  solid rects, one for a dynamic image texture, one for text), pixel-space
  orthographic projection done in the vertex shader. `renderer_set_region`
  offsets subsequent draws into one of the three screen panes, so each
  pane's drawing code can work in its own local (0,0)-origin coordinates.
- `src/font.*` — a procedurally-built 5x7 bitmap font atlas (space, 0-9,
  A-Z) rendered as a texture, no font files or FreeType needed.
- `src/calendar_model.*` — pure date math (Zeller's congruence for weekday,
  Julian-day arithmetic for date add/subtract, week-start calculation). Has
  a native unit test in `tests/`.
- `src/input.*` — puts the SSH session's terminal into raw, non-blocking
  mode and decodes arrow-key escape sequences.
- `src/calendar_view.*` — draws the week view (hour grid, all-day strip,
  event blocks, "now" line) and the static "not yet authorized" screen.
  Pure rendering: it never touches the network.
- `src/gcal_colors.*` — Google Calendar's fixed 11-color event palette.
- `src/config_store.*` — reads the user-provided OAuth client id/secret
  and Reolink camera credentials, and persists the refresh token, under
  `$HOME/.config/calendar_pi/`.
- `src/event_store.*` — mutex-protected shared list of fetched events,
  written by the background sync thread and snapshotted once per frame by
  the render loop.
- `src/oauth_refresh.*` — `grant_type=refresh_token` token exchange, via
  libcurl. (Google's OAuth device-authorization flow was tried first but
  doesn't support Calendar API scopes at all — see
  `docs/GOOGLE_CALENDAR_SETUP.md`.) Shared by both the calendar and tasks
  sync threads.
- `src/gcal_client.*` — fetches and parses `events.list` from the Google
  Calendar API (vendored `third_party/cJSON` for parsing).
- `src/gcal_sync.*` — background thread that waits for a refresh token to
  appear, then re-syncs events every 15 minutes.
- `src/task_store.*`, `src/gtasks_client.*`, `src/gtasks_sync.*`,
  `src/todo_view.*` — the to-do pane's equivalents of the four modules
  above: fetches the default Google Tasks list, on its own independent
  15-minute sync thread, reusing the same OAuth client/token.
- `src/camera_store.*`, `src/reolink_client.*`, `src/reolink_sync.*`,
  `src/camera_view.*` — the camera pane: a background thread logs in via
  the camera's session-token API and polls a JPEG snapshot on an interval
  (vendored `third_party/stb_image` for JPEG decoding), and the render
  loop uploads each new frame to a GL texture, drawn aspect-fit within
  the pane.

## Google Calendar and Tasks sync

The calendar pane is populated live from your Google Calendar's primary
calendar (read-only); the to-do pane from your default Google Tasks list
(read-only). Both share one OAuth client/token. Setup is covered in
`docs/GOOGLE_CALENDAR_SETUP.md` — do that first. In short: a one-time
Google Cloud Console configuration, then a one-time authorization step
you run on your own computer (`tools/authorize_gcal.py`, needs Python 3,
opens a browser) that produces a refresh token you copy to the Pi. The Pi
never needs a browser or credentials of its own beyond that copied token.

Once configured, each pane syncs independently every 15 minutes; if the
network drops or a sync fails, it keeps showing the last successfully
fetched data with a small "OFFLINE" footer rather than going blank. Until
the token is in place, each pane shows its own "not yet authorized"
message instead of live data.

## Reolink camera

The camera pane polls a JPEG snapshot from a Reolink camera on your local
network every 2 seconds — see `docs/REOLINK_SETUP.md` for setup. No cloud
account or internet access is needed for this pane, just LAN connectivity
to the camera.

This targets the modern Mesa `vc4-kms-v3d` driver stack that Raspberry Pi OS
Bullseye and newer use by default. Confirm it's active:

```sh
grep vc4 /boot/firmware/config.txt   # expect: dtoverlay=vc4-kms-v3d
```

## Building — natively on the Pi

Building directly on the Pi avoids any cross-compile toolchain concerns
entirely, and the Pi 4's quad-core Cortex-A72 builds this small project
in well under a minute.

On the Pi:
```sh
sudo apt install build-essential cmake libdrm-dev libgbm-dev libegl1-mesa-dev libgles2-mesa-dev libcurl4-openssl-dev
```
Copy the source over (from your dev machine):
```sh
rsync -rl --exclude=build /path/to/calendar_pi/ your_username@<pi-address>:~/calendar_pi/
```
Then, on the Pi:
```sh
cd ~/calendar_pi
cmake -B build -S .
cmake --build build -j$(nproc)
./build/calendar_pi
```

Notes:

- The Pi must **not** be running a desktop compositor (X11/Wayland/labwc) at
  the same time — whichever process holds DRM master owns the display.
  Raspberry Pi OS **Lite** avoids this entirely (no desktop to conflict
  with); on Full, stop the desktop session first
  (`sudo systemctl stop lightdm` or equivalent, depending on your image).
- Your user is normally already in the `video`/`render` groups on
  Raspberry Pi OS, so `/dev/dri/card*` access shouldn't need root. If you
  get a permission error opening the device, check `groups` and
  `ls -l /dev/dri/`.
- Pass a different device as the first argument if `/dev/dri/card0` isn't
  the right one, e.g. `./calendar_pi /dev/dri/card1`.
- For running this persistently on boot (systemd unit, and the tradeoffs
  that come with it) and the full headless SD-card setup story, see
  `docs/DEPLOYMENT.md`.

## Cross-compiling (historical note — was for a Pi Zero W target)

This project originally targeted a Raspberry Pi Zero W. `cmake/toolchain-rpi.cmake`
documents a real, unresolved ARMv6-specific toolchain blocker hit while
cross-compiling for that board (its ARM1176 core can't execute the
Thumb-2 startup objects GCC bundles for armhf) — see the comment block
at the top of that file for the full story. That blocker is specific to
the Zero W's ARMv6 core and doesn't apply to the Pi 4's ARMv8 Cortex-A72,
but cross-compiling for the Pi 4 hasn't been set up or tested in this
project — native builds are fast enough on its quad-core CPU that there
hasn't been a need to.

## Running the native date-math test

`calendar_model.c` has no Pi-specific dependencies, so its logic can be
checked on your dev machine without the cross toolchain:

```sh
gcc -std=c11 -Wall -Wextra -o /tmp/test_calendar tests/test_calendar_model.c src/calendar_model.c
/tmp/test_calendar
```

## Known limitations

- Font covers space, digits, uppercase A-Z, and a handful of punctuation
  (`: - . / +`) only — enough for weekday/day labels, event titles (folded
  to uppercase), and the device-authorization screen.
- Arrow-key sequences are assumed to arrive in a single `read()`; over a
  slow/high-latency SSH link a 3-byte escape sequence could in principle be
  split across two reads and get dropped. Not an issue on typical local or
  broadband SSH sessions.
- No timezone/locale handling beyond the system's local time for "today".
- The hour grid only shows 6:00-22:00; events entirely outside that window
  aren't drawn.
- Overlapping events in a day use a simple even-width column split, not
  Google Calendar's own width-maximizing layout.
- Only the primary calendar is synced, and the fetch window is a fixed
  rolling ~6 weeks (7 days back, 35 days ahead) around today, independent
  of how far you've navigated.
- A multi-day timed event (one that starts before midnight and continues
  into the next day) is drawn only in its start day's column.
- The camera pane shows periodically-polled still snapshots, not real
  video. The Pi 4 does have real hardware H.264 decode (unlike the Zero W
  this project originally targeted, which ruled real video out entirely),
  so real-time RTSP video is plausible future work here, but it would
  need an actual RTSP client and a real dependency like GStreamer/FFmpeg
  wired into this bare-metal renderer — a substantial addition on its
  own, not implemented. See `docs/REOLINK_SETUP.md`.
- Camera auth always goes through Reolink's `cmd=Login` session-token
  flow (the same one the official app uses) rather than the simpler-but
  less-widely-supported direct `user=`/`password=` query-param shortcut
  some firmware accepts on other commands.
- The to-do pane only shows the default Google Tasks list, and only
  incomplete tasks (up to 100 fetched per sync); it's read-only, with no
  on-screen way to add/check off items.
- Anyone who set up Calendar sync before the to-do pane existed needs to
  re-authorize once (delete the token, rerun `tools/authorize_gcal.py`)
  to grant the added Tasks scope — see the Troubleshooting section of
  `docs/GOOGLE_CALENDAR_SETUP.md`.
