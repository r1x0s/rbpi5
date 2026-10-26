# Sources and credits

**English** · [Русский](SOURCES.ru.md)

The projects and material this fork is built on. Thank you to their authors.

## Foundation

| Project | What it gave |
|---|---|
| [@i.erhan.es](https://instagram.com/i.erhan.es) | the original base of the rbpi4b code (as credited upstream) |
| [salesforcedoc/rbpi4b](https://github.com/salesforcedoc/rbpi4b) (MIT) | the whole approach: soft-float chroot + LD_PRELOAD shims, the `rbp` interoperability patches, the DirectFB patch, `install.sh` / `doctor.sh`, the controller table, the FLX4 map. This repository is its fork |
| [stableneo/rbpi5-grv6](https://github.com/stableneo/rbpi5-grv6) | the Pi 5 port: `kernel8.img` (4 KB pages), 16 bpp instead of 32 (oops in `iommu_dma_mmap`), `docs/port/pi5-bringup.md`; its `port/grv6` branch is the base of `pi5-flx4` |
| [mutlisensor/Rx3-flx4](https://github.com/mutlisensor/Rx3-flx4) | a sibling XDJ-RX3 + FLX4 port on the Pi 5 (Python bridge): ideas, the FLX4 keepalive |

Projects upstream names as its predecessors (see [NOTICE.md](NOTICE.md)): the
XDJ-RX3 → Denon Prime GO port *PrimeBox* (the `rbp` patcher, the DirectFB fbdev
patch, the shared shims), *rb2go* (a postmarketOS phone), *chromebit* (ASUS
Chromebit, RK3288), *freelive4* (SC Live 4).

## Build

| | |
|---|---|
| [deniskropp/DirectFB](https://github.com/deniskropp/DirectFB) | DirectFB 1.4.16, commit `3637392`, LGPL-2.1 |
| [deniskropp/flux](https://github.com/deniskropp/flux) | the `fluxcomp` IDL compiler DirectFB's build needs, commit `10ad2eb` |
| Debian bookworm `gcc-arm-linux-gnueabi`, `qemu-user` | the soft-float toolchain and the ARM test runs on the workstation |

## Controllers (MIDI, LEDs)

| | |
|---|---|
| [Mixxx: Pioneer-DDJ-FLX4.midi.xml](https://github.com/mixxxdj/mixxx/blob/main/res/controllers/Pioneer-DDJ-FLX4.midi.xml), [script](https://github.com/mixxxdj/mixxx/blob/main/res/controllers/Pioneer-DDJ-FLX4-script.js) | FLX4 MIDI reference, checked against measurements |
| [joergbeyer/dj-controller](https://github.com/joergbeyer/dj-controller) | a DDJ-GRV6 Mixxx mapping: GRV6 note/CC reference |
| [Mixxx forum: Working on a Pioneer DDJ-GRV6 mapping](https://mixxx.discourse.group/t/working-on-a-pioneer-ddj-grv6-mapping/33114) | GRV6 MIDI discussion |
| [GitHubuser5o5/mixxx-grv6-build](https://github.com/GitHubuser5o5/mixxx-grv6-build) | another GRV6 mapping build |
| [DDJ-GRV6 Operating Instructions (PDF)](https://downloads.support.alphatheta.com/manuals/dj-controllers/DDJ-GRV6/DDJ-GRV6_DRI1927A_manual.pdf), [product page](https://alphatheta.com/en/product/dj-controller/ddj-grv6/black/) | names and functions of the GRV6's controls |

Everything the maps mark *measured* was captured on real controllers
(`aseqdump`, `tools/grv6-capture.sh`, `tools/grv6-ledprobe.sh`); the tables and
fixtures are in `docs/15-flx4-midi.md`, `docs/port/grv6-midi.md` and
`scripts/shims/tests/`.

## Raspberry Pi

| | |
|---|---|
| [config.txt and boot options](https://www.raspberrypi.com/documentation/computers/configuration.html) | `kernel=`, `video=` in `cmdline.txt` |
| [Raspberry Pi OS](https://www.raspberrypi.com/software/operating-systems/) | the OS on the Pi (Lite 64-bit, trixie) |
| [mvp/uhubctl](https://github.com/mvp/uhubctl) | switching USB power at boot: the FLX4 does not enumerate on the Pi 5 otherwise |

## XDJ-RX3 firmware

The vendor publishes the firmware update file on the XDJ-RX3 support pages
([pioneerdj.com](https://www.pioneerdj.com/) / [alphatheta.com](https://alphatheta.com/)),
for example [this 2024 firmware notice](https://www.pioneerdj.com/ja/news/2024/xdj-xz-xdj-rx3-xdj-rr-and-xdj-rx2-firmware/).
This project expects an **already extracted** v1.20 tree. Obtaining and
decrypting it, and any keys, are outside the project and not described here;
that is the user's responsibility ([NOTICE.md](NOTICE.md)).
