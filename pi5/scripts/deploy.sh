#!/bin/sh
# deploy.sh [full|shims|doctor] -- put the build on the Pi (pi.env).
#
#   full    (default) the chroot tarball through scripts/device/install.sh
#   shims   only work/shims (make shims): seconds, no tarball, rbp restarts
#   doctor  the read-only health check on the Pi (scripts/device/doctor.sh)
#
# One ssh connection is shared by every step, so a password login asks once for
# the ssh password and once for sudo.
set -eu
. "$(dirname "$0")/lib.sh"
load_env
need_pi
MODE="${1:-full}"
REMOTE=/tmp/rbpi5-deploy

stage_device() {
    pi "rm -rf $REMOTE && mkdir -p $REMOTE" || die "cannot reach $PI_HOST over ssh"
    pi_cp -r "$ROOT/scripts/device" "$PI_HOST:$REMOTE/device"
}

case "$MODE" in
    full)
        tgz="$WORK/rbpi4b-pi4.tgz"
        [ -f "$tgz" ] || die "no $tgz. Run first: make build"
        stage_device
        say "copying the tarball ($(du -h "$tgz" | cut -f1))"
        pi_cp "$tgz" "$PI_HOST:$REMOTE/"
        [ "$(pi "sha256sum $REMOTE/rbpi4b-pi4.tgz" | cut -c1-64)" = "$(sha256sum "$tgz" | cut -c1-64)" ] ||
            die "the tarball arrived damaged; nothing installed"
        say "installing (scripts/device/install.sh; sudo may ask for the password)"
        pi_t "sudo sh $REMOTE/device/install.sh $REMOTE/rbpi4b-pi4.tgz" || die "install.sh on the Pi failed (see above)"
        ;;
    shims)
        [ -f "$WORK/shims/knobshim.so" ] || die "no work/shims. Run first: make shims"
        stage_device
        pi_cp -r "$WORK/shims" "$PI_HOST:$REMOTE/shims"
        say "installing the shims, rbp restarts"
        pi_t "sudo sh $REMOTE/device/install.sh shims $REMOTE/shims" || die "shim install failed (see above)"
        ;;
    doctor)
        pi_t "sudo sh /opt/rblive4/install.sh doctor"
        exit 0
        ;;
    *) die "unknown mode $MODE (full | shims | doctor)" ;;
esac

say "rblive4 on the Pi: $(pi 'systemctl is-active rblive4' 2>/dev/null || true)"
[ "$MODE" = full ] && say "done. The first time: reboot the Pi (sudo reboot); rekordbox comes up by itself"
exit 0
