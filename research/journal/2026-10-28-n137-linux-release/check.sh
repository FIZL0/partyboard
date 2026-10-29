#!/usr/bin/env bash
# N137 check: unpack partyboard-linux-x86_64.tar.gz into a clean dir and play tools/netplay/party.script (a whole
# --match party, --turbo) to its end with PB_DISC, from a cwd outside the folder, in a fresh user dir.
#   check.sh host   [tar.gz]   on this machine
#   check.sh fedora [tar.gz]   in the pb-n137-fedora image (fedora:43 + Mesa, see README.md), on the real GPU,
#                              the hidden window on the host's Wayland socket
#   check.sh bullseye [tar.gz] in debian:bullseye (glibc 2.31, the floor): only that every library resolves and
#                              the game starts (bullseye's Mesa 20.3 has no driver for this GPU)
# Results in .n137-runs/<mode>/. Exit 0 when the run printed "script ended".
set -euo pipefail
cd "$(dirname "$0")/../../.."
root=$PWD
mode=${1:-host}
tgz=$(realpath "${2:-build/release/partyboard-linux-x86_64.tar.gz}")
d=$root/.n137-runs/$mode
rm -rf "$d" && mkdir -p "$d/game" "$d/home" "$d/cwd"
tar -xzf "$tgz" -C "$d/game"
cp tools/netplay/party.script "$d/party.script"
args=(--script "$d/party.script" --match 0@1500 --turbo --hash-log "$d/hash.log")
limit=${CHECK_TIMEOUT:-1500}
disc=${PB_DISC:?PB_DISC is the disc image}
case $mode in
host)
    (cd "$d/cwd" && XDG_DATA_HOME=$d/home/.local/share HOME=$d/home timeout -s KILL "$limit" \
        "$d/game/PartyBoard/partyboard" "${args[@]}" >"$d/out.txt" 2>&1) || true ;;
fedora|bullseye)
    img=pb-n137-fedora
    [ "$mode" = fedora ] || img=debian:bullseye
    run=(docker run --rm -u "$(id -u):$(id -g)" --device /dev/dri --group-add "$(stat -c %g /dev/dri/renderD128)"
        -v "$d:$d" -v "$disc:/disc/game.ciso:ro" -v "$XDG_RUNTIME_DIR/${WAYLAND_DISPLAY:-wayland-1}:/xdg/wayland-0"
        -e XDG_RUNTIME_DIR=/xdg -e WAYLAND_DISPLAY=wayland-0 -e SDL_VIDEO_DRIVER=wayland -e SDL_AUDIO_DRIVER=dummy
        -e SDL_APP_ID="${SDL_APP_ID:-partyboard-test}" -e PB_DISC=/disc/game.ciso -e HOME=$d/home
        -e XDG_DATA_HOME=$d/home/.local/share -w "$d/cwd" "$img")
    if [ "$mode" = bullseye ]; then
        "${run[@]}" sh -c "cat /etc/debian_version; ldd --version | head -1; \
            for f in $d/game/PartyBoard/partyboard $d/game/PartyBoard/*.so*; do ldd \$f | grep 'not found' || true; done; \
            timeout -s KILL 30 $d/game/PartyBoard/partyboard ${args[*]}" >"$d/out.txt" 2>&1 || true
    else
        "${run[@]}" timeout -s KILL "$limit" "$d/game/PartyBoard/partyboard" "${args[@]}" >"$d/out.txt" 2>&1 || true
    fi ;;
*) echo "usage: check.sh host|fedora|bullseye [tar.gz]" >&2; exit 2 ;;
esac
grep -a -m1 "netplay: match over at frame" "$d/out.txt" || true
grep -a -m1 "netplay: script ended" "$d/out.txt"
