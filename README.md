# calendar_pi

A full-screen calendar for a Raspberry Pi Zero W, rendered with OpenGL ES 2.0
directly to the HDMI output via DRM/KMS + GBM + EGL — no X11/Wayland desktop
required. Controlled remotely: run it from an SSH session and use the arrow
keys in your terminal to navigate.

- **Left / Right** — move selection by a day
- **Up / Down** — move selection by a week
- **Enter / Space** — jump back to today
- **q / Ctrl-C / Esc** — quit

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

## 1. Install a cross compiler (on your dev machine)

```sh
sudo apt install crossbuild-essential-armhf
```

This provides `arm-linux-gnueabihf-gcc`, targeting the same armhf/glibc ABI
Raspberry Pi OS (32-bit) uses.

## 2. Build a sysroot from your actual Pi

The cross compiler needs ARM builds of libdrm, gbm, EGL and GLESv2 headers
and libraries — these must come from the Pi itself so they match its Mesa
version. On the **Pi**, install the dev packages:

```sh
sudo apt install libdrm-dev libgbm-dev libegl1-mesa-dev libgles2-mesa-dev
```

Then, back on your **dev machine**, pull a sysroot over rsync (replace
`pi@raspberrypi.local` with your Pi's user@host):

```sh
mkdir -p ~/rpi-sysroot
rsync -rl --safe-links pi@raspberrypi.local:/usr/include ~/rpi-sysroot/usr/
rsync -rl --safe-links pi@raspberrypi.local:/usr/lib ~/rpi-sysroot/usr/
rsync -rl --safe-links pi@raspberrypi.local:/lib ~/rpi-sysroot/
```

If linking later fails on absolute symlinks (common with rsynced sysroots,
e.g. a `.so` symlink pointing at `/lib/arm-linux-gnueabihf/...`), rewrite
sysroot symlinks to be relative — search for
`sysroot-relativelinks.py` (from the Raspberry Pi tools project) or fix the
handful of offending links by hand with `find ~/rpi-sysroot -xtype l`.

## 3. Configure and build

```sh
cmake -B build -S . -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain-rpi.cmake -DRPI_SYSROOT=$HOME/rpi-sysroot
cmake --build build -j$(nproc)
```

This produces `build/calendar_pi`, an armhf ELF binary.

## 4. Deploy and run

```sh
scp build/calendar_pi pi@raspberrypi.local:~/
ssh pi@raspberrypi.local
./calendar_pi
```

Notes:

- The Pi must **not** be running a desktop compositor (X11/Wayland/labwc) at
  the same time — whichever process holds DRM master owns the display.
  Either boot the Pi to the CLI (`sudo raspi-config` → System Options → Boot
  → Console), or stop the desktop session before running
  (`sudo systemctl stop lightdm` or equivalent, depending on your image).
- The `pi` user is normally already in the `video`/`render` groups on
  Raspberry Pi OS, so `/dev/dri/card*` access shouldn't need root. If you
  get a permission error opening the device, check `groups` and
  `ls -l /dev/dri/`.
- Pass a different device as the first argument if `/dev/dri/card0` isn't
  the right one, e.g. `./calendar_pi /dev/dri/card1`.

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
