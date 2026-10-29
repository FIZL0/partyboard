#!/usr/bin/env bash
# tools/build.sh [windows] — release builds of the game into build/dist/, as opentennis's tools/build.sh:
#   build/dist/partyboard-windows-x86_64.zip  a PartyBoard\ folder: partyboard.exe, libdol.dll, every REL DLL, aurora's
#                         DLLs (SDL3, webgpu_dawn + dxcompiler/dxil, nod) and res\. Run partyboard.exe, pick the disc.
# No game data goes into build/dist/.
# Windows is cross-compiled with MinGW (N138: the windows-mingw preset, cmake/toolchains/x86_64-w64-mingw32.cmake);
# libgcc, libstdc++ and winpthread are linked into every binary, so no MinGW DLL ships. aurora's prebuilt Dawn and
# SDL3 are MSVC builds: they need the Microsoft Visual C++ 2015-2022 runtime (x64), as the MSVC build does.
# One-time setup (Arch):
#   sudo pacman -S rustup mingw-w64-gcc python-pillow   # pillow: the .exe's icon (platforms/windows/png2ico.py)
#   rustup default stable && rustup target add x86_64-pc-windows-gnu
set -euo pipefail
cd "$(dirname "$0")/.."
what=${1:-windows}

windows() {
    local t=x86_64-pc-windows-gnu preset=windows-mingw
    if ! command -v x86_64-w64-mingw32-gcc >/dev/null || ! rustc --print target-libdir --target $t 2>/dev/null | xargs test -d; then
        echo "windows build needs (once): sudo pacman -S rustup mingw-w64-gcc python-pillow; rustup default stable; rustup target add $t" >&2
        exit 1
    fi
    [ -f extern/aurora/CMakeLists.txt ] || git submodule update --init --recursive --quiet
    local b=build/$preset
    [ -f "$b/build.ninja" ] || cmake --preset $preset >/dev/null
    cmake --build --preset $preset
    # The folder is built from the install tree (the game's own targets plus the DLLs aurora copies beside the .exe),
    # emptied first, so nothing stale from an earlier build gets in.
    local inst=build/windows-mingw-install
    rm -rf "$inst" && cmake --install "$b" --prefix "$inst" >/dev/null
    local out=build/dist/windows/PartyBoard
    rm -rf build/dist/windows && mkdir -p "$out"
    cp -r "$inst/res" "$out/"
    local f
    for f in "$inst"/*.exe "$inst"/*.dll; do
        case $(basename "$f") in
            # aurora's MSVC-built DLLs: as downloaded
            SDL3.dll|webgpu_dawn.dll|dxcompiler.dll|dxil.dll) cp "$f" "$out/" ;;
            *) x86_64-w64-mingw32-strip -o "$out/$(basename "$f")" "$f" ;;
        esac
    done
    # Every DLL a binary imports must be in the folder or be Windows' own (no libgcc_s/libstdc++/libwinpthread).
    local missing
    missing=$(for f in "$out"/*.exe "$out"/*.dll; do x86_64-w64-mingw32-objdump -p "$f" | sed -n 's/^\s*DLL Name: //p'; done |
        sort -fu | while read -r d; do
            [ -e "$out/$d" ] && continue
            case ${d,,} in
                api-ms-win-*|kernel32.dll|user32.dll|gdi32.dll|advapi32.dll|shell32.dll|ole32.dll|oleaut32.dll|ntdll.dll|\
                userenv.dll|ws2_32.dll|bcrypt.dll|bcryptprimitives.dll|crypt32.dll|secur32.dll|ncrypt.dll|iphlpapi.dll|\
                combase.dll|propsys.dll|rpcrt4.dll|ktmw32.dll|dxgi.dll|d3d12.dll|d3d11.dll|dbghelp.dll|winmm.dll|imm32.dll|\
                version.dll|setupapi.dll|cfgmgr32.dll|hid.dll|dwmapi.dll|uxtheme.dll|shlwapi.dll|comdlg32.dll|winspool.drv|\
                msvcp140.dll|msvcp140_atomic_wait.dll|vcruntime140.dll|vcruntime140_1.dll) ;;
                *) echo "$d" ;;
            esac
        done)
    [ -z "$missing" ] || { echo "build/dist/windows/PartyBoard: imports DLLs it doesn't ship: $missing" >&2; exit 1; }
    tools/netplay/check-hook-imports.sh "$out" >/dev/null
    rm -f build/dist/partyboard-windows-x86_64.zip
    (cd build/dist/windows && zip -qr9 ../partyboard-windows-x86_64.zip PartyBoard)
    echo "build/dist/partyboard-windows-x86_64.zip ($(du -h build/dist/partyboard-windows-x86_64.zip | cut -f1))"
}

case $what in
    windows) windows ;;
    *) echo "usage: tools/build.sh [windows]" >&2; exit 2 ;;
esac
