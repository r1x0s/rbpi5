#!/bin/sh
# pi-setup.sh -- one-time preparation of a Raspberry Pi 5 running Raspberry Pi
# OS Lite (64-bit or 32-bit, trixie/bookworm). Idempotent; safe to re-run after
# changing pi.env. Reboots are left to you (it says when one is needed).
#
# What it changes on the Pi (backups next to each file as *.sdj-bak, once):
#   /boot/firmware/config.txt   [pi5] kernel=kernel8.img  (4 KB pages: the chroot needs them)
#   /boot/firmware/cmdline.txt  video=<connector>:<mode>[,rotate=180] + console off the panel
#   /opt/rblive4/rb.local.conf  controller map, touch axes, boot trim
#   packages                    gcc make python3 uhubctl
#   rblive4-usb.service         USB power-cycle at boot for a bus-powered FLX4 (USB_POWERCYCLE=1)
#   default target              multi-user (no desktop fighting for the display)
set -eu
. "$(dirname "$0")/lib.sh"
load_env
need_pi

PANEL_CONNECTOR="${PANEL_CONNECTOR:-HDMI-A-1}"
PANEL_MODE="${PANEL_MODE:-1280x800@60}"
PANEL_ROTATE="${PANEL_ROTATE:-0}"
MIDI_MAP="${MIDI_MAP:-auto}"
case "$PANEL_ROTATE" in 0|180) ;; *) die "PANEL_ROTATE must be 0 or 180" ;; esac
case "$PANEL_CONNECTOR" in HDMI-A-1|HDMI-A-2) ;; *) die "PANEL_CONNECTOR must be HDMI-A-1 or HDMI-A-2" ;; esac

say "Pi: $PI_HOST"
pi 'cat /proc/device-tree/model; echo' | tr -d '\0' | grep -q 'Raspberry Pi 5' ||
    warn "this does not report itself as a Raspberry Pi 5; continuing anyway"

pi "rm -rf /tmp/rbpi5-setup"
pi_cp -r "$ROOT/pi5/scripts/on-pi" "$PI_HOST:/tmp/rbpi5-setup"
pi_t "sudo PANEL_CONNECTOR='$PANEL_CONNECTOR' PANEL_MODE='$PANEL_MODE' PANEL_ROTATE='$PANEL_ROTATE' MIDI_MAP='$MIDI_MAP' USB_POWERCYCLE='${USB_POWERCYCLE:-1}' sh /tmp/rbpi5-setup/setup.sh"

pi 'test -f /tmp/rbpi5-reboot-needed' || exit 0
if [ "${AUTO_REBOOT:-}" != 1 ]; then
    printf 'Reboot the Pi now? [Y/n] '
    read -r ans || ans=n
    case "$ans" in ""|y|Y|д|Д) ;; *) die "reboot the Pi yourself before make deploy: ssh $PI_HOST sudo reboot" ;; esac
fi
say "rebooting the Pi"
pi_t 'sudo systemctl reboot' || true
ssh -O exit $SSH_OPTS "$PI_HOST" 2>/dev/null || true
sleep 15
i=0
until ssh -o ConnectTimeout=5 $SSH_OPTS "$PI_HOST" true 2>/dev/null; do
    i=$((i + 1)); [ "$i" -lt 36 ] || die "the Pi did not come back over ssh in 3 minutes"
    sleep 5
done
say "the Pi is back: $(pi 'uname -r; getconf PAGESIZE' | tr '\n' ' ')(page size must be 4096)"
