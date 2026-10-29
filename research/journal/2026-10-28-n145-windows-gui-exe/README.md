# N145: Windows exe without a console window

`partyboard.exe` from `tools/build.sh windows` was a console-subsystem program (upstream's `WIN32_EXECUTABLE` is
commented out), so Windows opened a black console behind the game. It is now a GUI-subsystem exe
(`objdump -p`: `Subsystem 00000002 (Windows GUI)`, was `00000003 (Windows CUI)`).

## What changed

- `CMakeLists.txt`: `WIN32_EXECUTABLE TRUE` on `partyboard` under `if (MINGW)` (CMake adds `-mwindows`). The
  entry is unchanged: MinGW's CRT start calls libmingw32's `main`, which calls SDL's `WinMain` (the `-u WinMain`
  from N138 stays). The MSVC preset is left as upstream had it (not a target we build; no MSVC here).
- `logfile::attach_std_streams()` (`src/port/log_file.cpp`, called first in `port_main`). A GUI exe starts with
  no std handles unless the parent passed some (a redirect). The CRT then gives `stdout`/`stderr` fd -2 ("no
  console"): printf goes nowhere, and N109's tee, which swaps fds 1 and 2 for pipes, would have left the streams on
  -2, so **the log file would have lost all CRT output**. Now a stream without a handle is reopened on the parent's
  console (`AttachConsole(ATTACH_PARENT_PROCESS)` + `CONOUT$`: started from cmd/PowerShell, the output shows there,
  after the prompt, since shells don't wait for GUI exes) or else on `NUL`. A redirect (handles present) is left
  alone.
- `start_tee()` on Windows tees `_fileno(stdout)`/`_fileno(stderr)` instead of fixed fds 1 and 2 (after `freopen`
  the streams sit on fd 3+, since the CRT keeps 0-2 marked open with handle -2), and points the Win32 std handles
  (Rust's eprintln) at those pipes. Linux/Android/iOS code paths unchanged.

## Checks (wine, headless gamescope)

`harness/runwine.sh` (N138's, with a `STDIO` mode), the unzipped package from this build, wine's `vulkan-1.dll`
beside the exe (test only) and `"backend.graphicsBackend": "vulkan"`, one game at a time. Every run:
`--script tools/netplay/mash-a.script --turbo --match 0@1500 --hash-log <run>/hash.txt`.

| how started (`STDIO=`) | game's std handles | `logs/partyboard.log` | stdout (`out.txt`) | hash log |
|---|---|---|---|---|
| `redirect`: `wine partyboard.exe > out.txt 2>&1` | the redirect | 855 lines, to `script ended at frame 17400, hash 35F0AB781EC2B38C` | the game's output (974 lines with gamescope's) | reference |
| `start`: `wine start /wait /unix` (ShellExecute, as Explorer) | none | 855 lines, same end | none of the game's lines (118 lines, all gamescope/wine) | identical |
| `cmd`: `wine cmd /c partyboard.exe`, no redirect | wine's cmd passes its own | 855 lines, same end | the game's output | identical |

All three: "match start: killed 2 menu processes" and the end hash `35F0AB781EC2B38C`, the same as N138's console
exe (so the overlays/frames match N138's Linux-identical run). The `start` row is the case the fix is for: no std
handles, and the log file still has everything.

- `tools/check.sh` (cargo test in `net/` + the Linux build): passes.
- `x86_64-w64-mingw32-objdump -p partyboard.exe`: `Subsystem 00000002 (Windows GUI)`. libdol.dll and the RELs
  are DLLs, their subsystem field doesn't matter.

## Not verified

- **Real Windows 10/11** (no machine here; N18 is the human check): that no console opens when started from
  Explorer, and the `AttachConsole` route from cmd/PowerShell (output printed into the shell after its prompt).
  Under wine I can't tell the two no-redirect routes apart: wine's `cmd` hands its unix stdio to the child, and in
  the `start` run nothing shows whether `AttachConsole` succeeded (it went to the console or `NUL`; either way not to
  out.txt, and the log was complete).
- That the old code (console tee on fds 1/2) really lost the CRT output in a GUI exe without handles: from the
  UCRT's stdio init (streams get fd -2 when there is no handle), not run. The fix doesn't depend on it.
- The MSVC preset: unchanged (still a console exe), no MSVC toolchain here.
