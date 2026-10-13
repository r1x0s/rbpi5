#!/bin/sh
# pi5-survey.sh -- READ-ONLY inventory of the facts rbpi4b hardcodes or measured
# on the Pi 4, taken on a new board (written for the Raspberry Pi 5 port).
#
#   sudo sh tools/pi5-survey.sh                 # writes ./pi5-survey.txt
#   sudo sh tools/pi5-survey.sh /tmp/out.txt    # or wherever you say
#
# Run it as root: dmesg, some /sys/kernel/debug-free DRM files and vcgencmd are
# unreadable otherwise, and an unreadable file reads like an absent one.
#
# It changes nothing: no modprobe, no mode-set, no mount, no write outside the
# one output file. If `fbdump` (tools/fbdump.c, built on the Pi with
# `gcc -O2 -static -o fbdump tools/fbdump.c`) sits in the current directory or
# is named by FBDUMP=..., its output is appended too -- that is the single most
# important display measurement (docs/13 "Measuring the framebuffer").
#
# Every section is labelled with the audit row (PORTING.md section 5.4, A1..A16)
# it answers, so the output can be read straight into docs/port/pi5-bringup.md.
# A command that is missing on this image prints its own error into the file
# rather than aborting the survey: an absent tool is itself a fact worth having.
# The output is meant to be committed, so the board serial, MAC addresses, the
# root PARTUUID and the Imager's instance id are left out of it.

OUT="${1:-./pi5-survey.txt}"
FBDUMP="${FBDUMP:-./fbdump}"

sec() { printf '\n== %s\n' "$*"; }

# /proc/device-tree values are raw: strings are NUL-terminated, numbers are
# big-endian u32. Print either without needing anything beyond od/tr.
dt_str() { [ -r "$1" ] && { tr '\0' ' ' < "$1"; echo; } | sed 's/ *$//' || echo "(absent: $1)"; }
dt_u32() { [ -r "$1" ] && od -An -tu1 -v "$1" |
	awk '{for(i=1;i<=NF;i++) v=v*256+$i} END{print v}' || echo "(absent: $1)"; }

{
echo "pi5-survey  $(date -u '+%Y-%m-%dT%H:%M:%SZ')  host=$(hostname)  uid=$(id -u)"
[ "$(id -u)" = "0" ] || echo "WARNING: not root -- dmesg/vcgencmd/DRM reads below may be incomplete"

sec "model (board identity)"
dt_str /proc/device-tree/model
grep -E '^(Revision|Serial|Model)' /proc/cpuinfo 2>&1 | sed 's/^Serial.*/Serial\t\t: (redacted)/'
free -m

sec "os"
head -4 /etc/os-release

sec "kernel  [A1]"
uname -a
# This file gets committed, so the unit-identifying tokens are masked: the MAC
# the firmware adds, the root partition's PARTUUID and the Imager's instance id.
echo "cmdline: $(sed -E -e 's/(macaddr=)[0-9A-Fa-f:]+/\1(redacted)/g' \
	-e 's/(PARTUUID=)[0-9A-Fa-f-]+/\1(redacted)/g' \
	-e 's/(ds=nocloud;i=)[^ ]+/\1(redacted)/g' /proc/cmdline)"

sec "pagesize  [A1: must be 4096 -- kernel=kernel8.img under [pi5]]"
getconf PAGESIZE

sec "userland  [A1/S1.1: expect 32 and armhf]"
getconf LONG_BIT
dpkg --print-architecture

sec "32-bit EL0 support  [A1: expect a line]"
dmesg 2>&1 | grep -i 'EL0' | head -3
grep -m1 -i '^Features' /proc/cpuinfo

sec "kernels on /boot/firmware  [open question 1]"
ls -l /boot/firmware/ 2>&1 | grep -E 'kernel|\.img' || ls /boot 2>&1

sec "config.txt (active lines, in order, section headers included)  [A7]"
grep -vE '^[[:space:]]*(#|$)' /boot/firmware/config.txt 2>&1

sec "power  [A14: get_throttled must be 0x0]"
vcgencmd get_throttled 2>&1
vcgencmd pmic_read_adc EXT5V_V 2>&1
vcgencmd get_config usb_max_current_enable 2>&1
printf 'device-tree chosen/power/max_current (mA, what the PSU negotiated): '
dt_u32 /proc/device-tree/chosen/power/max_current
printf 'device-tree chosen/power/usb_max_current_enable: '
dt_u32 /proc/device-tree/chosen/power/usb_max_current_enable
vcgencmd measure_temp 2>&1

sec "drm cards  [A3: which cardN is vc4?]"
ls -l /dev/dri/ 2>&1
for c in /sys/class/drm/card[0-9]*; do
	case "$c" in *-*) continue;; esac          # connectors are listed below
	[ -e "$c" ] || continue
	drv=$(basename "$(readlink -f "$c/device/driver" 2>/dev/null)" 2>/dev/null)
	of=$(dt_str "$c/device/of_node/compatible" 2>/dev/null | head -c 120)
	printf '%s -> driver=%s compatible=%s\n' "$c" "${drv:-?}" "${of:-?}"
