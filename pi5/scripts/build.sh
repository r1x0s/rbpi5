#!/bin/sh
# build.sh [--no-test] [--shims] -- build everything on the workstation, in Docker.
#
#   1. the build image (docker/Dockerfile)
#   2. patched DirectFB 1.4.16 -> work/dfb        (once; delete work/dfb to redo)
#   3. the shims, `make check test`                (scripts/shims)
#   4. the chroot tarball -> work/rbpi4b-pi4.tgz   (scripts/build-chroot.sh)
#
# --shims stops after 3 and leaves the shims in work/shims (make shims deploys
# them without a new tarball). --no-test skips the tests.
set -eu
. "$(dirname "$0")/lib.sh"
load_env

TEST=1 SHIMS_ONLY=0
for a in "$@"; do
    case "$a" in
        --no-test) TEST=0 ;;
        --shims)   SHIMS_ONLY=1 ;;
        *) die "unknown option $a" ;;
    esac
done

[ -e "$ASSETS/XDJRX3-rootfs/lib/libc.so.6" ] && [ -f "$ASSETS/rbp-audio" ] ||
    die "no prepared assets in $ASSETS. Run first: make assets SRC=/path/to/XDJRX3"

build_image
mkdir -p "$WORK"

if [ "$SHIMS_ONLY" = 0 ]; then
    if [ -f "$WORK/dfb/lib/libdirectfb-1.4.so.0" ]; then
        say "DirectFB: already built (work/dfb)"
    else
        say "DirectFB 1.4.16 (a few minutes, once; log in work/cache/*.log)"
        in_docker sh /w/scripts/in-docker/dfb-build.sh > "$WORK/dfb-build.log" 2>&1 ||
            { tail -20 "$WORK/dfb-build.log" >&2; die "DirectFB build failed (work/dfb-build.log)"; }
    fi
fi

if [ "$TEST" = 1 ]; then
    say "shims: make check test (log in work/test.log)"
    in_docker make -C /w/scripts/shims RX3=/assets/XDJRX3-rootfs check test > "$WORK/test.log" 2>&1 ||
        { tail -30 "$WORK/test.log" >&2; die "tests failed (work/test.log)"; }
    say "tests: $(grep -c '^-- ' "$WORK/test.log") suites, $(grep -E 'failures' "$WORK/test.log" | grep -vc ' 0 failures') with failures"
fi

say "shims: build"
in_docker make -C /w/scripts/shims RX3=/assets/XDJRX3-rootfs all > "$WORK/shims.log" 2>&1 ||
    { tail -30 "$WORK/shims.log" >&2; die "shim build failed (work/shims.log)"; }
rm -rf "$WORK/shims" && mkdir -p "$WORK/shims"
for a in fbshim.so knobshim.so audioshim.so crashcatch.so netshim.so controllers_cli; do
    [ -f "$ROOT/scripts/shims/$a" ] || die "scripts/shims/$a was not built"
    cp "$ROOT/scripts/shims/$a" "$WORK/shims/"
done
[ "$SHIMS_ONLY" = 1 ] && { say "shims in work/shims. Next: make deploy-shims"; exit 0; }

say "chroot tarball (log in work/chroot.log)"
# As root in the container: the rootfs carries device nodes (dev/console, …),
# and only root may create them in the staging copy. The outputs are handed back.
in_docker -root sh -c 'RX3=/assets DFB=/w/work/dfb OUT=/w/work bash /w/scripts/build-chroot.sh; rc=$?
    chown -R "$HOST_UID:$HOST_GID" /w/work /w/scripts/shims; exit $rc' > "$WORK/chroot.log" 2>&1 ||
    { tail -30 "$WORK/chroot.log" >&2; die "build-chroot.sh failed (work/chroot.log)"; }
[ -f "$WORK/rbpi4b-pi4.tgz" ] || die "build-chroot.sh did not produce work/rbpi4b-pi4.tgz"
say "built work/rbpi4b-pi4.tgz ($(du -h "$WORK/rbpi4b-pi4.tgz" | cut -f1)). Next: make pi-setup (once), make deploy"
