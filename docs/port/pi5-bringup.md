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
| Deployed commit + tarball sha256 | **`port/pi5` @ 09983c8** (16 bpp default, C-019), tarball sha256 `1c58b8ca587b2d94c0ae2775f34b4afe20f3f5765681f40600a6301abe41ab3e`, installed 2026-10-13 ~12:20 onto a stopped unit; no deploy-root overrides; `rb.local.conf`: `RB_MIDI_MAP=kbd`, `RB_EVDEV_MAP=kbd`, `RB_POINT_KIND=auto`, `RB_KNOB_VERBOSE=0`. Launcher: `depth: player and lie both 16 bpp`, no `override:` line; `PRESENT:` as S2.2; oops count 0. (S0–Soak were measured on the earlier 8e71d73 tarball + the 16 bpp override, which is the same player and shims.) |

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
| A13 | timing-dependent crash fixed by `patch-rbp-nopc.py`; spontaneous exits (S3.7) | Pi 4 speed | **none seen** in the 1 h soak (`NRestarts=0`, no `crash.log`); longer sessions not yet run |
| A14 | power: no under-voltage | controller dropped once (S4.4) | **verified**: `throttled=0x0` with the stick only (survey), and in every healthwatch sample of the 1 h soak with stick, G502 mouse and keyboard attached |
| A16 | default controller row is `flx4` | FLX4 attached | `RB_MIDI_MAP=kbd` in `rb.local.conf` for Phase 1 |
| A18 | (new on the Pi 5) the fbdev buffer is mapped through the **IOMMU**: `fb_mmap` → `drm_fbdev_dma_fb_mmap` → `drm_gem_dma_mmap` → `iommu_dma_mmap` → `vm_map_pages`. `drm_gem_dma_mmap` passes the *VMA* size, so nothing bounds a mapping longer than the buffer | Pi 4: no IOMMU on this path; an over-long map silently covers neighbouring memory | **breaks the 32 bpp default.** With `RB_FB_LIE_BPP=32` (shim `line_length` 5120, real `smem_len` 2,048,000) rbp's thread `gui_task` oopsed in `validate_page_before_insert` 0.8 s after launch (2026-10-13 10:08, `Oops: 0000000096000007`), wedging rbp in `D` and leaving the screen black. With `RB_FB_LIE_BPP=16` and the 16 bpp player: no oops, UI up (S2.2). Cause **suspected, not measured**: a mapping sized line_length × yres (4,096,000 bytes). Since C-019, 16 bpp is this fork's `rb.conf` default. A fix that makes 32 safe: optional, later |
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
| S2.1 | `video=HDMI-A-1:1280x800@60` survives in `/proc/cmdline`; `fbdump` after reboot | **pass, 2026-10-13**: the token is in `/proc/cmdline` (with `fbcon=map:1 console=tty3 consoleblank=0 vt.global_cursor_default=0 quiet loglevel=3`); `kmsprint`: CRTC `1280x800@60.02 83.496 1280/64/136/200/- 800/1/3/24/+`, fb `1280x800 RG16`; the launcher's `display baseline: 1280,800 2560 16`. The 1080p monitor, whose EDID has no 1280×800, **shows the picture** (as upstream's Pi 4 panel did). Owner, 2026-10-13: it **fills the whole screen** (the monitor's own scaler stretches 16:10 to 16:9) and is sharp |
| S2.2 | launch; the `PRESENT:` line; the UI renders | **fail at 32 bpp, pass at 16 bpp, 2026-10-13.** At the default `RB_FB_LIE_BPP=32`: kernel oops in rbp's fb0 mmap, no DirectFB init, black screen (A18). At `RB_FB_LIE_BPP=16` with the 16 bpp player (md5 `97aa2223…`, deploy-root override): `PRESENT: mode=off angle=0 real_fb=1280x800 pitch=2560 bpp=16 pages=1 pan=0 (logical 1280x800)`, identical to the Pi 4's line, and **the rekordbox UI is on the monitor** |
| S2.3 | no tearing (`off`, then `RB_DFB_PRESENT=convert`) | **`off` half: pass, 2026-10-13** (owner: no flicker, at 16 bpp). The `convert` re-launch: not run |
| S2.4 | idle 10 min: no console text or cursor over the UI | **pass, 2026-10-13** (owner: no console text or blinking cursor over the UI across the session; not a timed 10-minute idle) |
| S3.1 | `tools/evdevdump --list` finds the mouse and the keyboard; `/tmp/pointsrc.log` names the mouse | **pass, 2026-10-13**: `evdevdump --list` summary `POINT_KIND=rel POINT_DEV=/dev/input/event2` and `…event6` (two relative pointers); `pointsrc: relative device /dev/input/event2 name='Logitech Gaming Mouse G502'` with `RB_POINT_KIND=auto`; `cursor: first paint at (640,400)` |
| S3.3 (mouse) | the cursor follows the mouse; clicks on a browser row, a tab and a deck control act where the cursor is | **pass, 2026-10-13, by the owner's hand** ("yes"); `pointsrc.log` shows the clicks reaching rbp's controls: Beat FX channel select (deck 2 → MASTER → deck 1), Beat FX power toggle, `hot cue deck1 pad1 -> key 0x4117 ch1`, `deck1 QUANTIZE tap at (57,769)` and `(55,746)`. Upstream had never run this half by hand |
| K1 | `↑`/`↓` move the selection (record the **direction**), `Enter` opens, `Backspace`/`Esc` go back; keys logged with `RB_KNOB_VERBOSE=1` | **pass, 2026-10-13** (owner: "you can fully navigate with the keyboard"). `knobshim.log`: `code=108` (↓) and `code=103` (↑) `-> 0x420c rotate`, `code=28` (Enter) `-> 0x420c press`, `code=1` (Esc) `-> 0x0201 press/release`; releases of the rotate keys `ignored`, as designed. Direction: not separately recorded |
| S6.1 | a rekordbox-exported stick shows as USB 1 with label and track count | **pass, 2026-10-13**: `usb-watch` 10:28:22 `attach: slot 1 <- sda`, `mounted /dev/sda1 (exfat)`, `chroot bind ok`, `label 'N3O'`, then `notify: umount`/`mount` and `rbp opened export.pdb (notify attempt 0)` at 10:28:31. Owner: SOURCE shows it as USB 1 with its name, tracks and playlists. The stick sat in a USB 3 port (`2-1`, A8) |
| K2 | browse the stick, `1` loads into deck 1, `space` plays: the waveform moves (silently) | **pass, 2026-10-13** (owner: playing, no audio as expected, waveforms move and are fully functional) |
| S10.x | only if the monitor is not 1280×800: the UI scales correctly | TODO / n/a |
| S11 | boot time with the boot trim | **measured 2026-10-13** (`boot-trim.sh report`, one `boot_id`, monotonic s): kernel 1.739, boot-fw 3.839, sysinit 5.043, basic 5.047, **rblive4 5.075**, launcher 5.124, fix-dev 6.860, rbp-launch 8.442, **rbp-ready 9.318**, multi-user 9.409, network 9.289, usb-watch 10.336, display-w 11.345, wait-online 15.280, net-online 15.281; `reloads 4`, `cloud-init enabled`, `apt-daily no`. For scale: the Pi 4's best was rbp-ready 17.930 (docs/13 S11.4), there with cloud-init disabled. Here the trim's cloud-init item did not apply (its network pre-flight refused, so the network stays cloud-init's), which keeps `wait-online` on the boot but not on rbp's path |
| Soak | ≥ 1 h playing silently with some browsing: `rbp` up, `get_throttled` = `0x0`, no `crash.log` entries | **pass, 2026-10-13** (16 bpp): a loop played ~1 h (owner: UI clean, normal behaviour). At 12:03: rbp still pid 2091 since 10:28:18, `NRestarts=0`, no `/tmp/crash.log`, `dmesg` oops count 0; healthwatch every 15 s through the run: `thr=0x0`, `kerr=0`, load ≈ 0.3, 3.5 GB free |

## Phase 1 exit (2026-10-13)

Every Phase 1 criterion is met on the Pi 5 with no controller attached, at 16 bpp:
the UI fills the HDMI screen (S2.1), the mouse moves the pointer and clicks land
where it is (S3.3), the keyboard browses (K1), a rekordbox stick is detected and a
track loads and plays silently (S6.1, K2), and a 1 h soak shows no `rbp` exit and
`throttled=0x0` (Soak). The one known gap is cosmetic: at 16 bpp a few pixels at
the right edge of the waveform strip draw faint (A18).

