# rbpi5 — rekordbox (XDJ-RX3) on Raspberry Pi 5

**English** · [Русский](README.ru.md)

Run the **Pioneer DJ XDJ-RX3** standalone *rekordbox* player (`rbp`, firmware
v1.20) on a **Raspberry Pi 5** with a 7" touch panel and a **DDJ-FLX4** or
**DDJ-GRV6** controller: the real player's UI, browser, effects and USB library,
driven by the controller, with sound through the controller's audio interface.

This is a fork of [salesforcedoc/rbpi4b](https://github.com/salesforcedoc/rbpi4b)
(Raspberry Pi 4), hence the name: **rb** is what the player is called inside the
firmware, **pi5** the board. It merges in the Pi 5 work of
[stableneo/rbpi5-grv6](https://github.com/stableneo/rbpi5-grv6) and adds a
DDJ-GRV6 map and a one-command build-and-deploy kit. Credits: [SOURCES.md](SOURCES.md).

> **Read [NOTICE.md](NOTICE.md) first.** This repository contains **no** firmware,
> keys, `rbp` binary or any other Pioneer/AlphaTheta file. It only works on files
> **you** supply. How you obtain them, and whether you may do so where you live,
> is your decision and your responsibility; everything you do with it is at your
> own risk.

```
your extracted XDJ-RX3 v1.20 tree ─► make assets ─► make build ─► make pi-setup ─► make deploy
          (you supply it)            extracted/      work/*.tgz     Pi 5, once       /opt/rblive4
```

## What you need

| | |
|---|---|
| **Raspberry Pi 5** | 4 GB is plenty. Use the official 27 W PSU: a DDJ-FLX4 is powered by the Pi's USB. |
| **OS on the Pi** | Raspberry Pi OS **Lite 64-bit** (trixie), SSH enabled, a user with sudo |
| **Display** | HDMI, ideally 1280×800 (other sizes are scaled), USB touch |
| **Controller** | DDJ-FLX4 and/or DDJ-GRV6 (the GRV6 only on its own power adapter) |
| **Workstation** | Linux (x86_64 or arm64) with **Docker** and **git**, ~5 GB free |
| **Assets** | your **already extracted** XDJ-RX3 **v1.20** tree: the directory holding `gui/` and `images/` (`rootfs.cramfs`, `gui.tar.gz`, `pdj.tar.gz`). See [docs/pi5/assets.md](docs/pi5/assets.md) |

## Quick start

```sh
git clone https://github.com/r1x0s/rbpi5
cd rbpi5
cp pi.env.example pi.env           # the Pi's address, HDMI port, rotation
make all SRC=/path/to/XDJRX3       # assets -> build -> Pi setup -> install
```

`make all` asks once whether to reboot the Pi (needed after the kernel switch).
After the install the Pi boots straight into rekordbox.

## Step by step

| Step | Command | What it does | Where |
|---|---|---|---|
| 1 | `make assets SRC=…` | from your XDJ-RX3 tree: unpacks the rootfs and GUI, checks the stock `rbp`'s md5, applies the interoperability patches → `extracted/` | workstation, Docker |
| 2 | `make build` | DirectFB 1.4.16, the shims + their tests, the chroot → `work/rbpi4b-pi4.tgz` | workstation, Docker |
| 3 | `make pi-setup` | `kernel8.img` (4 KB pages), display mode and rotation, packages, `rb.local.conf`, USB fix for the FLX4; offers the reboot | Pi over SSH |
| 4 | `make deploy` | installs the player to `/opt/rblive4` and starts it | Pi over SSH |
| — | `make doctor` | read-only health check on the Pi | Pi |

Every step can be re-run; a second run upgrades what is there.

## Settings (`pi.env`)

| Key | Values | |
|---|---|---|
| `PI_HOST` | `pi@192.168.x.y` | where to install |
| `PANEL_CONNECTOR` | `HDMI-A-1` / `HDMI-A-2` | HDMI-A-1 is the port next to the USB-C power |
| `PANEL_ROTATE` | `0` / `180` | default `0`; `180` only for an upside-down panel, see below |
| `MIDI_MAP` | `auto` / `flx4` / `grv6` | `auto`: the map of the controller plugged in at start |
| `USB_POWERCYCLE` | `1` / `0` | power-cycle USB at boot when no controller enumerated (a bus-powered FLX4 needs it) |

### Upside-down panel (optional)

Only for a panel mounted upside down (many 7" HDMI panels route their cables
better that way). With a normally mounted panel do nothing: `PANEL_ROTATE=0` is
the default.

If the picture and the touch are upside down:

1. In `pi.env`: `PANEL_ROTATE=180`.
2. `make pi-setup`: it writes `video=<port>:1280x800@60,rotate=180` to
   `cmdline.txt` (the kernel rotates the picture) and `RB_POINT_INVERT_X=1`,
   `RB_POINT_INVERT_Y=1` to `/opt/rblive4/rb.local.conf` (the touch), then offers
   the reboot.

To undo: `PANEL_ROTATE=0` and `make pi-setup` again. If the touch is still off on
one axis only, flip that one by hand in `rb.local.conf` and
`sudo systemctl restart rblive4`.

### Fine tuning

Player settings (audio, jog, LEDs, …) live in `/opt/rblive4/rb.local.conf` on the
Pi; every key is described in [scripts/device/rb.conf](scripts/device/rb.conf).
Installs never overwrite that file.

## Using it

- **Switching controllers:** with `MIDI_MAP=auto`, restart the player
  (`sudo systemctl restart rblive4`) after plugging in the other one.
- **Changing a mapping:** the maps are C code compiled into a shim, not files
  next to the player. Edit `scripts/shims/map_flx4.c` or `map_grv6.c` and run
  `make shims`: it builds, runs the tests, copies the shims to the Pi and
  restarts the player in seconds. See [docs/pi5/mapping.md](docs/pi5/mapping.md).

## Status

Running on the author's Pi 5 since 2026-10-23: Pi OS trixie 64-bit, a 1280×800
HDMI panel rotated 180°, DDJ-FLX4 and DDJ-GRV6. Long sets (≥ 1 h without a
restart) are **not confirmed yet**. What is measured and what is not, and the
known limits: [docs/pi5/hardware.md](docs/pi5/hardware.md).

## Repository layout

| Path | |
|---|---|
| `Makefile`, `pi.env.example`, `pi5/` | this fork's kit: Docker build image, build / Pi setup / deploy scripts |
| `scripts/shims/` | the LD_PRELOAD shims: display, audio, controller maps, LEDs (C, with tests) |
| `scripts/device/` | what runs on the Pi: `install.sh`, `start-rb.sh`, `doctor.sh`, `rb.conf` |
| `scripts/build-chroot.sh`, `tools/patch-rbp/`, `tools/build-directfb/` | chroot assembly, the `rbp` interoperability patches, the DirectFB patch |
| `docs/` | upstream's documentation (Pi 4, protocols, bring-up); `docs/pi5/` — this fork's |
| [README-rbpi4b.md](README-rbpi4b.md), [TUTORIAL.md](TUTORIAL.md) | upstream's README and manual walk-through (Pi 4) |

## Before you push

```sh
make check      # nothing that must stay local (firmware, keys, binaries) is tracked
```

Publishing your copy: [docs/pi5/publish.md](docs/pi5/publish.md).

## License

MIT ([LICENSE](LICENSE)) for the code here. Third-party components and their
licenses: [NOTICE.md](NOTICE.md).
