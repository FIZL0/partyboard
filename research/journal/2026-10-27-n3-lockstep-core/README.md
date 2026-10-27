# N3: lockstep core (`net/src/lockstep.rs`)

## What was done

- `net/src/lockstep.rs`: `Session<T: Transport>`, a delay-based lockstep ported from opentennis
  `hst-play/src/play/netplay.rs` `Lockstep` (no Bevy, no `Msg` enum; own binary datagram).
  - `Pad`: `#[repr(C)]` mirror of aurora's `PADStatus` **with `TARGET_PC`'s `u32 extButton`** (16 bytes,
    not 12; const-asserted size and offsets, and checked against the real header with gcc). The wire form is
    12 bytes: `button` LE, the ten byte fields, one zero. `extButton` is not sent and arrives as 0 (no game
    code reads it; `grep extButton src include` is empty).
  - `delay_for(rtt_ms)`: opentennis's formula, `ceil(rtt/2 * 60) + 1` clamped to 2..=15.
  - `step(now_ms, local) -> Step::Ready([Pad;4]) | Step::Wait`. Frames `0..delay` are idle for all seats.
    At the first call for frame `t` the local pads are taken for frame `t + delay`; calls while waiting
    don't take pads (a press that lasts only the waiting frames is lost; fine for a turn-based board, N5
    may want to latch presses).
  - Seats: `Config.seats[s]` = line-up index of the peer playing seat `s`, or `NO_PEER` (always idle; CPU).
    A peer can own several seats; `step` takes its pads in seat order.
  - Resend: every step sends each peer every own frame from that peer's ack up (oldest first, at most
    1024 bytes of pads per datagram). The ack is "every frame below this is in", piggybacked on the
    peer's own datagrams. Frames more than `2*delay + 60` past the next one are dropped (the sender
    resends), as opentennis's `ahead`.
  - RTT: each datagram carries the sender's ms stamp, the echo of the last stamp it heard and how long it
    held it; smoothed 7/8. `rtt_ms(peer)` feeds `delay_for`; the host choosing the delay at match start is
    N6's job (the session's delay is fixed for its life, as opentennis).
  - Hashes: C calls `set_hash(frame, h)` for the state before every 30th frame; each datagram carries the
    two latest own hashes; compared whichever side comes first (as opentennis `check`), each (peer, frame)
    once; mismatches queue for `desync()`.
  - `Transport` trait: unreliable datagrams by line-up index (`send(to, &[u8])`, `recv() -> (from, Vec)`).
    N2's iroh datagrams implement it. `Box<dyn Transport>` implements it too.
- `net/src/lockstep_c.rs`: the C ABI (one global session behind a `Mutex`, ms clock from `Instant`):
  `pbnet_step(local, n_local, out[4])` → `PBNET_READY`/`PBNET_WAIT`/`PBNET_OFF`, `pbnet_frame`,
  `pbnet_waiting_on`, `pbnet_set_hash`, `pbnet_poll_desync`, `pbnet_delay_for`, `pbnet_lockstep_active`,
  `pbnet_lockstep_stop`. Starting a session is Rust-only (`lockstep_c::start(cfg, Box<dyn Transport>)`),
  since only Rust has a transport. Declared in `include/port/pbnet.h`; all names start with `pbnet_` so
  the N1 objcopy keeps them global (`nm -D libdol.so` shows all nine).
- Deviation from the brief's `pbnet_step(local_pad, out_pads[4])`: it takes `n_local` too, because a
  peer can have more than one seat (two pads on one machine).

## Tests (`cargo test`, in `tools/check.sh`)

In-memory wire `lockstep::fake` (shared hub, simulated clock, fixed one-way lag, seeded xorshift loss).
Sessions use `delay_for(2 * lag)`.

- two and four peers at 0 / 150 / 300 ms one-way lag with 5% loss, 1800 frames: every peer ran identical
  pads on every frame, equal to what each peer read `delay` frames earlier. Runs at 0 and 150 ms keep
  ≥ 90% of real frames; at 300 ms one way (needs 19 frames, delay capped at 15) ≥ 75% (measured ~81-83%).
  The measured RTT is within 40 ms of 2×lag (it measured 16 / 300 / 600).
- two seats on one peer plus an empty seat; a peer starting 120 frames late (the others hold); a silent
  peer (Wait, `waiting_on` names it); solo session; malformed datagrams dropped.
- hashes: honest run reports nothing; peer 1's hash forged at frame 300 is reported on both sides as one
  desync with the right frame, peer and both hashes.
- the C ABI on a solo session (the one test that touches the global).
- Mutation check: making `send` send only the newest frame (no resend) failed 7 tests, so the loss
  tests do exercise resend.

## Tried / failed

- Nothing failed on the way. The worktree again had no submodules: `git submodule update --init --recursive`
  before the build (as in N1).

## Not verified

- Real network: there is no transport yet (N2). Only the fake wire was used; jitter and reordering beyond
  what loss produces are not simulated (the fake lag is constant).
- In the game: nothing calls `pbnet_step` yet (N5), so no game run. No disc image was needed or used.
- Datagram size against iroh's real max datagram size: the budget is 1024 bytes of pads + ~40 header
  bytes, assumed to fit a ~1200-byte QUIC datagram; N2 should check `max_datagram_size`.
- Android/iOS builds of the crate: only the Linux desktop build ran.
