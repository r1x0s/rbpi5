#!/bin/sh
# on-pi/setup.sh -- runs ON the Pi as root, copied there by pi5/scripts/pi-setup.sh.
# Env: PANEL_CONNECTOR PANEL_MODE PANEL_ROTATE MIDI_MAP BOOT_NET_INSTALL USB_POWERCYCLE.
# Idempotent.
# Tests (not root, no apt/systemctl): SDJ_TEST=1 SDJ_BOOT=<dir> SDJ_LOCAL=<file>.
set -eu
say() { echo "pi-setup: $*"; }
warn() { echo "pi-setup: WARNING: $*" >&2; }
[ "$(id -u)" = 0 ] || [ -n "${SDJ_TEST:-}" ] || { echo "run as root" >&2; exit 1; }

BOOT=${SDJ_BOOT:-/boot/firmware}
[ -f "$BOOT/config.txt" ] || BOOT=/boot
CONFIG="$BOOT/config.txt"
CMDLINE="$BOOT/cmdline.txt"
LOCAL=${SDJ_LOCAL:-/opt/rblive4/rb.local.conf}
REBOOT=0

backup_once() { [ -f "$1.sdj-bak" ] || cp -p "$1" "$1.sdj-bak"; }
# /boot/firmware is FAT with no journal: write a temp file, then mv over.
replace() { cat > "$1.sdj-new" && mv "$1.sdj-new" "$1"; }

# --- packages --------------------------------------------------------------
# gcc/make: install.sh builds vncserve on the unit. python3: boot screen and
# status page. uhubctl: only for the USB power-cycle fallback (USB_POWERCYCLE=1).
PKGS="gcc make python3"
[ "${USB_POWERCYCLE:-0}" = 1 ] && PKGS="$PKGS uhubctl"
missing=""
[ -n "${SDJ_TEST:-}" ] && PKGS=""
for p in $PKGS; do dpkg -s "$p" >/dev/null 2>&1 || missing="$missing $p"; done
if [ -n "$missing" ]; then
    say "installing:$missing"
    apt-get update -qq && apt-get install -y -qq $missing
else
    say "packages: all present"
fi

# --- 32-bit ARM support ----------------------------------------------------
# The RX3 chroot is 32-bit soft-float ARM. A 64-bit Pi OS kernel runs it if it
# has 32-bit EL0 support (the stock one does); install.sh tests it for real.
say "kernel $(uname -r), userland $(getconf LONG_BIT)-bit, $(dpkg --print-architecture)"

# --- config.txt: 4 KB pages ------------------------------------------------
# The Pi 5 firmware boots kernel_2712.img (16 KB pages) by default; the RX3
# glibc 2.13 chroot needs 4 KB pages -> kernel8.img. It has to sit ABOVE the
# final [all] section, or [all] would not be the last word on the rest.
if awk '/^\[/{s=$0} s=="[pi5]" && /^kernel=kernel8\.img/{f=1} END{exit !f}' "$CONFIG"; then
    say "config.txt: [pi5] kernel=kernel8.img already set"
else
    backup_once "$CONFIG"
    awk '{ l[NR]=$0 } /^\[all\]/{ a=NR }
         END { for (i=1; i<=NR; i++) {
                   if (i==a) print "[pi5]\nkernel=kernel8.img\n";
                   print l[i] }
               if (!a) print "\n[pi5]\nkernel=kernel8.img" }' "$CONFIG" | replace "$CONFIG"
    say "config.txt: added [pi5] kernel=kernel8.img"
    REBOOT=1
fi

# --- cmdline.txt: display mode, rotation, console off the panel ---------------
VIDEO="video=$PANEL_CONNECTOR:$PANEL_MODE"
[ "$PANEL_ROTATE" = 180 ] && VIDEO="$VIDEO,rotate=180"
WANT="$VIDEO fbcon=map:1 console=tty3 consoleblank=0 vt.global_cursor_default=0"
orig=$(cat "$CMDLINE")
out=""
placed=0
set -f
for t in $orig; do
    case "$t" in
        video=HDMI-A-1:*|video=HDMI-A-2:*)   # ours replaces it, in place, once
            [ "$placed" = 1 ] || out="$out $VIDEO"
            placed=1
            continue ;;
    esac
    out="$out $t"
done
for t in $WANT; do
    case " $out " in *" $t "*) ;; *) out="$out $t" ;; esac
done
set +f
out=${out# }
if [ "$out" = "$orig" ]; then
    say "cmdline.txt: already right ($VIDEO)"
else
    case "$out" in *root=*) ;; *) echo "pi-setup: refusing: root= would be lost from cmdline.txt" >&2; exit 1 ;; esac
    backup_once "$CMDLINE"
    printf '%s\n' "$out" | replace "$CMDLINE"
    say "cmdline.txt: $VIDEO, console moved off the panel"
    REBOOT=1
fi

# --- boot target, PipeWire ---------------------------------------------------
if [ -z "${SDJ_TEST:-}" ] && [ "$(systemctl get-default)" != multi-user.target ]; then
    systemctl set-default multi-user.target
    say "default target: multi-user (a desktop would fight rbp for the display)"
    REBOOT=1
fi
# On an ssh login PipeWire's user units grab the controller's MIDI and audio.
[ -n "${SDJ_TEST:-}" ] || for u in pipewire.socket pipewire.service pipewire-pulse.socket pipewire-pulse.service wireplumber.service; do
    [ -f "/usr/lib/systemd/user/$u" ] && systemctl --global mask "$u" >/dev/null 2>&1 && masked=1
