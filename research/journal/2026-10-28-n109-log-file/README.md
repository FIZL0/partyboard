# N109: log file on every platform, crash handler

## What it does

- `src/port/log_file.cpp` / `include/port/log_file.hpp`, started from `port_main` right after the settings load.
- Setting `backend.writeLogFile` (default on), in a new **Settings > Advanced** tab (section "Logging"):
  "Write Log File" toggle, the log path as a line under it, and "Open Logs Folder" where the platform can
  (desktop: `SDL_OpenURL(file://…)`; iOS: `shareddocuments://<path>`, opens the Files app there; not on Android,
  where `file://` intents into app dirs throw, and not on web). The tab is hidden on web (`logfile::supported()`).
  Turning it on or off takes effect at the next start (help text says so).
- Where: `<user dir>/logs/partyboard.log`. iOS: the user dir is already Documents (N100; Info.plist already has
  `UIFileSharingEnabled`), so it is On My iPhone > Party Board > logs. Android: logs only go to
  `SDL_GetAndroidExternalStoragePath()/logs` = `/storage/emulated/0/Android/data/com.mariopartyrd.partyboard/files/logs`
  (decision: the rest of the user dir stays internal, moving saves was not asked for and needs a migration).
- Rotation at start: `partyboard.log` -> `.1` -> `.2` -> `.3` (this run + the 3 before it).
- How everything gets in: a tee at the fd level. fds 1 and 2 become pipes; a thread per pipe copies each read to
  the original fd (console unchanged) and `write(2)`s it to the log, so no user-space buffer is lost on a crash.
  When 1 and 2 are the same file (`2>&1`, a tty) one pipe is used so their order is kept. stdout is set to line
  buffered (a pipe would make it fully buffered); with the log off stdout is made line buffered too, so a crash
  doesn't eat its tail. This catches aurora's log callback, `PartyBoard*Log`, OSReport, printf and pbnet's
  `eprintln!` alike, without touching any of them. Windows: `_pipe`/`_dup2` + `SetStdHandle` (Rust writes to the
  Win32 std handles).
- Crash handler (POSIX): SIGSEGV, SIGBUS, SIGABRT, SIGILL, SIGFPE, SIGTRAP, on an alt stack. It waits (≤300 ms) for
  the tee to drain, then writes `*** PartyBoard crashed: signal …`, version + git hash + build type, pc/lr/sp from
  the ucontext, the pc's frame, the backtrace (glibc/Apple `backtrace()`, Android `_Unwind_Backtrace` since bionic
  has no `backtrace()` before API 33) with `image+offset (symbol+off)` per frame, the loaded image list with load
  bases (`dl_iterate_phdr` / dyld header + slide), `*** end of crash report`, fsync. Then the previous action is
  restored and the signal goes on (fault re-executes; sent signals are re-raised), so cores still happen.
  Windows: `SetUnhandledExceptionFilter`, code + address, `CaptureStackBackTrace`, module list via toolhelp.
- Next launch: if the old `partyboard.log`'s last 256 KiB contain the crash marker, the launcher (prelaunch
  `version-info`) shows one line "The last run crashed. Log: <path of partyboard.1.log>", and the log says so too.
- Boot lines (`partyboard::log`): version, branch + git hash, build type/date, platform + arch, device model
  (DMI on Linux, `ro.product.*` on Android, `hw.machine` on iOS), OS version (os-release / Android release, API and
  fingerprint / `kern.osproductversion`, plus uname), user dir, log file, and the disc id after `aurora_dvd_open`
  (`DVDGetCurrentDiskID`).
- Debug crash: `PB_CRASH_TEST=segv|abort|bus|ill`; on Android `adb shell setprop debug.partyboard.crash segv`
  (apps get no env; clear it with `setprop debug.partyboard.crash "''"`).

Symbolizing offline: frames print as `libdol.so+0x6b6de9`; `addr2line -f -C -e libdol.so 0x6b6de9` on the
matching unstripped build gave `partyboard::logfile::maybe_test_crash … log_file.cpp:826` and `port_main
… portmain.cpp:461`. On iOS use `atos -o <dylib> -l <header address> <pc>` with the image list's header address.

## Checks done

- Desktop (Linux, RelWithDebInfo): `PB_CRASH_TEST=segv|abort|bus|ill` each wrote the report to the log and to
  stderr, exit 139/134/135/132 (cores kept); next start logged and showed "The last run crashed"
  (checked the launcher element's text with a temporary print, removed); rotation kept `.1`..`.3`.
- Setting off (`{"backend.writeLogFile": false}`): no logs dir, "Log file: off", crash report still on stderr.
- `tools/netplay/party_check.sh` (two full scripted `--match` parties through the tee): identical 135000 frames,
  both reached `match over`; the log equals out.txt except the two config lines printed before the tee starts.
- Android emulator (hst34 x86_64, Android 14, APK built with `-Ppartyboard.nativeAbis=arm64-v8a,x86_64`): log in
  the external files dir with the boot lines; forced segv via the property gave the report with a full unwind
  through `libsigchain`/ART; next start reported the crash; `.1`/`.2` rotated.
- Builds: Android arm64 + x86_64 APKs (`./gradlew :app:assembleDebug`, JDK 21 download), iOS IPA
  (`tools/ios-ipa.sh`) + `tools/ios-ipa-check.py` clean, web (`WEB_FAST=1 tools/web.sh`), MinGW `-fsyntax-only`
  of log_file.cpp.

## Failed on the way

- iOS: `<ucontext.h>` errors without `_XOPEN_SOURCE` on Apple; use `<sys/ucontext.h>` there.
- The first emulator APK had no `libmain.so` for x86_64: gradle builds only arm64-v8a unless
  `-Ppartyboard.nativeAbis=arm64-v8a,x86_64` (not a code problem).

## Not verified

- iOS on a device: no device here. Not run: the log in Files, the crash report (Apple register access via
  `__darwin_arm_thread_state64_get_*`, `backtrace()` from a signal handler), "Open Logs Folder"
  (`shareddocuments://`). Only the build and the IPA static check.
- Windows: no MSVC/Windows box; only a MinGW syntax check. The tee, `SetStdHandle` and the exception filter never ran.
- Launcher crash line layout on screen (long paths wrap via `word-break`): not seen. Hidden windows can't be
  screenshotted with the RmlUi layer (aurora_capture_frame takes the EFB only); only the element's text was checked.
- "Open Logs Folder" on Linux (xdg-open) was not clicked.
- If the process is SIGKILLed, the few bytes still in the pipe (microseconds' worth) are lost from both console and
  log; before N109 they'd have reached a redirected file. Not seen in the party check.
- A crash inside the dynamic loader (holding its lock) could hang the handler (`dladdr`/`dl_iterate_phdr`); accepted.
