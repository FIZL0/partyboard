# N149: players who join take over CPU seats when continuing an online match

User 2026-10-29: "make lan players able to join and takeover for cpus when continuing game".

## What changed

**Continue lobby (`net/src/lobby.rs`).** A continue already seated any joiner in a seat the computer played (first a
seat that was the computer's in the save, then an absent player's), but the joiner couldn't choose, the host couldn't
undo it, and a returning owner lost to a stranger. Now:

- `Msg::Take { from, to }` (guest) / `Host::take_seat` (host's own player): moves a player from its seat to another
  seat the computer plays. `from` goes back to the save's seat (the computer's, with its saved key). Not allowed off
  the player's *own* saved seat (same key): that seat is theirs, and moving would leave the key on two seats.
- `Host::cpu(i, ..)` in a continue on a guest's seat gives it back to the computer as saved (at the difficulty
  passed). A guest left with no seat gets `Refused("the host gave your seat to the computer")`. Never the host's
  seat.
- Owner first: a Hello whose key is a saved seat's key while a stranger holds that seat gives the owner the seat. The
  stranger moves to another free seat (keyless first), or when none is left it gets `Refused("the player of your seat
  is back ...")`. The lobby-full check counts such seats as the owner's. N89's rule still applies: if a seat here
  already carries the key, the Hello is another player (a twin copy) and gets a new key.
- `Room::open()` is public. The LAN listing's `open` uses it, so a continue lobby lists its CPU seats as open. Before
  this it counted only empty seats, which is always 0 in a continue.

**Mid-match (`lobby.rs`, `lobby_c.rs`, `recover.rs`, `desync.rs`).** A Hello that arrives after the start is no longer
refused if there is a *stand-in* seat (`Room::stand_ins`: the computer plays it and it has a player key, i.e. a
dropped player the host gave to the CPU (N10), or an absent player in a continue). Seats that were the computer's
from the start stay the computer's. I limited it this way because the brief names the N10 seat, and it keeps CPU
seats that nobody ever played from being taken in the middle of a match.

- The host checks with `Host::can_take_over` (build/disc, enough stand-in seats beyond those already promised), queues
  the peer in `Shared::takers` and sends `Msg::Joining`. The guest's snapshot gets `"joining":true`, and its lobby
  window says "The match is on: you take over a computer's seat at the next turn or minigame."
- At the host's next resync point (`pbnet_resync_point`: a turn start or the end-of-round minigame start, N46/N52),
  `Shared::point` runs `Host::take_over` for each queued peer. A seat with the peer's key comes first, then the first
  stand-in seat. The host sends a rejoin token (`Book::issue_to`) and does the normal `host_reload`: every peer
  reloads the point that was just taken, on a new epoch, with the new room. So the seat's `iscom` flips on the same
  frame on every peer, through the same path as an N10 stand-in or rejoin. The newcomer gets `Reload`, its lobby goes
  to STARTED, and `netplay_lobby_poll` starts it from the room's checkpoint (the rejoin path, no save needed).
- If the match is started and has a stand-in seat nobody is waiting for, the host keeps a LAN listing up
  (`open` = free stand-in seats). The LAN tab then lists the running match and a join goes through. A taker that
  disconnects before the point is dropped from the queue.

**C ABI / UI.** `pbnet_lobby_take(from, to)` (pbnet.h, stub). In `online.cpp` the lobby's seat list shows a continue's
CPU seats as `Open - <name> - <character> (<level>)`. Their pane has **Take This Seat** for a seated player of this
device (`pbnet_lobby_take(mySeats[0], i)`). The host's pane on a guest's seat in a continue has **Give to the
Computer**. `View::joining` is parsed from the snapshot.

## Checks

