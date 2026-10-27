# N42: N25's online match end, run with two game processes (GMPE01)

**Outcome: blocked.** Two `partyboard` processes (host and guest, Online menu, one human seat each, two CPUs)
continued a real turn-10/10 online save to the party's end. The guest ran N25's whole end path. The host never
reached the end: the two peers **diverge in the party results** (mstory3Dll, ovl 78), a few seconds after the
board.

- With the desync check on (as shipped), N46 finds the divergence at frame 180. It reloads the turn point, and the
  next epoch diverges at the same frame with the same two hashes, every time. The match never ends.
- With the desync check off (temporary switch), the guest leaves for modeseldll at 5420 and logs
  `match over at frame 5421`. The host is still in mstory3 at that frame, and then waits in a lockstep WAIT at 5423
  for good.

Filed **N58** with the details below. Two crashes turned up on the way and are filed too: **N56** (a resync
reload while a player is in a shop) and **N57** (m425).

No code changed in this task. Everything below ran on temporary changes, saved in `harness/scratch.diff` and
reverted.

## What was checked, against the task

| task item | result |
|---|---|
| both log `netplay: match over at frame N` with the same N | **no**. Guest: `match over: overlay 74 starts at frame 5420`, `match over at frame 5421, hash D46F6968D82DAB36: session ends`. Host: none; still in ovl 78 at 5420–5422 (desync check off, run m7). |
| neither is stuck in a lockstep WAIT | **no**. The host waits at frame 5423 ("Waiting for Guesty" hold modal) for good: the guest's session ended at 5421, so its pads for 5422 and later never come. This follows from the divergence. N25's end assumes every peer ends after the same frame. |
| both show the lobby again, toast "The match is over" | guest: **yes**. At frame 5421 its lobby state goes 3 → 2 (started → open), and the visible RmlUi documents show `[The match is over. Back in the lobby.][Waiting for Guesty to be ready.][Ready]`. Host: never got there. |
| the host's room has a new id and nobody ready | **not reached** (the host never ended). The guest shows the started room's id `local-0000000050424E54`, opened locally, with nobody ready. That is `Guest::back`'s "host was slower" case; the host's reopened room never came. |
| a second Start enters the board on both, hashdiff agrees from that frame | **not reached**. The guest pressed Ready again; the host was still in the match. |
| N31: the guest's online save deleted at the end | guest: no `online/` file afterwards (it had none of its own: a turn-10 continue writes no checkpoint before the end). Host: the save is still there, because its end never ran. Not a real check of N31. |

## The divergence (N58)

Run m4/m5 (desync check on). The same turn-10 save on the host, the same pads (both peers' pad hash sections are
equal), fresh epochs:

- `tools/netplay/hashdiff.py` (epoch 1): first differing frame **181**, in `frand` (and `heap`, whose layout differs
  from frame 0 anyway, as in N26/N30). `sys`, players, cfg and pads are equal. N46 reports the desync at 180 (hashes
  go to pbnet every 30 frames). After each reload the next epoch diverges at the same frame with the same pair of
  hashes (host `908979C2…`, guest `8CF4E25D…` in run m3). So this is not random: each peer always takes its own path.
- A temporary probe in `frand`/`frandmod`/`frandf` (caller via `dladdr`, ovl 78, frames 150–186) shows one
  extra caller: `UpdateNpcDust` (`src/game/chrman.c:1812`). It runs every 4 frames from **182 on the host** and
  **181 on the guest**. In later epochs it starts at 158 on the guest and after 186 on the host.
- A probe in `UpdateNpcDust` (motion id/time of its model each frame) shows why. Model 54 plays motion 59 from time
  0 at frame 159 on both peers. It shows motion 57 (time 14) from **161 on the guest** and **162 on the host**. That
  is the end of a 15-frame `Hu3DMotionShiftSet` into 57 (`Hu3DMotionNext` switches when `motOvlWork.start` reaches
  the end). The time at the switch is 14 on both, so the shift itself was **started one frame later on the host**.
  Motion playback is deterministic (`minimumVcountf` is constant), so the cause is earlier and outside the hash:
  whatever process starts that shift does so one frame apart.