done

sec "connectors  [A4: HDMI-A-1 / HDMI-A-2 names, status, offered modes]"
for s in /sys/class/drm/card*-*; do
	[ -e "$s/status" ] || continue
	printf '%s: status=%s enabled=%s\n' "$(basename "$s")" \
		"$(cat "$s/status" 2>/dev/null)" "$(cat "$s/enabled" 2>/dev/null)"
	[ -s "$s/modes" ] && sed 's/^/    mode /' "$s/modes" | head -12
done
command -v kmsprint >/dev/null 2>&1 && kmsprint 2>&1 | head -40 ||
	echo "(kmsprint not installed: sudo apt-get install kms++-utils, optional)"

sec "framebuffer  [A2: id, bpp, geometry, pages]"
cat /proc/fb
ls -l /dev/fb* 2>&1
for f in /sys/class/graphics/fb*; do
	[ -e "$f/name" ] || continue
	printf '%s name=%s virtual_size=%s bpp=%s stride=%s\n' "$f" \
		"$(cat "$f/name")" "$(cat "$f/virtual_size")" \
		"$(cat "$f/bits_per_pixel")" "$(cat "$f/stride" 2>/dev/null)"
done
dmesg 2>&1 | grep -iE 'fb0|drmfb|vc4|hdmi' | head -20

sec "fbdump /dev/fb0  [A2 / S1.3]"
if [ -x "$FBDUMP" ]; then
	"$FBDUMP" /dev/fb0 2>&1
else
	echo "(no fbdump at $FBDUMP -- build it: gcc -O2 -static -o fbdump tools/fbdump.c)"
fi

sec "alsa  [A5 (Phase 2): HDMI card ids]"
cat /proc/asound/cards 2>&1
aplay -l 2>&1

sec "sequencer and sound nodes  [S1.2]"
ls -l /dev/snd/ 2>&1
aconnect -l 2>&1 | head -20

sec "usb  [A8, A9, A10]"
lsusb 2>&1
lsusb -t 2>&1
for d in /sys/bus/usb/devices/*; do
	[ -f "$d/idVendor" ] || continue
	echo "$(basename "$d") $(cat "$d/idVendor"):$(cat "$d/idProduct") $(cat "$d/product" 2>/dev/null)"
done
printf 'healthwatch-style count (entries matching ^[0-9]+-[0-9.]+$): '
ls /sys/bus/usb/devices 2>/dev/null | grep -cE '^[0-9]+-[0-9.]+$'
for b in /sys/block/sd*; do
	[ -e "$b" ] && echo "$b -> $(readlink -f "$b")"
done

sec "net  [A11]  (names and state only: MAC addresses are left out of a committed file)"
ip -br link 2>&1 | awk '{print $1, $2}'

sec "input  [S3.1]"
grep -E '^(N|H):' /proc/bus/input/devices 2>&1
ls -l /dev/input/ 2>&1

sec "anything already deployed (an old rbpi4b install would be here)"
ls -la /opt/rblive4 2>&1 | head -20
systemctl is-enabled rblive4.service 2>&1
systemctl is-active rblive4.service 2>&1
} > "$OUT" 2>&1

echo "wrote $OUT ($(wc -l < "$OUT") lines)"
