#!/bin/sh
# deploy.sh -- build on the build machine and put the result on the unit, in one
# command. Run from anywhere inside the checkout, on the build machine (in this
# fork: `vm`, the OrbStack machine rbdev), never on the Pi.
#
#   tools/deploy.sh shims    build + test the shims, copy them to the unit and
#                            restart the player. Seconds; no tarball, no reboot.
#                            Enough for any change under scripts/shims/ (a
#                            controller map, say): start-rb.sh copies the
#                            deploy-root shims into the chroot at every launch.
#   tools/deploy.sh full     build + test, build the whole tarball
#                            (scripts/build-chroot.sh) and install it. install.sh
#                            stops the player, releases the chroot's mounts before
#                            it unpacks, and starts the player again (C-039), so
#                            this is safe on a running unit and needs no reboot.
#                            Needed for anything outside scripts/shims/: rb.conf
#                            defaults, start-rb.sh and the other device scripts,
#                            DirectFB, the player patches, the chroot itself.
#
# Options (after the mode):
#   --no-test   skip `make check test` (the build still runs)
#   --dirty     allow uncommitted changes to tracked files. Without it the
#               script refuses, because a deploy should name a commit.
#
# Which unit: tools/unit.local.conf (not checked in; unit.local.conf.example is
# the shape), the same file tools/fbwatch.py reads:
#   RB_UNIT_HOST=neo@192.168.0.237
#   RB_UNIT_KEY=~/.ssh/id_ed25519        (optional)
# The user there needs sudo. One SSH connection is shared by every step
# (ControlMaster), so a password-only login asks for the SSH password once and
# for the sudo password once.
#
# Build inputs (defaults are this fork's layout on vm; override in the
# environment): RX3=/home/neo/extracted   DFB=/home/neo/work/dfb
#
# Everything the run prints is also kept in work/deploy-<UTC time>.log.

set -eu

die() { echo "deploy: ERROR: $*" >&2; exit 1; }
say() { echo "deploy: $*"; }

MODE="${1:-}"
case "$MODE" in
  shims|full) shift ;;
  -h|--help|"") awk 'NR > 1 && /^#/ { sub(/^# ?/, ""); print; next } NR > 1 { exit }' "$0"; exit 0 ;;
  *) die "unknown mode '$MODE' (shims or full; --help for details)" ;;
esac
TEST=1
DIRTY=0
for a in "$@"; do
  case "$a" in
    --no-test) TEST=0 ;;
    --dirty)   DIRTY=1 ;;
    *) die "unknown option '$a'" ;;
  esac
done

REPO="$(cd "$(dirname "$0")/.." && pwd)"
cd "$REPO"
[ -d scripts/shims ] && [ -d scripts/device ] || die "$REPO does not look like the checkout"

RX3="${RX3:-/home/neo/extracted}"
DFB="${DFB:-/home/neo/work/dfb}"
ROOTFS="$RX3/XDJRX3-rootfs"
[ -d "$ROOTFS" ] || die "no rootfs at $ROOTFS (set RX3=<the decrypted assets>)"

# --- the unit ---------------------------------------------------------------
# KEY=VALUE lines are read, not sourced: the file is data.
CONF="$REPO/tools/unit.local.conf"
[ -f "$CONF" ] || die "no $CONF. Create it from tools/unit.local.conf.example:
    cp tools/unit.local.conf.example tools/unit.local.conf
  and set RB_UNIT_HOST=<user>@<address> (here: neo@192.168.0.237)."
conf_get() { sed -n "s/^[[:space:]]*$1=//p" "$CONF" | tail -1; }
HOST="$(conf_get RB_UNIT_HOST)"
KEY="$(conf_get RB_UNIT_KEY)"
case "$HOST" in ""|*"<"*) die "RB_UNIT_HOST is not set in $CONF" ;; esac
case "$KEY" in "~/"*) KEY="$HOME/${KEY#\~/}" ;; esac

mkdir -p "$HOME/.ssh"
CM="$HOME/.ssh/cm-rbdeploy-%r@%h:%p"
SSH_OPTS="-o ControlMaster=auto -o ControlPath=$CM -o ControlPersist=120"
if [ -n "$KEY" ]; then SSH_OPTS="$SSH_OPTS -i $KEY"; fi
# ssh options must come BEFORE the host: anything after it is the remote command.
rssh()  { ssh $SSH_OPTS "$HOST" "$@"; }
rssh_t() { ssh -t $SSH_OPTS "$HOST" "$@"; }
rscp() { scp -q $SSH_OPTS "$@"; }

# --- log everything from here on --------------------------------------------
mkdir -p work
LOG="work/deploy-$(date -u +%Y%m%dT%H%M%SZ).log"
if [ -z "${DEPLOY_LOGGING:-}" ]; then
  # Re-run under tee so the log has exactly what the screen has. The exit status
  # is carried through a file because a pipeline only returns tee's.
  export DEPLOY_LOGGING="$LOG"
  rc_file="$(mktemp)"
  # `&& rc=0 || rc=$?` and not a bare call: under set -e a failing call would end
  # the group before its status is written, and the exit below would then be empty.
  { sh "$0" "$MODE" "$@" && rc=0 || rc=$?; echo "$rc" > "$rc_file"; } 2>&1 | tee "$LOG"
  rc=$(cat "$rc_file"); rm -f "$rc_file"
  echo "deploy: log kept in $LOG"
  exit "$rc"
fi
LOG="$DEPLOY_LOGGING"

