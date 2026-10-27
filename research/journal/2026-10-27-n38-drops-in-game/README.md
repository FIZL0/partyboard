# N38: N10's drops, rejoins and CPU stand-in, run in the game

Two `partyboard` processes (GMPE01 disc, relwithdebinfo, Linux) played an online match. The lobby was hosted and
joined through the Online menu, and every scenario of N10 ran. Two bugs turned up and are fixed:

1. **A rejoin reload desynced from frame 0** (`src/port/netplay_match.c`). The host's own board ran on in frame 0
   and wrote over the reloaded state.
2. **A "Host left" modal stayed up over the running match** after a long drop (`src/port/ui/online.cpp`).

After the fixes, every reload's hash logs agree from its frame 0 (`tools/netplay/hashdiff.py`).

## Results (final build, runs 2 and 3)

| scenario | result |
|---|---|
| 10 s guest network drop, mid-board (turn 1) | Both peers report "Waiting for guest" / "Waiting for host" **2.99 s** after the cut. The modal appears 3.0–3.1 s after the cut, with Keep Waiting / CPU Plays for guest / Save and Quit on the host and Keep Waiting / Leave Match on the guest. Both hold at the same frame (5136, pbnet 5137) and resume at 5137, 1.2 s after the link is back. Epoch 0 logs are identical through the drop: **5668 frames**, until the guest was killed. Run 1 (before the fixes) gave the same: 6057 → 6058, identical 7001 frames. |
| kill the guest app, restart, Online > Join > Rejoin Match | The host gets `rejoin ...: Reload`, and both start epoch 1 from the last turn point. **Both resume at player 3's turn**, and the logs are identical from the reload's frame 0: 3462 frames in run 2, 5656 in run 3. No desync line. |
| guest killed, host presses "CPU Plays for guest", guest rejoins 46 s later | The host logs `the computer plays <token>'s seats` and reloads alone (epoch 2: this machine plays 1P, no remote peer, no wait). The CPU finished player 3's turn and went into the minigame (ovl 3 instDll, then ovl 16). The guest's Rejoin Match came **while the host was inside minigame 16**. Epoch 3 reloaded the turn point on both peers, and the guest took seat 2P back. Identical for **3015 frames**, until the next kill. |
| guest killed while both peers were inside minigame 16, rejoin | Epoch 4 reloads the last turn point (player 3, turn 1) on both. Identical for **15529 frames**: the minigame again, the post-minigame checkpoint (frame 13059, turn 2), and on into turn 3. No desync. |
| extra: 45 s guest drop | QUIC's idle timeout ends the connection after ~30 s, and the guest shows "Host left" (N10 says this is expected). The guest redials, the host answers `Resume { index: 1 }`, and play goes on **at the same frame** (3387 → 3388). The resume came 2.1 s after the link was back in run 3 and 19 s after it in run 2. Run 2 left the "Host left" modal up (bug 2). With the fix, no modal is left on either peer. |

"Reload from the last checkpoint" means the host's newest **turn point** (N46), not the minigame's start. A reload
from inside a minigame therefore replays the turn before it, as N46 says.

## Bug 1: the host's old board ran on in frame 0 of a reload

Run 1 was on the tree before this task. After Rejoin Match, the logs said:

```
host:  netplay: the board resumes at player 0's turn
guest: netplay: the board resumes at player 3's turn
both:  netplay: desync at frame 30 ...
hashdiff (epoch 1): first differing frame: 0   sys 6E92FE5042CF0242 vs D71CC6B9148057B5
```

