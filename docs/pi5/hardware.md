# Hardware and Pi 5 specifics

**English** · [Русский](hardware.ru.md)

## The author's setup (everything was checked on it)

| | |
|---|---|
| Board | Raspberry Pi 5, 4 GB |
| OS | Raspberry Pi OS trixie **64-bit**, kernel `kernel8.img` (4 KB pages) |
| Panel | 7" HDMI 1280×800 on **HDMI-A-2**, upside down: `video=HDMI-A-2:1280x800@60,rotate=180` |
| Touch | USB, QinHeng `1a86:e5e3`, real multitouch |
| Controllers | DDJ-FLX4 (card `DDJFLX4`), DDJ-GRV6 (card `DDJGRV6`) |

## What matters on a Pi 5

- **4 KB pages.** The Pi 5 firmware boots `kernel_2712.img` (16 KB pages) by
  default; the XDJ-RX3 chroot (glibc 2.13) needs 4 KB, so `make pi-setup` adds,
  above the final `[all]` of `config.txt`:
  ```
  [pi5]
  kernel=kernel8.img
  ```
  Check: `getconf PAGESIZE` → `4096`.
- **16 bpp.** At 32 bpp the kernel oopses (`iommu_dma_mmap`) when `rbp` maps
  `/dev/fb0`, and the screen stays black. `rb.conf` defaults to `RB_FB_LIE_BPP=16`;
  leave it. Cosmetic: a few pale pixels at the right edge of the waveform band.
- **A 64-bit OS is fine.** The 32-bit soft-float chroot runs on the 64-bit kernel
  (32-bit EL0); `install.sh` tests that during the install.
- **USB power.** With the official 27 W PSU the ports give 1.6 A in total, without
  it 0.6 A. The FLX4 is bus-powered, so it needs that PSU (or a powered hub).
  **The GRV6 only on its own adapter.**
- **The FLX4 at boot: the bootloader.** With network install enabled (the Pi 5
  default) the bootloader initialises USB at power-on to look for a keyboard
  shortcut; that first contact leaves a DDJ-FLX4 stuck, and its port reads "not
  attached" after a cold start and a reboot alike until the controller's power
  drops (measured 2026-10-26). `make pi-setup` sets **`NET_INSTALL_ENABLED=0`** in
  the bootloader EEPROM (old config kept in `/var/lib/rbpi5/eeprom.conf.sdj-bak`);
  with it the FLX4 enumerates at 0.94 s either way. By hand:
  `sudo rpi-eeprom-config > e.conf; echo NET_INSTALL_ENABLED=0 >> e.conf;
  sudo rpi-eeprom-config --apply e.conf`, then reboot. The cost: no network-install
  screen from a USB keyboard at power-on. `BOOT_NET_INSTALL=1` in `pi.env` leaves
  the EEPROM alone; then `USB_POWERCYCLE=1` is the fallback (`rblive4-usb.service`:
  VBUS off on all root hubs once at boot, `uhubctl`), which also blanks a panel fed
  from the Pi's USB.
- **PipeWire** grabs the controller's MIDI and audio on an SSH login;
  `make pi-setup` masks its user units.
- **No HDMI audio**: the HDMI audio mirror is off (`RB_AUDIO_MIRROR_DEV=""`).
- **Other panel sizes.** `rbp` draws 1280×800 and is scaled to the panel.
  Downscaling to 0.75× and below loses thin lines and font strokes; 800×480 is
  small.

## Measured and not

| | |
|---|---|
| FLX4: sound, controls, LEDs, USB stick, touch | works (2026-10-23) |
| GRV6: sound, controls, LEDs, jog-centre indicator, CFX, QUANTIZE | works (2026-10-23) |
| GRV6 jog-centre indicator smoothness at 100 Hz | **not confirmed** |
| ≥ 1 h sets on each controller without a restart, xrun or freeze | **not confirmed** |
| DSI panels, other panel sizes | **not tried** |
| 32-bit Pi OS Lite on a Pi 5 | **not tried** by this kit (upstream's Pi 5 bring-up used it [stableneo]) |

The full Pi 5 bring-up record: [docs/port/pi5-bringup.md](../port/pi5-bringup.md);
the Pi 4 one: [docs/13-raspberrypi4.md](../13-raspberrypi4.md).
