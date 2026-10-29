#!/usr/bin/env bash
# tools/linux-release.sh [out.tar.gz] — N137: the Linux release, partyboard-linux-x86_64.tar.gz (default
# build/release/partyboard-linux-x86_64.tar.gz): one PartyBoard/ folder with partyboard, libdol.so, every REL .so,
# libSDL3.so.0, res/ and README.txt, stripped, $ORIGIN runpath.
# Built as opentennis's tools/build.sh linux: inside Valve's Steam Runtime "sniper" SDK container (Debian 11,
# glibc 2.31), so it starts on any distro with glibc 2.31+ (Ubuntu 20.04+, Debian 11+, Fedora 32+, SteamOS 3).
# The preset `linux-release` (CMakePresets.json) builds SDL3 from source and links every other library in
# (abseil, fmt, RmlUi, freetype, sqlite, png, zlib, zstd, libstdc++, libgcc); the check below refuses a build that
# needs anything else from the system than glibc, GL/Vulkan and what SDL3 dlopens (X11, Wayland, ALSA, Pulse…).
# Needs docker (sudo pacman -S docker; sudo systemctl enable --now docker; sudo usermod -aG docker $USER). The
# container's Rust toolchain and cargo cache live in ~/.cache/partyboard-steamrt, its build in build/linux-release.
set -euo pipefail
cd "$(dirname "$0")/.."
src=$PWD
out=${1:-build/release/partyboard-linux-x86_64.tar.gz}
sdk=registry.gitlab.steamos.cloud/steamrt/sniper/sdk:latest
glibc=2.31
docker info >/dev/null 2>&1 || { echo "the Linux release needs docker (see the top of $0)" >&2; exit 1; }
[ -f extern/aurora/CMakeLists.txt ] || git submodule update --init --recursive --quiet

# a worktree's .git points into the main checkout's: mount that too, at the same path (the version stamp, patches)
gitdir=$(cd "$(git rev-parse --git-common-dir)" && pwd)
cache=~/.cache/partyboard-steamrt
mkdir -p "$cache"
mounts=(-v "$src:$src" -v "$cache:/cache")
case $gitdir/ in "$src"/*) ;; *) mounts+=(-v "$gitdir:$gitdir") ;; esac
docker run --rm -u "$(id -u):$(id -g)" "${mounts[@]}" -w "$src" \
    -e HOME=/cache -e CARGO_HOME=/cache/cargo -e RUSTUP_HOME=/cache/rustup -e CC=gcc-14 -e CXX=g++-14 \
    "$sdk" sh -ec '
        [ -x /cache/cargo/bin/cargo ] || curl -sSf https://sh.rustup.rs | sh -s -- -y -q --profile minimal --no-modify-path
        export PATH=/cache/cargo/bin:$PATH
        git config --global --add safe.directory "*"
        cmake --preset linux-release >/dev/null
        nice -n 19 cmake --build --preset linux-release'

inst=build/linux-release-install
dist=build/linux-release-dist
pb=$dist/PartyBoard
rm -rf "$dist" && mkdir -p "$pb"
# the install has partyboard, libdol.so, the RELs and res/; SDL3 (a FetchContent build) is copied by hand
cp -r "$inst/res" "$pb/"
for f in "$inst"/partyboard "$inst"/*.so build/linux-release/_deps/sdl-build/libSDL3.so.0; do
    strip --strip-unneeded -o "$pb/$(basename "$f")" "$f"
done
cat >"$pb/README.txt" <<'EOF'
Party Board for Linux (x86_64; glibc 2.31 or newer: Ubuntu 20.04+, Debian 11+, Fedora 32+, SteamOS 3 / Steam Deck).

Run ./partyboard from this folder and pick your Mario Party 4 disc image (USA, GMPE01; .iso, .gcm, .ciso, .rvz…)
in the window it opens. Nothing of the game is included: you need your own disc image. Settings and saves go
to ~/.local/share/MarioPartyRD/Party Board.
EOF

# the libraries it may take from the system: glibc's, GL/Vulkan come through Dawn's and SDL's dlopen, the rest
# (X11, Wayland, ALSA, Pulse, PipeWire, udev, dbus…) SDL3 dlopens itself
allowed='^(libc|libm|libdl|libpthread|librt|libutil|ld-linux-x86-64|libdol|libSDL3)\.so'
bad=0
for f in "$pb"/partyboard "$pb"/*.so*; do
    while read -r lib; do
        [[ $lib =~ $allowed ]] || [ -e "$pb/$lib" ] || { echo "$f needs $lib from the system" >&2; bad=1; }
    done < <(readelf -d "$f" | sed -n 's/.*(NEEDED).*\[\(.*\)\]/\1/p')
    rp=$(readelf -d "$f" | sed -n 's/.*(RUNPATH).*\[\(.*\)\]/\1/p')
    [ -z "$rp" ] || [ "$rp" = '$ORIGIN' ] || { echo "$f has runpath $rp" >&2; bad=1; }
done
need=$(objdump -T "$pb"/partyboard "$pb"/*.so* | grep -o 'GLIBC_[0-9.]*' | sort -uV | tail -1)
echo "PartyBoard/ needs $need (the floor is GLIBC_$glibc)"
[ "$(printf '%s\n' "${need#GLIBC_}" $glibc | sort -V | tail -1)" = $glibc ] || { echo "needs $need, newer than GLIBC_$glibc" >&2; bad=1; }
[ $bad = 0 ] || exit 1

mkdir -p "$(dirname "$out")"
tar -czf "$out" -C "$dist" PartyBoard
echo "$out: $(du -h "$out" | cut -f1)"
