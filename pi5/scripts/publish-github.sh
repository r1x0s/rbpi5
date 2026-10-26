#!/bin/sh
# publish-github.sh <github-user> [--public] -- put this repository on GitHub as
# <user>/rbpi5 (REPO_NAME=... for another name), the current branch as main.
#
# Private unless --public. With the GitHub CLI (`gh auth login` done) the
# repository is created here; without it the script asks you to create it empty
# on github.com first. Only committed work is pushed. Runs `make check` first and
# stops on any problem. Push over HTTPS instead of SSH: PUBLISH_HTTPS=1.
set -eu
. "$(dirname "$0")/lib.sh"

GH_USER="${1:-}"
[ -n "$GH_USER" ] || die "usage: make publish GH_USER=<github-user> [PUBLIC=1]"
VIS=--private
[ "${2:-}" = --public ] && VIS=--public
NAME="${REPO_NAME:-rbpi5}"
if [ "${PUBLISH_HTTPS:-0}" = 1 ]; then URL="https://github.com/$GH_USER/$NAME.git"
else URL="git@github.com:$GH_USER/$NAME.git"; fi

sh "$ROOT/pi5/scripts/check-git.sh"

if command -v gh >/dev/null && gh auth status >/dev/null 2>&1; then
    if gh repo view "$GH_USER/$NAME" >/dev/null 2>&1; then
        say "github.com/$GH_USER/$NAME exists"
    else
        say "creating github.com/$GH_USER/$NAME (${VIS#--})"
        gh repo create "$GH_USER/$NAME" "$VIS" \
            --description "rekordbox (Pioneer XDJ-RX3 player) on Raspberry Pi 5 with DDJ-FLX4 / DDJ-GRV6" >/dev/null
    fi
else
    say "no GitHub CLI (gh). Create an EMPTY repository on https://github.com/new:"
    echo "      $GH_USER/$NAME  (${VIS#--}, without README, .gitignore or license)"
    printf 'Press Enter when it exists... '
    read -r _
fi

git -C "$ROOT" remote get-url github >/dev/null 2>&1 &&
    git -C "$ROOT" remote set-url github "$URL" ||
    git -C "$ROOT" remote add github "$URL"
BRANCH="$(git -C "$ROOT" rev-parse --abbrev-ref HEAD)"
say "push $BRANCH -> github.com/$GH_USER/$NAME main"
git -C "$ROOT" push -u github "$BRANCH:main"

say "done:  git clone https://github.com/$GH_USER/$NAME"
[ "$VIS" = --private ] && echo "    (private: make it public in the repository's Settings when you are ready)"
exit 0
