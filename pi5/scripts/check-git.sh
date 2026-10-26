#!/bin/sh
# check-git.sh -- run before every `git push` (make check). Looks at what git
# would publish (the files tracked at HEAD):
#
#   ERROR  firmware/vendor file names, ELF binaries, files over 2 MB,
#          private keys, and the content of any file named in SECRET_FILES
#   WARN   private IP addresses (often a leftover Pi address)
#
#   SECRET_FILES="/path/to/a.key /path/to/b"   files whose content must not be
#                                              in any tracked file (only the
#                                              name of a matching file is printed)
set -eu
. "$(dirname "$0")/lib.sh"
errors=0 warnings=0
err() { printf '  \033[31mERROR\033[0m %s\n' "$*"; errors=$((errors + 1)); }
wrn() { printf '  \033[33mWARN \033[0m %s\n' "$*"; warnings=$((warnings + 1)); }

check_repo() {
    repo="$1"
    [ -d "$repo/.git" ] || [ -f "$repo/.git" ] || { err "$repo is not a git checkout"; return; }
    say "$(basename "$repo"): $(git -C "$repo" rev-parse --abbrev-ref HEAD) @ $(git -C "$repo" rev-parse --short HEAD)"
    [ -z "$(git -C "$repo" status --porcelain --untracked-files=no)" ] ||
        wrn "uncommitted changes to tracked files: they will NOT be pushed (git -C $repo status)"
    tmp=$(mktemp)
    git -C "$repo" ls-tree -r -z --name-only HEAD | tr '\0' '\n' > "$tmp"
    while IFS= read -r f; do
        p="$repo/$f"
        [ -f "$p" ] || continue             # submodule gitlinks, symlinks to nowhere
        case "$f" in
            *.UPD|*.upd|*.iso|*.img|*.img.xz|*.cramfs|*.squashfs|*.key|*.pem|*.vfsb|*.so|*.so.*) err "$f: firmware/key/binary file name" ;;
            */rbp|rbp|*/rbp-audio|*/rbp-nopc|*rootfs*.tar*) err "$f: player binary or rootfs name" ;;
        esac
        sz=$(stat -c %s "$p")
        [ "$sz" -le 2097152 ] || err "$f: $((sz / 1024)) KB (over 2 MB -- an asset?)"
        [ "$(head -c 4 "$p" | od -An -tx1 | tr -d ' \n')" != 7f454c46 ] || err "$f: ELF binary"
    done < "$tmp"
    (cd "$repo" && tr '\n' '\0' < "$tmp" | xargs -0 grep -lIE -- '-----BEGIN [A-Z ]*PRIVATE KEY-----' 2>/dev/null) |
        while IFS= read -r f; do echo "  ERROR $f: private key"; done | grep . && errors=$((errors + 1))
    for s in ${SECRET_FILES:-}; do
        [ -s "$s" ] || continue
        # Lines of 8+ characters only: an empty pattern line would match every file.
        pat=$(mktemp)
        awk 'length($0) >= 8' "$s" > "$pat"
        hits=""
        [ -s "$pat" ] && hits=$(cd "$repo" && tr '\n' '\0' < "$tmp" | xargs -0 grep -lF -f "$pat" 2>/dev/null || true)
        rm -f "$pat"
        [ -z "$hits" ] || err "content of $(basename "$s") found in: $hits"
    done
    ips=$(cd "$repo" && tr '\n' '\0' < "$tmp" | xargs -0 grep -noIE '\b(192\.168|10\.[0-9]{1,3}|172\.(1[6-9]|2[0-9]|3[01]))\.[0-9]{1,3}\.[0-9]{1,3}\b' 2>/dev/null | head -5 || true)
    [ -z "$ips" ] || wrn "private addresses (check they are examples):
$(echo "$ips" | sed 's/^/          /')"
    rm -f "$tmp"
}

check_repo "$ROOT"

if [ "$errors" -gt 0 ]; then
    die "$errors problem(s): fix them before pushing"
fi
say "ok: nothing that must stay local is tracked ($warnings warning(s))"
