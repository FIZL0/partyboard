# N94: OSGetTime an hour off during daylight saving time

## Cause

aurora's `OSGetTime` (`extern/aurora/lib/dolphin/os/OSTime.cpp`) found the UTC offset as
`mktime(localtime(t)) - mktime(gmtime(t))`. `gmtime` sets `tm_isdst = 0`, so `mktime` reads the UTC fields
as local *standard* time, and the difference is the standard offset, with no DST in it. In EDT that is
-5 h when it should be -4 h, so the game's clock (save dates, the in-game clock) ran an hour behind.
Windows behaves the same way (see below).

## Fix

`cmake/patches/aurora/0006-ostime-dst-offset.patch`: offset = (local calendar fields from
`localtime_r`/`localtime_s`, read as UTC) - `t`. That is `timegm(localtime(t)) - t` worked out by hand with
`std::chrono::sys_days` (C++20 calendar, which the file already uses through `local_days`). It is the same
code on every platform, so Windows needs no `tm_gmtoff` or `_get_timezone`/`_get_dstbias` branch.

## Checks

- Formula, natively (glibc): old, new and `tm_gmtoff` for New York, Berlin, Sydney, Kolkata, UTC and
  Chatham (+12:45/+13:45), each on one DST date and one standard-time date. new == `tm_gmtoff` in every
  case; old is 3600 short whenever `tm_isdst=1`.
- Formula on the Windows CRT: MinGW build (`-static`) run under Wine with POSIX TZ strings EST5EDT,
  AEST-10AEDT, CET-1CEST and UTC0. new == `-(_get_timezone + (isdst ? _get_dstbias : 0))` everywhere. Old was
  an hour short in DST here too, which confirms the Windows build had the bug.
- Built game code: a scratch program dlopens the gate's `libdol.so` and prints
  `OSTicksToCalendarTime(OSGetTime())`. It equals `date` for America/New_York (EDT, 2026-10-28),
  Europe/Berlin (CET), Australia/Sydney (AEDT) and UTC.
- `tools/check.sh` passes.

## Other clocks looked at

- Netplay's hook (`__wrap_OSGetTime`, `src/port/netplay.c`) returns `netplay_time()` while online. That clock
  is synthetic: `time_base` comes from the match seed plus frames, not the wall clock. The fix does not touch
  it, and it stays the same on every peer. Offline the hook passes straight through to the fixed function.
- `extern/aurora/lib/card/Util.cpp` `getGCTime` (memory-card file timestamps) uses the same
  `mktime(gmtime)` approach, but it adds 3600 itself when `localtime` says `tm_isdst`, so it is right during
  DST. I left it alone.

## What went wrong along the way

- The first `check.sh` failed at configure: this slot's `extern/aurora` tree held an older WIP form of
  `0005-gx-firefox-wgsl` in `lib/gx/shader.cpp`. That version neither applied nor reverse-applied. Fix (local
  only, nothing committed): `git checkout -- lib/gx/shader.cpp` in the submodule, then
  `git apply --include=lib/gx/shader.cpp 0005-…patch`. A fresh checkout (the runner's merge test) applies the
  patches from scratch, so it never hits this.
- Wine sat silent for minutes on the default prefix. A new `WINEPREFIX` in the scratchpad with
  `DISPLAY`/`WAYLAND_DISPLAY` unset worked.

## Not verified

- A real Windows/MSVC build (no MSVC here). Only the formula was checked, under MinGW + Wine's CRT, not
  aurora compiled for Windows.
- macOS/iOS/Android/web at runtime. The code uses only `localtime_r` and C++20 `sys_days`, both present on
  those toolchains (the existing Apple fallback path already uses `local_days`). It was not run there.
- Save dates and the clock shown in game menus were not checked on screen. The check stops at
  `OSGetTime` → `OSTicksToCalendarTime`, which is where those values come from.
- DST change-over instants (an `OSGetTime` call right on the transition). The offset is worked out on every
  call from that call's `localtime`, so it follows the transition.
