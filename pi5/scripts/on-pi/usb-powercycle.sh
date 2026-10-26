#!/bin/sh
# usb-powercycle.sh -- at boot, power-cycle the Pi 5's USB ports when no DJ
# controller has shown up, so that it enumerates.
#
# Measured 2026-10-23 on the Pi 5 with a DDJ-FLX4 plugged straight into a blue
# port: across four boots (reboot and power-button cycles alike) the FLX4 never
# enumerated -- its port read "not attached" -- until it was replugged by hand.
# Neither a port disable/enable through sysfs, nor unbinding and rebinding the
# xHCI controller, nor a single-port uhubctl cycle brought it back; switching
# VBUS off on all four root hubs for 4 s did, at once. VBUS is on long before
# the kernel's xHCI is up, and the FLX4 apparently gives up on a host that does
# not answer and waits for its power to come back.
#
# Everything on USB drops for a few seconds (the touch panel, a media stick), so
# this runs once, early, before the menu or any system uses them.
CARDS="${RB_USB_CARDS:-DDJFLX4 DDJGRV6}"
OFF_S="${RB_USB_OFF_S:-3}"

have_controller() {
    for c in $CARDS; do
        grep -q "\[$c *\]" /proc/asound/cards 2>/dev/null && return 0
    done
    return 1
}

wait_controller() {      # $1 = seconds
    n=0
    while [ "$n" -lt "$1" ]; do
        have_controller && return 0
        sleep 1; n=$((n + 1))
    done
    have_controller
}

command -v uhubctl >/dev/null || { echo "usb-powercycle: no uhubctl, skipped"; exit 0; }
if wait_controller 2; then
    echo "usb-powercycle: controller present, nothing to do"; exit 0
fi
echo "usb-powercycle: no controller on USB, cycling power on all ports for ${OFF_S}s"
for l in 1 2 3 4; do uhubctl -l "$l" -a off >/dev/null 2>&1; done
sleep "$OFF_S"
for l in 2 4 1 3; do uhubctl -l "$l" -a on >/dev/null 2>&1; done
if wait_controller 8; then
    echo "usb-powercycle: controller up: $(grep -oE "\[DDJ[A-Z0-9]+ *\]" /proc/asound/cards | head -1)"
else
    echo "usb-powercycle: still no controller (none plugged in?)"
fi
exit 0