# --- what is being deployed -------------------------------------------------
BRANCH="$(git rev-parse --abbrev-ref HEAD)"
COMMIT="$(git rev-parse --short HEAD)"
if [ -n "$(git status --porcelain --untracked-files=no)" ]; then
  if [ "$DIRTY" = 1 ]; then
    say "WARNING: uncommitted changes to tracked files are included (--dirty)"
    COMMIT="$COMMIT+dirty"
  else
    git status --short --untracked-files=no >&2
    die "uncommitted changes to tracked files (above). Commit them, or pass --dirty"
  fi
fi
say "mode $MODE, $BRANCH @ $COMMIT, unit $HOST, $(date '+%Y-%m-%d %H:%M %Z')"

# --- test --------------------------------------------------------------------
if [ "$TEST" = 1 ]; then
  say "make check test (output in work/deploy-test.log)"
  if ! make -C scripts/shims RX3="$ROOTFS" check test > work/deploy-test.log 2>&1; then
    tail -30 work/deploy-test.log >&2
    die "make check test failed (last lines above; whole output in work/deploy-test.log). Nothing was deployed"
  fi
  # `make test` stops at the first failing program, so reaching here means every
  # one passed; the per-program summaries are what is worth seeing.
  grep -E 'checks, [0-9]+ failures|^ok: ' work/deploy-test.log | sed 's/^/  /' || true
  say "all tests passed ($(grep -c '^-- ' work/deploy-test.log) test runs)"
else
  say "tests skipped (--no-test)"
fi

# --- build -------------------------------------------------------------------
STAGE=work/deploy-stage
rm -rf "$STAGE"
mkdir -p "$STAGE"
cp -r scripts/device "$STAGE/device"

# The shims are built and staged in BOTH modes. In `full` mode too, because the
# deploy-root copies on the unit (/opt/rblive4/*.so) override the chroot's at
# every launch: after one `shims` deploy, a tarball alone would go on running
# the shims of that earlier deploy. Staged under shims/, they are what
# install.sh's artifact step installs over the deploy-root copies (it looks in
# ../shims beside itself). And build-chroot.sh only builds shims that are
# MISSING, so building them here first is also what keeps it from packing stale
# ones (e.g. with --no-test).
make -C scripts/shims RX3="$ROOTFS" all > work/deploy-build.log 2>&1 || {
  tail -30 work/deploy-build.log >&2; die "shim build failed (work/deploy-build.log)"; }
mkdir -p "$STAGE/shims"
for a in fbshim.so knobshim.so audioshim.so crashcatch.so netshim.so controllers_cli; do
  [ -f "scripts/shims/$a" ] || die "scripts/shims/$a was not built"
  cp "scripts/shims/$a" "$STAGE/shims/$a"
done
say "built shims:"
(cd "$STAGE/shims" && sha256sum *) | awk '{ printf "  %s  %.16s…\n", $2, $1 }'

if [ "$MODE" = shims ]; then
  REMOTE_CMD="sudo sh /tmp/rbdeploy/device/install.sh shims /tmp/rbdeploy/shims"
else
  say "building the tarball (scripts/build-chroot.sh; output in work/deploy-build.log)"
  RX3="$RX3" DFB="$DFB" scripts/build-chroot.sh >> work/deploy-build.log 2>&1 || {
    tail -30 work/deploy-build.log >&2; die "build-chroot.sh failed (work/deploy-build.log)"; }
  [ -f work/rbpi4b-pi4.tgz ] || die "build-chroot.sh did not produce work/rbpi4b-pi4.tgz"
  ln -f work/rbpi4b-pi4.tgz "$STAGE/rbpi4b-pi4.tgz" 2>/dev/null || cp work/rbpi4b-pi4.tgz "$STAGE/"
  say "tarball $(stat -c %s work/rbpi4b-pi4.tgz) bytes, sha256 $(sha256sum work/rbpi4b-pi4.tgz | cut -c1-64)"
  REMOTE_CMD="sudo sh /tmp/rbdeploy/device/install.sh /tmp/rbdeploy/rbpi4b-pi4.tgz"
fi

# --- copy --------------------------------------------------------------------
say "copying to $HOST:/tmp/rbdeploy"
rssh 'rm -rf /tmp/rbdeploy && mkdir -p /tmp/rbdeploy' || die "cannot reach $HOST over ssh"
rscp -r "$STAGE"/* "$HOST:/tmp/rbdeploy/"
if [ "$MODE" = full ]; then
  rsum="$(rssh 'sha256sum /tmp/rbdeploy/rbpi4b-pi4.tgz' | cut -c1-64)"
  lsum="$(sha256sum work/rbpi4b-pi4.tgz | cut -c1-64)"
  [ "$rsum" = "$lsum" ] || die "the tarball on the unit hashes $rsum, not $lsum: copy damaged, nothing installed"
  say "tarball arrived intact"
fi

# --- install + check, in ONE session so sudo asks once ------------------------
# The check waits for rbp to come up, then prints the lines that say the build
# is the one running: the shim maps line and the controller subscription, the
# kernel oops count (A18), and the service state.
say "installing on $HOST (sudo may ask for $HOST's password)"
rssh_t "$REMOTE_CMD"' || exit 1
  echo "deploy: waiting 12 s for the player..."; sleep 12
  echo "--- on the unit, after the deploy:"
  echo "  rblive4: $(systemctl is-active rblive4)"
  grep -hE "maps:|subscribed to|reading sequencer" /tmp/knobshim.log 2>/dev/null | sed "s/^/  /"
  echo "  kernel oops (Internal error) count: $(sudo dmesg | grep -c "Internal error")"
  exit 0' \
  || die "the install on the unit reported a failure (see above); the checks after it did not run"

say "done: $MODE deploy of $BRANCH @ $COMMIT to $HOST"
say "for the journal: commit $COMMIT, mode $MODE, log $LOG"
