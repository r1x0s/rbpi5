# lib.sh -- shared by the scripts in this directory. Source it, do not run it.
# shellcheck shell=sh

# The checkout's root; the build container sees it at /w.
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
WORK="$ROOT/work"
IMAGE="${IMAGE:-rbpi5-build}"

say() { printf '\033[1m==> %s\033[0m\n' "$*"; }
warn() { printf '\033[33mWARNING: %s\033[0m\n' "$*" >&2; }
die() { printf '\033[31mERROR: %s\033[0m\n' "$*" >&2; exit 1; }

# pi.env: optional for the workstation-only steps, required for anything that
# talks to the Pi (need_pi).
load_env() {
    if [ -f "$ROOT/pi.env" ]; then
        # shellcheck disable=SC1091
        . "$ROOT/pi.env"
    fi
    ASSETS="${ASSETS:-extracted}"
    case "$ASSETS" in /*) ;; *) ASSETS="$ROOT/$ASSETS" ;; esac
}

need_docker() {
    command -v docker >/dev/null || die "Docker is needed on the workstation (https://docs.docker.com/engine/install/)"
    docker info >/dev/null 2>&1 || die "docker is installed but not usable by $(id -un) (is the daemon running, are you in the docker group?)"
}

# Build the image once; docker's cache makes later runs instant.
build_image() {
    need_docker
    say "build environment (docker image $IMAGE)"
    docker build -q -t "$IMAGE" "$ROOT/pi5/docker" >/dev/null || die "docker build failed"
}

# in_docker [-root] <cmd...>: run in the build image with the checkout at /w and
# the assets at /assets. As the calling user unless -root.
in_docker() {
    user="-u $(id -u):$(id -g)"
    if [ "${1:-}" = -root ]; then user=""; shift; fi
    mkdir -p "$ASSETS" "$WORK"
    # shellcheck disable=SC2086
    docker run --rm $user -e HOME=/tmp -e HOST_UID="$(id -u)" -e HOST_GID="$(id -g)" \
        -e QEMU_LD_PREFIX=/assets/XDJRX3-rootfs \
        -v "$ROOT:/w" -v "$ASSETS:/assets" -w /w "$IMAGE" "$@"
}

need_pi() {
    [ -f "$ROOT/pi.env" ] || die "no pi.env. Run: cp pi.env.example pi.env and fill it in"
    case "${PI_HOST:-}" in ""|*"<"*) die "PI_HOST is not set in pi.env" ;; esac
    mkdir -p "$HOME/.ssh"
    SSH_OPTS="-o ControlMaster=auto -o ControlPath=$HOME/.ssh/cm-rbpi5-%r@%h:%p -o ControlPersist=120"
    if [ -n "${PI_KEY:-}" ]; then
        case "$PI_KEY" in "~/"*) PI_KEY="$HOME/${PI_KEY#\~/}" ;; esac
        SSH_OPTS="$SSH_OPTS -i $PI_KEY"
    fi
}
pi() { ssh $SSH_OPTS "$PI_HOST" "$@"; }
pi_t() { ssh -t $SSH_OPTS "$PI_HOST" "$@"; }
pi_cp() { scp -q $SSH_OPTS "$@"; }
