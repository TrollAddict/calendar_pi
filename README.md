# calendar_pi

A full-screen calendar for a Raspberry Pi Zero W, rendered with OpenGL ES 2.0
directly to the HDMI output via DRM/KMS + GBM + EGL — no X11/Wayland desktop
required. Controlled remotely: run it from an SSH session and use the arrow
keys in your terminal to navigate.

- **Left / Right** — move selection by a day
- **Up / Down** — move selection by a week
- **Enter / Space** — jump back to today
- **q / Ctrl-C / Esc** — quit

Run with no controlling terminal attached (e.g. launched by systemd at
boot) and it doesn't exit — it falls back to a passive, read-only display
of the current date instead. See `docs/DEPLOYMENT.md` for running it that
way persistently, plus the full story on getting a headless Zero W to this
point in the first place (SD card pre-seeding, Wi-Fi, the works).

## How it's built

- `src/drm_display.*` — opens `/dev/dri/cardN`, finds the connected display
  and its preferred mode, and sets up a GBM window surface + EGL/GLES2
  context targeting it. Page flips are synced to vblank.
- `src/gl_renderer.*` — a tiny batched 2D quad renderer (one draw call for
  solid rects, one for text), pixel-space orthographic projection done in
  the vertex shader.
- `src/font.*` — a procedurally-built 5x7 bitmap font atlas (space, 0-9,
  A-Z) rendered as a texture, no font files or FreeType needed.
- `src/calendar_model.*` — pure date math (Zeller's congruence for weekday,
  Julian-day arithmetic for date add/subtract). Has a native unit test in
  `tests/`.
- `src/input.*` — puts the SSH session's terminal into raw, non-blocking
  mode and decodes arrow-key escape sequences.

This targets the modern Mesa `vc4-kms-v3d` driver stack that Raspberry Pi OS
Bullseye and newer use by default. Confirm it's active:

```sh
grep vc4 /boot/firmware/config.txt   # expect: dtoverlay=vc4-kms-v3d
```

## Building — natively on the Pi (the path that actually works)

Cross-compiling from another machine looked appealing but hit real ARMv6
toolchain incompatibilities that don't have a clean fix — see the warning
at the top of `cmake/toolchain-rpi.cmake` if curious, or
`docs/DEPLOYMENT.md` for the full story. Building directly on the Pi
sidesteps the whole problem, and the project is small enough that a single
ARM11 core still builds it in a couple of minutes.

On the Pi:
```sh
sudo apt install build-essential cmake libdrm-dev libgbm-dev libegl1-mesa-dev libgles2-mesa-dev
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

## Cross-compiling (unresolved — for reference only)

`cmake/toolchain-rpi.cmake` exists and gets partway there (fixes a
crt1.o/Scrt1.o sysroot mismatch), but binaries built with it still crash
with `SIGILL` on real hardware from a second, unfixed issue in GCC's own
bundled startup objects. Don't use it expecting a working binary today —
see the comment block at the top of that file for what was tried and why
it's parked. If you want to attempt it anyway (e.g. as a starting point for
finding a real fix), `docs/DEPLOYMENT.md` has the sysroot-pulling steps
that were used to get this far.

## Running the native date-math test

`calendar_model.c` has no Pi-specific dependencies, so its logic can be
checked on your dev machine without the cross toolchain:

```sh
gcc -std=c11 -Wall -Wextra -o /tmp/test_calendar tests/test_calendar_model.c src/calendar_model.c
/tmp/test_calendar
```

## Known limitations

- Font covers space, digits, and uppercase A-Z only (enough for month
  names, weekday abbreviations, and day numbers).
- Arrow-key sequences are assumed to arrive in a single `read()`; over a
  slow/high-latency SSH link a 3-byte escape sequence could in principle be
  split across two reads and get dropped. Not an issue on typical local or
  broadband SSH sessions.
- No timezone/locale handling beyond the system's local time for "today".