- `cargo test`: new `lobby::tests::continue_cpu_seats_are_claimable`, `a_keyed_seat_goes_to_its_owner`,
  `host_hands_a_seat_back_to_cpu`, `a_newcomer_takes_a_stand_in_seat_after_the_start` (sans-IO), and
  `lobby_c::tests::a_newcomer_takes_a_stand_in_seat_at_the_next_point`. That last one uses two real endpoints on the
  testnet: a continue started without the saved host, a newcomer gets `joining`, a second newcomer is refused ("no
  seat"), `point()` gives epoch 1 with the seat, and the newcomer's lockstep starts. In the old
  `continue_unclaimed_seats_go_to_anyone_or_stay_cpu`, the line asserting the host can't touch a player's seat now
  checks the host's own seat, because N149 changes that rule on purpose.
- In-game, `harness/` (N148's approach: a scratch hook in online.cpp plus an A masher for every local pad, added by
  `apply.py` and taken out by `remove.sh`; the GMPE01-01 disc; two desktop copies with their own user dirs; iroh,
  relay off):
  - `run.sh make m1 170`: one copy hosts Hosty + Pal (two local players) with seats 3/4 CPU and plays turn 1. The
    online save it writes (`.n149-runs/save1.json`, turn 1, keys `[key-host, key-pal, "", ""]`) is the save the next
    runs continue.
  - `run.sh cont c2 SAVE 200`: copy 1 continues with Hosty alone. Copy 2 finds the lobby with `pbnet_lan_browse`
    (listing `"open":3`), joins with its code (as `join_lan` does) as Carl, and is seated in 3P (the save's keyless
    CPU seat). It focuses the 4P seat button and clicks the real **Take This Seat** in its pane: it moves to 4P and
    3P goes back to the CPU. Then it readies and the host clicks Start Match. Started room: 4P = Carl, cpu false,
    character 3 (the save's). Hash logs, lockstep session: **identical for 11991 frames** (board, minigame, board,
    turn 2's minigame), 0 desync lines. Run c1 did the same with `pbnet_lobby_take` (that harness version had not
    found the select-button yet): identical for 11957 frames.
  - `run.sh mid d1 SAVE 220`: copy 1 starts the continue alone (Pal's 2P is a stand-in). Copy 2 waits for
    `started.txt`, then the LAN tab lists the running match (`"open":1`). It joins and logs "waits for the next
    point". At the host's next resync point (minigame 14 of turn 1), the host logs `takes seats [1] from the
    computer` and `reload 1`. Both log `lobby started (epoch 1)`, and the guest plays 2P (Carl, character 1 = Pal's).
    Host session 2 vs guest session 1: **identical for 10187 frames** (through turn 2 and its minigame), 0 desync
    lines.
  - Hash logs were split with N38's `split.py` (copied here) and compared with `tools/netplay/hashdiff.py`.
- `tools/check.sh`: passes (cargo test 114 + 79, C tests, CMake build).

## Tried / failed

- The first harness build failed: the lobby JSON's `state` is a string, not a number.
- The first cont run didn't find the 4P seat button: SelectButton's tag is `select-button`, not `button`. The
  harness used `pbnet_lobby_take` instead (c1). Fixed for c2, which clicked the real button.

## Not verified

- **A seat's owner coming back by key in-game** (a stranger moves or is refused): only unit-tested
  (`a_keyed_seat_goes_to_its_owner`). A game run of it needs three copies, which AGENTS.md allows only as a
  3-4 peer run with nothing else beside it. The rule is plain room logic.
- **The host's "Give to the Computer" button**: only unit-tested through `Host::cpu`. No game run clicked it.
- **Joining by short code or from the browser**: both use the same Hello/lobby path as the LAN join that was tested,
  but no run used them. The browser LAN tab's nearby listing (N139a) carries the same `open` count. No web build was
  made or run for this task.
- **A mid-match takeover of a seat given to the CPU after a live drop (N10)**: the run used an absent player's seat
  in a continue. That is the same `stand_ins` rule (computer plays it, key kept) and the same reload. The drop-then-
  stand-in case is covered by the sans-IO test, which calls `recover::stand_in`, not by a game run.
- **A turn-start point as the takeover point**: in the run the first point after the join was a minigame point. A
  turn point uses the same `pbnet_resync_point` → `point()` path.
- Android/iOS/Windows builds: not built. The changes are platform-neutral Rust/C++ plus one new C ABI function with
  a stub.