done
[ "${masked:-0}" = 1 ] && say "PipeWire user units masked"

# --- bootloader: no network-install USB scan (the FLX4 at boot) --------------
# Measured 2026-10-26: with network install enabled (the Pi 5 default) the
# bootloader initialises USB at power-on to look for a keyboard shortcut, and that
# first contact leaves a DDJ-FLX4 stuck -- its port reads "not attached" after a
# cold start and a reboot alike, until the controller's power drops. With
# NET_INSTALL_ENABLED=0 it enumerates at 0.94 s either way. The EEPROM config is
# written by the bootloader at the next reboot; the old one is kept once in
# /var/lib/rbpi5/eeprom.conf.sdj-bak. BOOT_NET_INSTALL=1 leaves the EEPROM alone.
if [ -z "${SDJ_TEST:-}" ] && [ "${BOOT_NET_INSTALL:-0}" = 0 ]; then
    if ! command -v rpi-eeprom-config >/dev/null 2>&1; then
        warn "no rpi-eeprom-config: bootloader left alone (a DDJ-FLX4 may need a replug after boot)"
    elif rpi-eeprom-config 2>/dev/null | grep -q '^NET_INSTALL_ENABLED=0'; then
        say "bootloader: NET_INSTALL_ENABLED=0 already set"
    else
        mkdir -p /var/lib/rbpi5
        eeprom=$(mktemp)
        rpi-eeprom-config > "$eeprom"
        [ -f /var/lib/rbpi5/eeprom.conf.sdj-bak ] || cp "$eeprom" /var/lib/rbpi5/eeprom.conf.sdj-bak
        grep -v '^NET_INSTALL_ENABLED=' "$eeprom" > "$eeprom.new"
        echo NET_INSTALL_ENABLED=0 >> "$eeprom.new"
        if rpi-eeprom-config --apply "$eeprom.new" >/dev/null; then
            say "bootloader: NET_INSTALL_ENABLED=0 staged (written at the next reboot)"
            REBOOT=1
        else
            warn "bootloader: could not stage NET_INSTALL_ENABLED=0; a DDJ-FLX4 may need a replug after boot"
        fi
        rm -f "$eeprom" "$eeprom.new"
    fi
fi

# --- USB power-cycle at boot (rblive4-usb.service): FALLBACK, off by default ---
# Superseded by the bootloader setting above. For a unit whose EEPROM cannot be
# changed: VBUS off on all ports once at boot when no controller has shown up.
# It also switches off whatever else the Pi's USB powers (a USB-fed panel goes
# dark for a few seconds).
HERE=$(cd "$(dirname "$0")" && pwd)
if [ -z "${SDJ_TEST:-}" ]; then
    if [ "${USB_POWERCYCLE:-0}" = 1 ]; then
        install -D -m 755 "$HERE/usb-powercycle.sh" /usr/local/lib/rbpi5/usb-powercycle.sh
        install -m 644 "$HERE/rblive4-usb.service" /etc/systemd/system/rblive4-usb.service
        systemctl daemon-reload
        systemctl enable rblive4-usb.service >/dev/null 2>&1
        say "rblive4-usb.service: enabled (USB power-cycle at boot when no controller shows up)"
    elif [ -f /etc/systemd/system/rblive4-usb.service ]; then
        systemctl disable rblive4-usb.service >/dev/null 2>&1 || true
        rm -f /etc/systemd/system/rblive4-usb.service
        rm -rf /usr/local/lib/rbpi5
        systemctl daemon-reload
        say "rblive4-usb.service: removed (USB_POWERCYCLE=0)"
    fi
fi

# --- rb.local.conf: per-unit values (install.sh never overwrites this file) ----
mkdir -p "$(dirname "$LOCAL")"
[ -f "$LOCAL" ] || printf '# rb.local.conf -- this unit (pi5/scripts/pi-setup.sh writes the keys it owns)\n' > "$LOCAL"
set_kv() {   # set_kv KEY VALUE  (VALUE "-" -> remove the key)
    grep -v "^$1=" "$LOCAL" > "$LOCAL.new" || true
    [ "$2" = "-" ] || printf '%s=%s\n' "$1" "$2" >> "$LOCAL.new"
    mv "$LOCAL.new" "$LOCAL"
}
set_kv RB_MIDI_MAP "$MIDI_MAP"
# No speakers on a touch panel: rbp's HDMI audio mirror off.
set_kv RB_AUDIO_MIRROR_DEV '""'
# upstream's boot trim (boot-trim.sh) writes video=HDMI-A-1:1280x800@60; keep it
# only when that is exactly what this unit wants, else it would undo the above.
if [ "$PANEL_CONNECTOR" = HDMI-A-1 ] && [ "$PANEL_MODE" = 1280x800@60 ] && [ "$PANEL_ROTATE" = 0 ]; then
    set_kv RB_BOOT_TRIM 1
else
    set_kv RB_BOOT_TRIM 0
fi
# The kernel rotates the picture, the touch still reports panel-native coordinates.
if [ "$PANEL_ROTATE" = 180 ]; then
    set_kv RB_POINT_INVERT_X 1; set_kv RB_POINT_INVERT_Y 1
else
    set_kv RB_POINT_INVERT_X -; set_kv RB_POINT_INVERT_Y -
fi
say "rb.local.conf:"; sed 's/^/    /' "$LOCAL"

rm -f /tmp/rbpi5-reboot-needed
if [ "$REBOOT" = 1 ]; then
    touch /tmp/rbpi5-reboot-needed
    say "done. REBOOT NEEDED before the deploy"
else
    say "done, nothing that needs a reboot changed"
fi