- Ruled out: the memory card. Run m3 gave the guest a copy of the host's `MemoryCardA.USA.raw`, and it still
  diverged. Ruled out: async data loads in mstory3 (there are none). Not found within the time I gave it. Suspects
  for N58: the host/guest role, the pad status of ports without a seat, audio or voice timing, and the online save
  file (the host has it, the guest doesn't).
- With hashes not sent to pbnet (run m6/m7, `PB_N42_NOHASH`), 510 (m6) / 1166 (m7) frames between 181 and ~5117
  differ, all in `frand`. After that the hashes are equal again. The flow does not meet again: in m7 the guest is
  in ovl 74 at frame 5420 and the host is in ovl 78, with the same state hash. Process timing and `omcurovl` are
  not in the hash. N58 asks to add what decides the overlay flow.

The two-peer run from a **turn-1** save (run t1) also desynced, at frame 3480 in the board (`sys` from 3471). Its
resync crashed both peers; that is N56 below. I did not look at that desync's cause. N53 is the general "runs
diverge" task.

## Also found (filed, not fixed)

- **N56**: a desync resync reload while the board runs crashes both peers at once. SIGSEGV in
  `Hu3DModelHookReset(54) <- BoardModelHookReset(46) <- DestroyShop (shop.c:390) <- gcTerminateProcess <-
  HuPrcEnd`, right after `OvlKill 1` of the reload. The old board's dol processes tear down on models that the
  overlay change already freed: the other half of N38's bug 1.
- **N57**: m425 (ovl 33) segfaults in its draw callback, `fn_1_E914` (`src/REL/m425Dll/thwomp.c:1949`) <-
  `fn_1_101C4` <- `Hu3DDrawPost`, 2 frames into the minigame. Seen in a single-process `--continue` of the turn-7
  save. The retry from the turn-8 save took another minigame (runs are not deterministic).
- The party results pages (mstory3, second start) need more than START. A on a page opens its details
  (`fn_1_16AD4`), and only **B** leaves those (`result.c:382`). START on the page list leaves the results. An A/START
  pattern can stay in a details page for good. A + then B|START stretches got through (pads below).

## How it was run

### Setup

- Build `linux-default-relwithdebinfo` at the task's base (f59ddd44). Temporary, uncommitted changes, all in
  `harness/scratch.diff` and reverted afterwards:
  - `src/game/main.c`: with `PB_TURBO_TMP`, `disableFrameLimiter` is set and the limiter is skipped under a
    lockstep too (a `--match` script run counts as one). Single-process save runs only.
  - `src/port/netplay_match.c` `party_config`: `PB_EXPLAIN0_TMP` turns the minigame explanations off (N40's way).
    Save runs only.
  - `src/port/netplay.c`: `n42_pads` (N38's pad presser, env `PB_N42`). In a lockstep match, this machine's seat
    taps every 24 frames: START in instDll (ovl 3, after 120 frames); in ovl 78, A for 600 frames, then B|START for
    600 frames, in turn; A everywhere else. `PB_N42_NOHASH` stops `pbnet_set_hash` (runs m6/m7 only).
  - `src/port/ui/online.cpp`: `#include` of `harness/n42_harness.inc` and `n42_harness()` at the top of
    `update_online_match`.
  - `src/game/frand.c`, `src/game/chrman.c`: the two probes above (runs m4/m5 on).
- `harness/`: `n42_harness.inc` presses the real buttons, found by label in the visible RmlUi documents (Click +
  Submit, as in N38):
  - host: Continue tab > "Toad's Midway Madness" > Host Match, write the code, Start Match once someone is ready
  - guest: Join tab > Join Game (`last_code` in online.json), Ready when nobody is ready
  - after the lobby is open again, the same for the second match
  - logs every lobby JSON/state change and the visible toast/modal/button texts, with ms and frame

  The rest of `harness/`:
  - `ns.sh`: a private user+net namespace with `lo` and a dummy 10.77.0.1/24, relay off
  - `peer.sh <host|guest> <tag>`: one peer in it, with its own `XDG_DATA_HOME`/`HOME` under `build/n42run/<peer>`,
    `--hash-log`, `$PB_EXTRA` env
  - `reset.sh`: puts the turn-10 save on the host and removes rejoin records, desync logs and online files
  - `split.py`: N38's; cuts a hash log into its sessions
- online.json: host player "Hosty" with key `local-1`. That is the key a `--match` save gives seat 0, so the host
  takes seat 0 back by key, and seat 0 is the results' pad. Guest "Guesty". Relay off. Vsync off in both
  config.json.

### The turn-10 save (keeping the two-peer part short)

A party from turn 1 is ~115k frames, and two lockstep peers run at 60 fps. So the host continues a turn-10 online
save through the real Continue tab. Seats: 0 the host (back by key), 1 the guest (the first keyless seat), 2 and 3
CPU (`GWPlayerCfg` confirmed in gdb: iscom 0, 0, 1, 1).

The save comes from single-process `--script --match 0@1500` runs (mash-a below 1500, then port 0 taps A every 20
frames, explanations off, turbo). They were played in pieces, because each piece hit something:
- **the save run**: checkpoints at turns 1–7 (frame 70961), then my own `timeout -s KILL 1500` killed it in turn 8
- **continue of turn 7** (`--continue t7.json --match 0@900`): turn-8 checkpoint at 9521, then the m425 crash (N57)
- **continue of turn 8**: turn 9 at 22401 and **turn 10/10 at 34541**. This is the save used, copied before its own
  match end could delete it (N31).

Turbo did not help in the end. Even with the limiter skipped, the hidden window paces at ~60–75 fps (N40's
1000 fps was under a private Xwayland, which AGENTS.md now rules out). The machine also had a load average of
30–50 from other slots.

### Two-peer runs

| run | what | result |
|---|---|---|
| t1 | turn-1 save, pipeline test | Continue > Host Match > Join > Ready > Start works; both enter w01 at frame 1, `turn 1/10`. Desync at 3480; resync crashed both peers (N56). |
| m1 | turn-10 save; START only in ovl 78 | board 3 frames, then ovl 78 for 21k frames: START alone does not get past the first mstory3 part. No desync, but nothing moved on. |
| m2, m3 | A then START stretches; m3 with the same card file on both | desync at 180–210, then at 180 in every epoch (N58) |
| m4, m5 | probes | the frand caller and the motion shift above |
| m6 | hash check off; A/START | the results' details page waited for B (16k frames), so no end |
| m7 | hash check off; A, then B\|START | guest: match end at 5421, lobby + toast. Host: lockstep WAIT at 5423 for good. |

## Tried and dropped

- `pkill -f`/`pgrep -f` loops with patterns that matched my own shell killed the shell, not the game. Kill by the
  PIDs that `peer.sh` prints instead. One of the slots in parallel runs its own peers with the same binary name
  under `s3/`, so never kill by name.
- The first turbo patch went into the `#else` (GC) branch of `main.c`'s loop and did nothing.

## Not verified

- **Every item of the task on the host**, and both-peer items: same N, no stuck WAIT, the host's new room id and
  empty ready list, the second Start and its hashdiff. The peers diverge before the end (N58), so none of these can
  hold until N58 is fixed. The guest-side results above are real, but they come from a run with the desync check
  off.
- **The second match**: never started (the host never reached the lobby).
- **N31's save deletion on two peers**: the guest had no file of its own, and the host's end never ran.
- **What times the shift** (N58's root cause): not found. Memory card and async loads ruled out only.
- **The turn-1 desync at 3480**: not analysed (N53/N58 may cover it). Its crash is N56.
- **Visuals** of the toast/lobby: only their texts were read from the RmlUi tree (hidden windows, no screenshots;
  gamescope is off-limits).
- **Real clicks/pads, two machines**: buttons got synthetic events; both peers ran in one network namespace on one
  host.
- **Offline play**: nothing changed in the tree, so nothing to check.