Both peers loaded the same blob (player 3's turn point), so the state was equal when `netplay_match_tick` set it.
`MatchStartProc` kills only processes whose code is outside libdol (the menu's own). The board's processes
(`MainFunc` etc., in `src/game/board`) are dol code, so a peer that is mid-board when the reload comes keeps them
running through frame 0. When `omOvlCallEx` replaces the overlay, their teardown runs (`BoardKill`/`DestroyBoard`,
flags, `player_curr`). The rejoined guest came from a menu and had none of this. `BoardStartCheck` then saw other
flags on the host, and the host restarted the round at player 0.

The N46 resync then ran on. Epoch 2 resynced from the host's new turn point, which was already wrong. Epoch 2
diverged again at frame 1896 (`plr2`), because both peers' teardowns now ran on different (desynced) states. Epoch
3 was identical. So a resync hid the bug, but every rejoin cost one or two extra reloads and a replayed round.

**Fix** (`netplay_ovl_start`): when the match's board overlay starts (`match.scrub`), the old overlay is gone. The
state is set again from the match at that point: `game_stat_canned`, then `resume_config` (the blob) or
`party_config`, then `netplay_reseed`. This is the same on every peer. For a peer that came from a menu, nothing
changed between the tick and the overlay start, so this is a no-op there. The frame-0 hash line on the host still
reflects the teardown, but the lockstep's frame-0 hash is taken before it. In run 2 and run 3 the logs are identical
from frame 0 anyway.

`tools/netplay/match_test.c`: after the start process, the test writes over `GWSystem`/`GWPlayer` and clears
`reseeded`. Another overlay must leave them alone. The board's start must restore them (party path) and reload the
blob (continue path: turn 7, coins, the CPU seat). Without the fix it fails at `party_calls == 2 && reseeded == 1`.

## Bug 2: a hold modal that never closed

`update_online_match` gave every `HoldModal` the same `shared_ptr<bool> alive`. When the message changed from one
text to another ("Waiting for host" → "Host left" at the QUIC timeout), the old modal was popped and the new one
pushed. The old one is deleted later in the same loop (`ui::update` removes closed documents), and its destructor
set the shared flag to false. On the next poll `shown` became null, so the new modal was never popped. After the
reconnect, a "Host left / Leave Match" modal stayed over the running match. Leave Match there would have quit a live
match.

**Fix:** a new flag for each modal. Run 3 checked it: the same 45 s drop leaves `modal (none)` on both peers after
the resume.

## How it was run

AGENTS.md rules out Xwayland/gamescope tricks, so xdotool (N35's way) could not be used. A scratch harness (not
committed, saved in `harness/`) pressed the real buttons from inside the game:

- `harness/n38_harness.inc` was `#include`d into `src/port/ui/online.cpp` and called at the top of
  `update_online_match` (three lines, plus `<RmlUi/Core.h>`, `<cstdio>`, `<fstream>`, `<sys/time.h>`). With
  `PB_N38=host|guest|rejoin` it does what a player would:
  - pushes `OnlineWindow` and selects the Host or Join tab
  - finds the button by its label in the visible RmlUi documents and dispatches Click and Submit on it (Host Game,
    Join Game, Rejoin Match, Ready, Start Match, and the modal's "CPU Plays for guest")
  - logs every change of `pbnet_recover_json` and of the visible hold-modal texts, with wall-clock ms
- The guest's code was put in its `online.json` `last_code` instead of Paste Code (the clipboard of a hidden
  Wayland window was not tried).
- `harness/netplay_c.diff`: pads for this machine's seat in a lockstep match. A taps every 24 frames, and START in
  instDll (ovl 3, after 120 frames) and the party results (78). The keyboard is not bound, so the board needs this.
- `harness/peer.sh`: each peer runs inside one private network namespace (`unshare -rn sleep`, entered with
  `nsenter -U -n --preserve-credentials`). The namespace has `lo` and a dummy `10.77.0.1/24` (iroh needs an address
  for the long code), relay off. Each peer has its own `XDG_DATA_HOME`/`HOME` under `build/n38run/<peer>` and runs
  with `--hash-log`. Windows went to the hidden workspace (`SDL_APP_ID=partyboard-test`).
- `harness/drop.sh <guest pid> <s>`: a real network drop for the guest only. An nftables table in the namespace drops
  every UDP datagram to or from the guest's sockets (from `ss -uapn`, mDNS 5353 aside) for `<s>` seconds.
- `harness/split.py`: a `--hash-log` starts at frame 0 for every session (pre-match, then one per epoch). It is cut
  into `.0, .1, ...`, and the matching epochs are compared with `hashdiff.py`. A killed guest's log ends early. The
  "one log ends" result is that, and the host is always 0–4 frames ahead (input delay).

## Tried / failed

- First harness: matching labels only on leaf elements found nothing, because a RmlUi text node is a child of the
  span. Matching the inner RML of any element worked.
- Logging the full recover JSON every loop flooded the log (`quiet_ms` changes every poll). It is stripped before
  comparing.
- Rebuilding while peers ran was avoided: a running peer `dlopen`s the REL `.so` files on overlay changes.

## Not verified

- **The modal's look.** Its presence, title text and buttons were checked from the RmlUi tree of the visible
  documents, and its "CPU Plays for guest" button was pressed (it worked). No screenshot was taken, because the
  windows are on a hidden workspace and gamescope is off-limits (AGENTS.md).
- **Mouse or touch clicks.** The buttons got synthetic Click and Submit events, and the code went through
  `online.json` instead of Paste Code.
- **Real pads and human play.** Seats played with the A/START pattern only. The stick was not used, so minigame
  results are whatever idle-ish play gives.
- **Two machines and a real network.** Both peers ran in one network namespace on one host. The "drop" was
  nftables, not Wi-Fi loss.
- **Drops of 60 s or more.** Only 10 s and 45 s were tried. 45 s already passes QUIC's ~30 s idle timeout, and the
  redial path then worked twice (19 s and 2.1 s after the link was back). Why it took 19 s in one run was not
  looked at.
- **Host-side drop or host restart** (the host's "Save and Quit", a guest's "Leave Match") was not run. Quitting
  mid-match is N34's.
- **A reload before the first turn point** (N10: the match restarts from its start) was not run. Every reload here
  had a turn point.
- **More than two peers, other boards, team matches.** Board 0, 20 turns, 2 humans + 2 CPUs only.
- **Offline play.** Unchanged by construction: both fixes are only reached in an online match (`netplay_ovl_start`
  runs under `netplay_active()`, and `update_online_match` shows modals only in a started lobby). No offline run was
  made for this task.
- **`--script --match` debug runs** pass through the same board start, so their seeds are set again one frame
  later than before. They stay deterministic, but hash values recorded from older runs may differ. None is checked
  in by the gate.
- **Other platforms.** Linux only.
