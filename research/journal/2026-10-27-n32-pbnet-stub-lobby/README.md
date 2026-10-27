# N32: pbnet stub complete again (PBNET_STUB=ON configures)

## What I found
The task said the stub lacked N20's `pbnet_lobby_*`. By the time N32 started, those were already in
`src/port/pbnet_stub.c` (host/join/leave/back/state/gen/json/picks/ready/settings/cpu/start, state
`PBNET_LOBBY_NONE`, json `{"state":"none"}`, every command 0). Comparing every `pbnet_*` in
`include/port/pbnet.h` against the stub (the same check `cmake/pbnet.cmake` runs) left only one missing:
`pbnet_android_init_tls` (N12/N21, `net/src/android.rs`).

## What I did
- Added `pbnet_android_init_tls` to the stub under the same `#ifdef __ANDROID__` as the header; it returns 0
  ("not on", which `src/port/portmain.cpp` already handles by logging and using bundled roots — moot, as the
  stub never makes HTTPS calls).
- `cmake --preset linux-default-relwithdebinfo -B <scratch> -DPBNET_STUB=ON` now configures (no FATAL_ERROR, no
  warning) and the whole game builds and links against the stub.
- `gcc -fsyntax-only -Wall -Wextra -D__ANDROID__` on the stub: clean.

## Not verified
- No Android or tvOS build with the stub: the stub's Android branch was only syntax-checked with host gcc
  (`-D__ANDROID__`), not built with the NDK; there is no tvOS toolchain here (no Apple SDKs).
- Did not boot the stub-linked Linux binary; the change only adds a function that Linux does not compile.
