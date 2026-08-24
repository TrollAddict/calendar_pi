# Connecting your Reolink camera

The bottom-right pane polls a snapshot from your Reolink camera every few
seconds and displays it -- a refreshing still image, not real-time video
(see "Why not real video?" below for why). No Reolink cloud account is
needed; the Pi talks directly to the camera over your local network.

## 1. Find the camera's LAN IP address

Use the Reolink app (Devices → your camera → Device Info) or check your
router's DHCP client list. A static/reserved IP (set in your router, or
as a DHCP reservation) is recommended so the Pi doesn't lose the camera
after a reboot changes its address.

## 2. Know its login

The default admin username is usually `admin`; the password is whatever
you set the first time you configured the camera (in the Reolink app or
web UI). This project only needs read-only snapshot access.

## 3. Configure the Pi

Create `~/.config/calendar_pi/camera.conf`:
```
host=192.168.1.50
user=admin
password=yourpassword
channel=0
```
- `host` — the camera's LAN IP (or hostname) from step 1, no `http://`.
- `channel` — optional, defaults to `0`. Only relevant if this "camera"
  is actually a channel on a Reolink NVR; a single standalone camera is
  always channel `0`.

(The directory is created automatically the first time the app runs if
it doesn't exist yet.) This file is never committed to the repo, and is
stored in plaintext (mode `0600`) the same way `client.conf` already
stores your Google OAuth client secret -- acceptable for a personal,
single-user device, but worth knowing.

## 4. Run it

`./build/calendar_pi` (or restart the systemd service). The camera pane
shows "CONNECTING TO CAMERA..." until the first snapshot succeeds, then
displays it letterboxed to fit the pane, refreshing automatically. If
`camera.conf` is missing, it shows "CAMERA NOT CONFIGURED" instead of
trying (and failing) to connect.

## Why not real video?

The Pi Zero W has a single ARM11 core with no realistic path to
decoding real-time H.264/RTSP video inside this project's bare-metal
DRM/GLES2 renderer. Polling a JPEG snapshot every couple of seconds
avoids that entirely -- it's not fluid video, but it's simple and
reliable on very limited hardware.

## Performance tip

Snapshot decode time scales with the camera's configured resolution. If
the feed feels sluggish or CPU-bound on the Pi, lower the camera's main
stream resolution in the Reolink app (Device Settings → Display →
Encode) — 1280x720 is a reasonable target. Snapshots decoded above
1920x1080 are rejected outright (logged, camera pane keeps showing the
last good frame) rather than attempting to downscale them.

## Troubleshooting

- **"CAMERA NOT CONFIGURED"**: `camera.conf` is missing or malformed —
  re-check step 3 (all three of `host`/`user`/`password` are required).
- **"CONNECTING TO CAMERA..." that never clears**: check `journalctl` (if
  run under systemd) or the terminal output for
  `reolink_sync: snapshot fetch failed` — usually a wrong IP/credentials,
  the camera being on a different network/VLAN than the Pi, or (see
  below) a firmware that doesn't accept this snapshot method.
- **"OFFLINE - LAST FRAME SHOWN"**: was connecting fine but the most
  recent poll failed — usually transient (Wi-Fi hiccup, camera reboot);
  it recovers on its own once the next poll succeeds.
- **Some Reolink firmware/models don't accept `user=`/`password=` query
  parameters directly** on the snapshot command and instead require a
  session token obtained via a separate `cmd=Login` call first. This
  project doesn't implement that login flow (known limitation, kept out
  of scope here) — if your camera doesn't work with the direct
  credentials approach, this feature currently won't work with it.
