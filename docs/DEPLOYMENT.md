# Deploying to a Raspberry Pi Zero W

This covers everything between "blank SD card" and "calendar running
persistently on boot" — the parts that aren't obvious from the source or the
main README, mostly learned the hard way getting this running on real
ARMv6 hardware with no keyboard/monitor ever attached to the Pi.

## 1. Flash the SD card

Use Raspberry Pi Imager with **Raspberry Pi OS Lite** (32-bit). Lite is not
just acceptable but preferred here: it boots straight to a console with no
desktop compositor running, and this app needs to be the sole owner of the
DRM device. A desktop session (X11/Wayland) would fight it for the display.

If you're on a recent/experimental OS build (e.g. an early Trixie-based
port) and the Imager's "OS Customisation" (gear icon / Ctrl+Shift+X)
settings don't seem to take effect on first boot, don't fight it — do the
pre-configuration by hand instead, below. It's more reliable and it's what
this whole doc assumes.

## 2. Headless pre-configuration (no keyboard/monitor needed)

After flashing, the card exposes two partitions: a small FAT32 one
(`bootfs`) and a larger ext4 one (`rootfs`). Re-mount the card on your dev
machine and edit these before first boot.

**Enable SSH** — on `bootfs`, create an empty file (no extension, no
content) named exactly `ssh`.

**Create your user account** — on `bootfs`, create `userconf.txt`
containing one line:
```
your_username:$6$hashsalt$therestofthehash
```
Generate the hash with `openssl passwd -6` (run with no arguments — it
prompts interactively so the plaintext password never touches shell
history).

**Wi-Fi — do not rely on the legacy `wpa_supplicant.conf` trick.** Dropping
a `wpa_supplicant.conf` onto `bootfs` is the classic headless-Pi advice, but
it's tied to the old `dhcpcd` network stack. Current Raspberry Pi OS
(Bookworm and newer, including Trixie) uses NetworkManager by default, which
largely ignores that file. Instead, write a NetworkManager keyfile directly
onto **`rootfs`** (not `bootfs`) at
`etc/NetworkManager/system-connections/preconfigured.nmconnection`:

```ini
[connection]
id=preconfigured
uuid=PASTE-OUTPUT-OF-uuidgen-HERE
type=wifi
interface-name=wlan0

[wifi]
mode=infrastructure
ssid=YourNetworkName

[wifi-security]
key-mgmt=wpa-psk
psk=YourPassword

[ipv4]
method=auto

[ipv6]
method=auto
addr-gen-mode=default
```

NetworkManager silently refuses to load this unless it's owned by root and
mode `600` — this is the one step that actually needs `sudo` on your dev
machine (mounting the card itself generally doesn't):
```sh
sudo chown root:root .../preconfigured.nmconnection
sudo chmod 600 .../preconfigured.nmconnection
```

**Unblock the Wi-Fi radio.** Raspberry Pi OS soft-blocks (`rfkill`) the
Wi-Fi radio until a regulatory country code is set, for compliance reasons —
normally handled by `raspi-config` or the Imager's customisation flow on
first boot. Since we're bypassing that, wire in a systemd oneshot unit that
does it unconditionally, on `rootfs` at
`etc/systemd/system/wifi-unblock.service`:

```ini
[Unit]
Description=Unblock WiFi radio and set regulatory domain before NetworkManager starts
Before=NetworkManager.service

[Service]
Type=oneshot
ExecStart=rfkill unblock wifi
ExecStart=raspi-config nonint do_wifi_country US
RemainAfterExit=yes

[Install]
WantedBy=multi-user.target
```
Enable it with a symlink (the *target* must be the absolute path as it'll
exist on the Pi, not your host mount path):
```sh
sudo mkdir -p .../rootfs/etc/systemd/system/multi-user.target.wants
sudo ln -s /etc/systemd/system/wifi-unblock.service \
  .../rootfs/etc/systemd/system/multi-user.target.wants/wifi-unblock.service
```

**Turn on persistent logging**, so a failed boot is diagnosable without
ever needing a console:
```sh
sudo mkdir -p .../rootfs/var/log/journal
```
`systemd-journald` switches to disk-backed logging automatically the moment
that directory exists — no config file needed. If Wi-Fi still doesn't come
up after a boot attempt, pull the card again and read exactly what
happened:
```sh
journalctl -D .../rootfs/var/log/journal --no-hostname -u NetworkManager -u wifi-unblock -b
```

## 3. Finding the Pi on the network

`127.0.1.1` in `/etc/hosts` is a red herring if you see it — it's Debian's
own-hostname loopback alias, not a real network address; pinging it just
pings whatever machine you ran the ping from.

```sh
ping raspberrypi.local        # mDNS, works out of the box via Avahi
```
or check `arp -a` / your router's DHCP client list for a MAC prefix of
`b8:27:eb`, `dc:a6:32`, `e4:5f:01`, `28:cd:c1`, or `d8:3a:dd` (Raspberry Pi
vendor OUIs).

## 4. Building — do this natively, on the Pi

**Cross-compiling from another machine does not currently work for this
board and isn't worth pursuing further** — see the note in
`cmake/toolchain-rpi.cmake` for the full story. Short version: the Pi Zero
W's ARM1176 core is ARMv6, and Debian/Ubuntu's `crossbuild-essential-armhf`
targets ARMv7+ (Debian dropped ARMv6 from the armhf baseline years ago). Two
separate incompatibilities showed up:

1. The cross-compiler's default `crt1.o`/`Scrt1.o` startup objects come
   from its own bundled sysroot, not the one you point `--sysroot` at —
   fixable with an extra `-B<sysroot>/usr/lib/arm-linux-gnueabihf` flag
   (already in the toolchain file).
2. GCC's own bundled `crtbeginS.o`/`crtendS.o` (injected into every binary
   for constructor/destructor boilerplate like `register_tm_clones`) are
   compiled Thumb-2, which the ARMv6 core can't execute at all — this one
   has no known flag-level fix, since those object files ship pre-built
   with the compiler package itself.

