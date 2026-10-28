#!/usr/bin/env bash
# Cross-build Party Board for iPhone on Linux (N86), as opentennis's tools/ios-build.sh: clang + rustup's ld64.lld
# against the SDK from tools/ios-sdk.py, through our ios.toolchain.cmake with everything it would ask xcodebuild/xcrun
# for passed in. Usage: tools/ios-build.sh [cmake --build args…]   (build dir: build/ios-linux, override PB_IOS_BUILD)
# Then tools/ios-ipa.sh packs build/ios-linux/PartyBoard.app into an IPA.
set -euo pipefail
cd "$(dirname "$0")/.."
sdk=${PB_IOS_SDK:-$HOME/.local/share/hst-ios/iPhoneOS.sdk}
[ -d "$sdk" ] || { echo "no SDK at $sdk: run tools/ios-sdk.py Xcode.xip" >&2; exit 1; }
lld=$(rustc --print sysroot)/lib/rustlib/x86_64-unknown-linux-gnu/bin/gcc-ld/ld64.lld
[ -x "$lld" ] || { echo "no ld64.lld at $lld (rustup's)" >&2; exit 1; }
for t in clang clang++ llvm-libtool-darwin llvm-install-name-tool llvm-ar llvm-ranlib; do
  command -v $t >/dev/null || { echo "need $t (LLVM)" >&2; exit 1; }
done
min=15.0
build=${PB_IOS_BUILD:-build/ios-linux}
sdk_version=$(sed -n 's/.*"Version":"\([0-9.]*\)".*/\1/p' "$sdk/SDKSettings.json")
# cargo (pbnet, aurora's nod through corrosion) builds for the same minimum. cc-rs asks xcrun for the SDK unless
# SDKROOT is set; on a Linux host the host build scripts ignore SDKROOT (pbnet.cmake avoids it for macOS hosts).
export SDKROOT=$sdk IPHONEOS_DEPLOYMENT_TARGET=$min
export CC_aarch64_apple_ios=clang CXX_aarch64_apple_ios=clang++ AR_aarch64_apple_ios=llvm-ar
export CFLAGS_aarch64_apple_ios="-target arm64-apple-ios$min -isysroot $sdk"
export CXXFLAGS_aarch64_apple_ios=$CFLAGS_aarch64_apple_ios
export CARGO_TARGET_AARCH64_APPLE_IOS_LINKER=clang
# -C rpath: rustc then names a dylib @rpath/<file> (nod's libnod.dylib), not its absolute build path.
export CARGO_TARGET_AARCH64_APPLE_IOS_RUSTFLAGS="-C link-arg=-target -C link-arg=arm64-apple-ios$min -C link-arg=-isysroot -C link-arg=$sdk -C link-arg=-fuse-ld=lld -C link-arg=--ld-path=$lld -C rpath"
# ios.toolchain.cmake sets CMAKE_SHARED_LINKER_FLAGS itself (the Frameworks rpaths), so the linker goes in after it.
mkdir -p "$build"
cat > "$build/ios-linux.toolchain.cmake" <<EOT
include("$PWD/ios.toolchain.cmake")
string(APPEND CMAKE_SHARED_LINKER_FLAGS " -fuse-ld=lld --ld-path=$lld")
string(APPEND CMAKE_MODULE_LINKER_FLAGS " -fuse-ld=lld --ld-path=$lld")
string(APPEND CMAKE_EXE_LINKER_FLAGS " -fuse-ld=lld --ld-path=$lld")
EOT
# FIND_ROOT_PATH_MODE ONLY: the host's /usr and /lib are not the phone's (SDL found Linux libusb there, aurora abseil).
if [ ! -f "$build/build.ninja" ]; then
  cmake -S . -B "$build" -G Ninja \
    -DCMAKE_TOOLCHAIN_FILE="$PWD/$build/ios-linux.toolchain.cmake" -DPLATFORM=OS64 -DDEPLOYMENT_TARGET=$min \
    -DCMAKE_BUILD_TYPE=RelWithDebInfo \
    -DXCODE_VERSION_INT=27.0 -DSDK_VERSION="$sdk_version" -DCMAKE_OSX_SYSROOT_INT="$sdk" -DCMAKE_DEVELOPER_ROOT="$sdk" \
    -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ \
    -DBUILD_LIBTOOL="$(command -v llvm-libtool-darwin)" -DCMAKE_INSTALL_NAME_TOOL="$(command -v llvm-install-name-tool)" \
    -DCMAKE_AR="$(command -v llvm-ar)" -DCMAKE_RANLIB="$(command -v llvm-ranlib)" \
    -DENABLE_BITCODE=OFF -DENABLE_ARC=OFF -DENABLE_VISIBILITY=ON \
    -DRust_CARGO_TARGET=aarch64-apple-ios -DBUILD_SHARED_LIBS=OFF -DAURORA_NOD_LINKAGE=shared -DAURORA_SDL3_PROVIDER=vendor \
    -DCMAKE_DISABLE_FIND_PACKAGE_PkgConfig=ON -DCMAKE_FIND_ROOT_PATH_MODE_LIBRARY=ONLY -DCMAKE_FIND_ROOT_PATH_MODE_INCLUDE=ONLY -DCMAKE_FIND_ROOT_PATH_MODE_PACKAGE=ONLY
fi
exec cmake --build "$build" "$@"
