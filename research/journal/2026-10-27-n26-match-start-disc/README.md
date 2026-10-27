# N26: N8's match start with a GMPE01 disc

## Outcome

With three fixes (below), runs that leave from different menus agree from the match frame:

- `--match 0@900`: three `--script` runs (title screen, Mini-Game mode, Party setup menu) with the same pads from
  frame 900 on. `hashdiff.py --from 900` reports `identical: 11100 frames` for each of the three pairs, overlays
  `-1@900, 89@902` (89 = w01).
- N27 lobby: two processes host and join a lobby in the Online menu and the host starts. Both log
  `netplay: lobby started` and then `match state set at frame 0`, and their hash logs (from the match's frame 0)
  are `identical: 5559 frames`.

Heap layouts differ between peers in every pair, before and during the board. The state stays identical anyway
(see Heaps).

## Fixes (each found by a run)

1. **Crash at match start from the title screen** (SIGSEGV in `BootTitleExec`, bootDll.so already unloaded).
   `omOvlKill` kills only `omwatchproc`'s children (the objman). A menu's root processes (`HuPrcCreate` in its
   ObjectSetup, e.g. bootDll's `BootExec`) normally end themselves right after their own `omOvlCallEx`. When
   `MatchStartProc` switches overlays from outside, they keep running in the unloaded DLL. Fix:
   - `Process.func` (TARGET_PC) and `HuPrcKillIf(pred, user)` in `src/game/process.c`.
   - `MatchStartProc` first kills every process whose entry point or destructor lies outside libdol (`dladdr` /
     `GetModuleHandleEx`). This is the overlay's own code. libdol's processes (window, chrman ...) are kept, as an
     overlay's own exit keeps them.
   - Killed processes end later in the same `HuPrcCall` pass, before `omwatchproc` (priority 0, last) unloads
     the DLL, so their destructors still find their code.
   - Logged as `netplay: match start: killed N menu processes`: 1 from the title screen, 2 from Mini-Game mode,
     13 from the party setup.
2. **Seeds differ at the match frame** (`rand8`, `frand`). In a `--script` run, `netplay_match_start` kept the
   session and did not set the seeds again, but each menu path had used them differently. New
   `netplay_reseed()` (`netplay_begin`'s seed part, from the session seed and the current frame) is called there
   in scripted runs. A real lobby match was already fine: it calls `netplay_begin`.
3. **Board one frame late after the title screen.** `HuAudDllSndGrpSet` (`src/port/audio.c`, the PC replacement
   of `src/game/audio.c`) runs `HuPrcVSleep()` when the AUX setting changes. bootDll's AUX differs from the
   board's, modesel's/mentDll's do not, so from the title screen the board's ObjectSetup ran one frame later.
   Frame 901 differed, and from 903 on every section did. New `HuAudNetplayAuxReset()`, called by
   `netplay_match_tick`, forgets the last AUX. Every peer then changes it and sleeps that frame.

Unit tests: `match_test.c` checks that the predicate keeps this program's code and kills code of another
module (libc) as entry point or as destructor, and that the tick resets the AUX and a scripted start reseeds.

## How the runs were made

Build: `cmake --build --preset linux-default-relwithdebinfo`. Each run had its own `XDG_DATA_HOME`/`HOME` and
`"video.enableVsync": false` in its `config.json`.

| run | config | script | where it is at frame 900 |
|-----|--------|--------|--------------------------|
| a | default | `mash-a.script` taps up to 899, then the common tail, `end 12000` | title screen (bootDll, ovl 1) |
| b | `"backend.skipBootSequence": true` | `tools/netplay/wander-mgmode.script` | Mini-Game mode (mgmodedll, ovl 72, from 671) |
| c | `"backend.skipBootSequence": true` | `tools/netplay/wander-party.script` | Party setup (mentDll, ovl 70, from 631) |

Each script's pads from frame 900 on are mash-a's pattern (A every 60 frames, A+START every 240), extended to
frame 12000. Run a's script is mash-a.script with that tail, so mash-a.script itself was not changed. The command
was `partyboard --script <s> --match 0@900 --hash-log <x>.log`, then `tools/netplay/hashdiff.py --from 900`.

What I learned about scripting the menus:
- The boot sequence cannot be cut short with A/START: modesel is reached only at frame ~835. Skipping the boot
  sequence reaches modesel at frame 4.
- The file select needs four A presses. The mode cursor moves with the analog stick (`HuPadDStkRep` comes from
  the stick, not the D-pad), e.g. `370 0 none 100 0 0 0 0 0`.

Lobby run (N27):
- Both processes ran in a private rootful Xwayland (`Xwayland :97`, `SDL_VIDEODRIVER=x11`), so xdotool input
  and the clipboard stayed off the desktop session.
- Each user dir had an `online.json` with `"relay": "off"`. Each process ran `partyboard --hash-log x.log` (no
  script; idle pads).
- Steps: Play → F1 → Online → Host Game → Copy Long Code. The guest: Online → Join → Paste Code → Join Game →
  Ready (keyboard nav). Then the host: Start Match.
- The logs restart at frame 0 at the match (`netplay_begin`), so each log was cut at its last frame-0 line
  before `hashdiff.py`.

## Heaps

Board start (`netplay: heap N:` lines at `Start New OVL 89`; 0 SYSTEM, 1 MUSIC, 2 DATA, 3 DVD, 4 MISC):

| run | heap 0 | heap 1 | heap 2 | heap 3 | heap 4 |
|-----|--------|--------|--------|--------|--------|
| a (title) | 7 blocks, 43968 B, B3F0E8642D91001A | 1, 1309760, 6AA421FEB596DA2C | 41, 289536, 6E01C0716603B2D3 | 1, 12480, FA4A69415D6D4BC1 | 0 |
| b (mgmode) | 7, 43968, 0E4A662EB72607BA | same | 54, 15872, 259F0F4D64887DBB | 0 | 0 |
| c (party) | 15, 103456, 3948F37066D2A608 | same | 60, 16448, A16E425192F7EB41 | 0 | 0 |
| lobby host, guest | 8, 84992, 6B20845103284ADA | same | 41, 289536, 6E01C0716603B2D3 | 1, 12480, FA4A69415D6D4BC1 | 0 |

**Yes, they differ.** Every block that survives the overlay kill has num -256 (`HuMemDirectMalloc`). I found the
callers with a temporary patch: return address in `HuMemDirectMalloc` plus `dladdr` in the dump. It was not
committed.

| heap | block (size) | allocated by | in runs |
|------|--------------|--------------|---------|
| SYSTEM | 0xa040 | `HuCardInit` (card work area) | all |
| SYSTEM | 0x140 | `HuPrcCreate` (a dol process) | all |
| SYSTEM | 0x120, 0x420 x2, 0x80, 0x60 | `fn_80036BC8` (minigame_seq.c, dol init) | all |
| SYSTEM | 0x2120, 0x280 x2, 0x2c00, 0x2e0, 0x41e0 x2, 0xfa0 | chrman `EffectInit` | c (party menu loaded characters) |
| DATA | 0x80/0x60/0x60/0x3c0 per sprite anim | `HuSprAnimRead` | 10 in a, 12 in b and c |
| DATA | 0x60 pairs | `HuSprAnimMake` | 4 in b, 7 in c |
| DATA | 0x43900 | bootDll code (return address no longer mapped after the unload). Probably `NintendoDataDecode`'s buffer (bootDll main.c:1090), the only bootDll `HEAP_DATA` alloc not freed on the spot | a |
| DVD | 0x30c0 | `GetRelIncludeData` | a |

So the surviving blocks are dol sprite animations that the menus read with `HuSprAnimRead`/`HuSprAnimMake` and
never freed (DATA), the party menu's character effects (SYSTEM), and the boot's logo/REL data when it is killed
mid-title. Positions differ as well as sets: a and b have the same SYSTEM block set but different layout hashes.

During the board, layouts still differ on every frame in every pair. In the lobby run, host and guest had the same
layout at board start (both came from the title screen), but it differed from frame 4 on. Even so, all state
sections stayed identical for 11100 and 5559 frames. The free-memory zeroing at board start (N8) covers reads of
unwritten memory. A peer running out of heap where another does not is still possible in principle; I did not see
it.

## Tried / failed

- Directions in modesel with D-pad buttons: ignored (see the stick note above).
- The lobby run with resized windows (`xdotool windowsize`): the RmlUi layout was cut, so I kept 1280x960
  windows and scrolled or used keyboard navigation for controls below the 643 px X screen.
- `HuAudNetplayAuxReset` first went into `src/game/audio.c`, which is not built on PC (link error). It is in
  `src/port/audio.c` now.

## Not verified

- **Minigames**: in the scripted runs the state stops changing after frame 4141 (all sections, including frand,
  constant up to 12000). The A/START taps probably stop matching what the board waits for. Identity was checked up
  to there and through the frozen part, but no minigame or `instDll` ran. Tuning scripts to reach a minigame is
  N15's.
- **Lobby run with input**: the pads were idle for the whole match (no controller, no script). The board intro
  ran (5559 frames), but no turn was played by a human seat.
- **Lobby over the relay / short code**: the long code with relay off on one machine. The short code (QX3Y) was
  shown but not used to join.
- Which allocation makes the lobby peers' heap layouts diverge at board frame 4: not found (filed).
- `HuPrcKillIf` predicate on Windows (`GetModuleHandleExA`) and Apple (`dladdr`): not built here (no toolchains).
  Android uses the Linux path. A platform with neither returns NULL: nothing is killed, which is the old behaviour.
- Other menus the online UI can sit on (options, extra room, story mode, pause): only the title screen, modesel →
  Mini-Game mode, modesel → Party setup, and (lobby) the title screen were run.