Building natively sidesteps both, because there's no cross-target mismatch
possible — the Pi's own `gcc` is built for the hardware it's running on. The
project is small enough (7 source files) that a single ARM11 core still
builds it in a couple of minutes.

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

### Appendix: pulling a cross-compile sysroot (for reference — doesn't produce a working binary yet)

This is the setup that got the cross-compile toolchain file as far as it
goes, kept in case someone picks up chasing the remaining `crtbeginS.o`
issue. On your dev machine:
```sh
sudo apt install crossbuild-essential-armhf
```
On the **Pi**, install the dev headers/libs the sysroot needs:
```sh
sudo apt install libdrm-dev libgbm-dev libegl1-mesa-dev libgles2-mesa-dev
```
Back on the **dev machine**, pull them over:
```sh
mkdir -p ~/rpi-sysroot
rsync -rl --safe-links your_username@<pi-address>:/usr/include ~/rpi-sysroot/usr/
rsync -rl --safe-links your_username@<pi-address>:/usr/lib ~/rpi-sysroot/usr/
rsync -rl --safe-links your_username@<pi-address>:/lib ~/rpi-sysroot/
```
Then:
```sh
cmake -B build -S . -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain-rpi.cmake -DRPI_SYSROOT=$HOME/rpi-sysroot
cmake --build build -j$(nproc)
```
This links successfully and produces a valid armhf ELF, but it still
crashes with `SIGILL` on the actual board — see the toolchain file's header
comment.

## 5. Connecting Google Calendar

The app pulls your primary Google Calendar's events over the network. See
`docs/GOOGLE_CALENDAR_SETUP.md` for the full setup: a one-time Google
Cloud Console configuration, then a one-time authorization step run **on
your dev machine** (`tools/authorize_gcal.py` — needs a real browser,
which the Pi doesn't have), which produces a refresh token. Drop the
`client_id`/`client_secret` into `~/.config/calendar_pi/client.conf` and
that refresh token into `~/.config/calendar_pi/token` (mode `0600`) on the
Pi — both are plain files, easiest to place via the same `rsync`/`scp`
you're already using to deploy the source.

Unlike the OS-level headless setup above, there's no on-device approval
step to watch for here — authorization happens entirely on your dev
machine before you ever touch the Pi. If you do run the binary before
those two files are in place (e.g. testing the build before finishing
Google setup), the physical display just shows a static "not yet
authorized" screen in place of the calendar rather than the live view;
once both files exist, a restart (or the background sync thread noticing
the token file, checked every few seconds) picks it up automatically.

## 6. Running persistently on boot

The app detects whether it has a controlling terminal. Run interactively
over SSH and you get full navigation; run with no terminal attached (e.g.
launched by systemd) and it falls back to a passive, read-only display of
the current date — it does not exit.

Create `/etc/systemd/system/calendar_pi.service` on the Pi:
```ini
[Unit]
Description=Full-screen calendar display
After=systemd-udev-settle.service multi-user.target network-online.target
Wants=systemd-udev-settle.service network-online.target

[Service]
Type=simple
User=your_username
WorkingDirectory=/home/your_username/calendar_pi
ExecStart=/home/your_username/calendar_pi/build/calendar_pi
Restart=on-failure
RestartSec=2

[Install]
WantedBy=multi-user.target
```
```sh
sudo systemctl daemon-reload
sudo systemctl enable --now calendar_pi.service
```

**The DRM device only allows one owner at a time.** Once the daemon is
running, you can't launch a second interactive instance alongside it — SSH
in and get navigation back with:
```sh
sudo systemctl stop calendar_pi.service
./build/calendar_pi        # interactive again, since it has your SSH tty
# when done, q to quit, then:
sudo systemctl start calendar_pi.service
```

## Troubleshooting notes

- `dmesg` needing root to show anything (`kernel.dmesg_restrict`) is common
  — use `sudo dmesg` rather than assuming a segfault produced no kernel log
  at all.
- For a real backtrace on a crash, without needing a full interactive gdb
  session:
  ```sh
  gdb -q --batch -ex run -ex bt --args ./calendar_pi
  ```
  Add `-ex "x/8i \$pc-16"` to also disassemble around the crash site — this
  is what caught the Thumb-2 instruction-width mismatch above (consecutive
  instruction addresses 2 bytes apart instead of 4 is the tell for Thumb
  vs. ARM encoding).
