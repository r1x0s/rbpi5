# Assets: what you supply

**English** · [Русский](assets.ru.md)

`make assets SRC=<dir>` turns your **already extracted** XDJ-RX3 **v1.20** tree
into the four inputs the build needs. It does not obtain, decrypt or download
anything (see [NOTICE.md](../../NOTICE.md)).

## Input

`<dir>` is the directory that holds:

```
<dir>/
├── gui/
│   ├── fontdata/
│   └── imagedata/
└── images/
    ├── rootfs.cramfs
    ├── gui.tar.gz
    ├── pdj.tar.gz        (holds pdj/rbp, the stock player)
    └── release.txt       (1.20)
```

The vendor publishes the firmware update file on the XDJ-RX3 support page; how
you get from it to this tree is outside this project and your responsibility.

## Output (`extracted/`, ignored by git)

```
extracted/
├── XDJRX3/gui/       a copy of the tree's gui/
├── XDJRX3-gui/       images/gui.tar.gz unpacked (pset/, system/)
├── XDJRX3-rootfs/    images/rootfs.cramfs unpacked (the soft-float userland)
└── rbp-audio         pdj/rbp + the interoperability patches (tools/patch-rbp)
```

Checks on the way: the stock `rbp` must have md5
`4f2efcfc0c9e3f539289f863acfddcc6` (v1.20) and the result
`3dda2d4e10187a75bfc16a7b4f16f192`. Another firmware version does not fit: the
patches are tied to v1.20's addresses. Details: [docs/04-firmware-assets.md](../04-firmware-assets.md),
[tools/patch-rbp/PATCHES.md](../../tools/patch-rbp/PATCHES.md).

The rootfs is unpacked as root inside the build container (it carries device
nodes) and handed back to your user. The shims link against its libraries;
nothing from it runs on the workstation.
