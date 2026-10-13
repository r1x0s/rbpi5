# Raspberry Pi 5 bring-up record (Phase 1: UI, mouse and keyboard)

This is the Pi 5 counterpart of the bring-up table in
[13 — Raspberry Pi 4B](../13-raspberrypi4.md#bring-up-order), filled in as the
port is run. It is a **record, not a plan**: a cell holds a measurement taken on
the Pi 5, or it says `TODO: unverified`. A value carried over from the Pi 4, or
from documentation, is written as **expected**, never as a result.

**Phase 1 scope.** No controller and no USB sound card are attached. `rbp` runs
silent (`audioshim` logs `NO OUTPUT DEVICE — rbp will run silent`, as upstream
measured in S4.10), and the control surface is the keyboard
(`RB_MIDI_MAP=kbd`) plus a USB mouse. Audio, controller and LED rows (S4.x,
S5.x, S8, S9) belong to Phase 2.

## The unit

| | |
|---|---|
| Board | **Raspberry Pi 5 Model B Rev 1.0** (revision `c04170`), 4 GB (`free -m`: 3991 MB) |
| PSU | negotiates **5000 mA** (`/proc/device-tree/chosen/power/max_current`), so the firmware sets `usb_max_current_enable=1` by itself (it is not in config.txt). EXT5V 5.13 V idle |
| OS image | Raspberry Pi OS Lite 32-bit: `Raspbian GNU/Linux 13 (trixie)`, `armhf`, `LONG_BIT` 32; boot files dated 2026-10-06 |
| Kernel (`uname -r`) | **`6.18.50+rpt-rpi-v8`** (`#1 SMP PREEMPT Debian 1:6.18.50-1+rpt1 (2026-09-11)`), the same kernel upstream measured on the Pi 4. Booted with **no** `kernel=` line (A1) |
| `getconf PAGESIZE` | **4096** |
| Monitor | model: TODO. EDID: native **1920×1080@60** on `HDMI-A-1`; mode list 1920×1080, 1600×900, 1280×1024, 1280×720, 1024×768 (**no 1280×800**); physical 350×200 mm |
| Pointer / keyboard | TODO (none attached during the survey: the only input nodes were `pwr_button` and the two HDMI CEC/jack devices) |
| Deployed commit + tarball sha256 | nothing deployed yet (`/opt/rblive4` absent). Built and ready: `port/pi5` @ 8e71d73, `ac4d8d94…b881` (S0.3) |

The raw inventory is `tools/pi5-survey.sh`'s output, committed beside this file
as [`pi5-survey.txt`](pi5-survey.txt) (taken 2026-10-13 07:56 UTC, before any
deploy; the PARTUUID and the Imager instance id masked by hand, as the script
now does itself).

## Board audit (what upstream assumes, and what the Pi 5 says)

Each row is closed either as **verified, no change** or by a numbered change.
Pi 4 values are upstream's measurements from [13](../13-raspberrypi4.md).

| # | Upstream assumption (where) | Pi 4 (upstream, measured) | Pi 5 result |
|---|---|---|---|
| A1 | 32-bit EL0 on a 64-bit kernel, **4 K pages** (`install.sh`, `doctor.sh` §4) | `6.18.50+rpt-rpi-v8`, 4 K | **verified, no change.** `6.18.50+rpt-rpi-v8`, page size 4096, `32-bit EL0 Support` at 0.003 s. The 32-bit image ships `kernel7.img`, `kernel8.img`, `kernel.img` and **no `kernel_2712.img`**, so the 16 K kernel is not on the card and no `kernel=` pin is needed. `install.sh`/`doctor.sh` check the page size in case an image ever adds one |
| A2 | `/dev/fb0` is `vc4drmfb`, one page, stride = width × bytes (`fbdump`) | 1280×800, 16 bpp, stride 2560, one page | **same kind of fb**, pre-recipe reading: `vc4drmfb`, **1920×1080**, 16 bpp RGB565 (565 @ 11/5/0), stride 3840 (no padding), `smem_len` 4147200 = one page, `ypanstep` 1/1 (the same misleading value as the Pi 4). Geometry depends on S2.1's `video=` mode |
| A3 | DRM overlay card is `/dev/dri/card1` (`drmband.c` `DRM_BAND_CARD`) | card0 = v3d, card1 = vc4 | **verified, no change.** card0 = `v3d` (`brcm,2712-v3d`), card1 = `vc4-drm` (`brcm,bcm2712-vc6`). The plane lists **44** formats (Pi 4: 39); `drmband.c` asks for up to 64, and both formats it uses (`XR24`, `RG16`) are on the list |
| A4 | connector names `HDMI-A-1`/`HDMI-A-2` (`boot-trim.sh`, `display-watch.sh`) | sink on `HDMI-A-1` | **verified, no change.** `card1-HDMI-A-1` connected/enabled, `card1-HDMI-A-2` disconnected (plus two `Writeback` connectors, unknown/disabled). Whether `video=HDMI-A-1:1280x800@60` takes on this 1080p monitor is S2.1 |
| A5 | HDMI audio `vc4hdmi0/1`, `IEC958_SUBFRAME_LE` (`rb.conf`, `audioshim.c`) | as stated | ids **unchanged**: card 1 `vc4hdmi0`, card 2 `vc4hdmi1` (`MAI PCM i2s-hifi-0`). There is no card 0 (the Pi 5 has no analog jack). Format: Phase 2 |
| A6 | `fbcon=map:1 console=tty3 …` keeps the console off the UI (`boot-trim.sh`) | works | TODO: unverified (S2.4) |
| A7 | config.txt edits only under a final `[all]` (`boot-trim.sh config_apply`) | `boot_delay=0`, `disable_splash=1` | precondition **holds**: the last header is `[all]` (after `[cm4]`, `[cm5]`, `[pi5] dtoverlay=nospi10`). Whether the trim applies cleanly is checked at install |
| A8 | USB media = `/sys/block/sd*` whose path contains `/usbN` (`usb-watch.sh`) | VL805 xHCI over PCIe | path **matches**: `/sys/block/sda -> …/1000120000.pcie/1f00200000.usb/xhci-hcd.0/usb2/2-1/…` (RP1 behind PCIe, still `/usb2/`). The stick (`0781:5581` SanDisk Ultra) is on the USB 3 bus. End to end is S6.1 |
| A9 | unbind/bind drills use path `1-1.2` (docs/13 S4.10/S4.11) | internal hub `1-1.x` | Phase 2. Confirmed: no internal hub; four root buses (`usb1`–`usb4`, two xHCI controllers), devices appear as `N-1` (the stick is `2-1`) |
| A10 | `healthwatch.sh` USB entry count `^[0-9]+-[0-9.]+$` | — | **no change**: the count is only logged (`usb=` field), never compared, so there is no baseline to move. Survey value: 1 (the stick alone) |
| A11 | `eth0`/`wlan0` (`netshim`, `RB_NETALIAS_*`) | — | **verified, no change**: `lo`, `eth0` (down), `wlan0` (up) |
| A12 | `gpioshim.c` stubs `/dev/gpiodrv` | not real GPIO | no change: it never touches Pi GPIO |
| A13 | timing-dependent crash fixed by `patch-rbp-nopc.py`; spontaneous exits (S3.7) | Pi 4 speed | TODO: soak result |
| A14 | power: no under-voltage | controller dropped once (S4.4) | `throttled=0x0`, EXT5V 5.13 V, 51 °C, with the stick only. TODO: again with mouse + keyboard, and after the soak |
| A16 | default controller row is `flx4` | FLX4 attached | `RB_MIDI_MAP=kbd` in `rb.local.conf` for Phase 1 |
| A17 | (new on the Pi 5) the board has a power button, `pwr_button`, an `EV_KEY` input device | — | **no change.** `evdev_io.c` opens every `EV_KEY` node read-only and never grabs it; its comment names a gpio-keys power button as expected, and an unmapped `KEY_POWER` is dropped by the map. Pressing the button still reaches logind as usual (it powers the board off) |

## Bring-up gates, in order

Each row is a gate for the next one, as in docs/13.

| Step | Check | Pi 5 result |
|---|---|---|
| S0.1 | `make -C scripts/shims RX3=… check` — only `GLIBC_2.4/2.7` | **pass, 2026-10-13** (on the build VM, `port/pi5` @ 8e71d73 plus an uncommitted one-line `.gitignore` edit; nothing that is built differs from 7755180): every shim `GLIBC_2.4`, plus `2.7` on knobshim and netshim; every undefined symbol resolved, `fstat` dangling only in fbshim as allowed. `make test` green under `qemu-arm` 7.2.22: e.g. `test_point` 123, `test_audio` 623, `test_midi` 173, `test_flx4` 4127, `test_kbd` 375, `test_menu` 13513 checks, `test_evdev` 5 and `test_cursor_dev` 14 scenarios, 0 failures in every suite |
| S0.2 | patch chain hashes reproduce (`4f2efcfc` → `3dda2d4e` → `97aa2223` → `99040424`) | **pass, 2026-10-13**: stock `4f2efcfc0c9e3f539289f863acfddcc6`, `rbp-audio` `3dda2d4e10187a75bfc16a7b4f16f192`, and `build-chroot.sh`'s depth stage reads `97aa2223c4ca5906b66389420f29d03f` and writes `990404244853a7d613be3d469ad1bc1d` (`RB_FB_LIE_BPP=32`) |
| S0.3 | `scripts/build-chroot.sh`; tarball's `libdirectfb_fbdev.so` matches `work/dfb`'s | **pass, 2026-10-13**: `rbpi4b-pi4.tgz` 33 MB, sha256 `ac4d8d940d79ba2c1902aad0c843cde978ea9a3f6048b499a67d38a14b78b881`. Its `usr/lib/directfb-1.4-6/systems/libdirectfb_fbdev.so` = `work/dfb`'s (`ad0bed56…489e`); `directfbrc` names `module-dir=/usr/lib/directfb-1.4-6`; `rb.conf`, both `TouchCalib_*.dat` and all five shims present. The tarball **also** carries the RX3 rootfs's stock module under `directfb-1.4-0` (unused), so a wildcard `tar -xzO '*systems/libdirectfb_fbdev.so'` concatenates both and never matches: name the member by its full path |
| S1.1 | `getconf LONG_BIT` = 32, `dpkg --print-architecture` = `armhf`, `32-bit EL0 Support`, **`PAGESIZE` = 4096** | **pass, 2026-10-13**: 32, `armhf`, `CPU features: detected: 32-bit EL0 Support`, 4096 (the chroot half is S1.5) |
| S1.2 | `/dev/fb0`, `/dev/snd/seq`, `/dev/input/event*` present | **pass, 2026-10-13**: `/dev/fb0` (29,0), `/dev/snd/seq` (116,1), `/dev/input/event0`–`event4`. No USB keyboard or mouse was attached yet |
| S1.3 | `fbdump` reading (pre-recipe) | **recorded 2026-10-13**: 1920×1080, 16 bpp RGB565, `line_length` 3840, `smem_len` 4147200 (one page). fbdump's verdict: memcpy format; a 1280×800 image fits 1:1 with bars of 320 columns and 140 rows. The full output is in `pi5-survey.txt` |
| S1.5 | chroot `echo ok`; `install.sh doctor` clean | TODO: unverified |
| S2.1 | `video=HDMI-A-1:1280x800@60` survives in `/proc/cmdline`; `fbdump` after reboot | TODO: unverified |
| S2.2 | launch; the `PRESENT:` line; the UI renders | TODO: unverified |
| S2.3 | no tearing (`off`, then `RB_DFB_PRESENT=convert`) | TODO: unverified |
| S2.4 | idle 10 min: no console text or cursor over the UI | TODO: unverified |
| S3.1 | `tools/evdevdump --list` finds the mouse and the keyboard; `/tmp/pointsrc.log` names the mouse | TODO: unverified |
| S3.3 (mouse) | the cursor follows the mouse; clicks on a browser row, a tab and a deck control act where the cursor is | TODO: unverified (upstream never ran this half by hand) |
| K1 | `↑`/`↓` move the selection (record the **direction**), `Enter` opens, `Backspace`/`Esc` go back; keys logged with `RB_KNOB_VERBOSE=1` | TODO: unverified |
| S6.1 | a rekordbox-exported stick shows as USB 1 with label and track count | TODO: unverified |
| K2 | browse the stick, `1` loads into deck 1, `space` plays: the waveform moves (silently) | TODO: unverified |
| S10.x | only if the monitor is not 1280×800: the UI scales correctly | TODO / n/a |
| S11 | boot time with the boot trim | TODO: unverified |
| Soak | ≥ 1 h playing silently with some browsing: `rbp` up, `get_throttled` = `0x0`, no `crash.log` entries | TODO: unverified |
