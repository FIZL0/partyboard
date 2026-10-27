//! C ABI of the lockstep (`include/port/pbnet.h`). One session per process, behind a mutex; the transport
//! that starts it ([`start`]) is Rust's (the match start, N2/N6), C only steps it.

use crate::lockstep::{delay_for, Config, Pad, Session, Step, Transport};
use std::sync::atomic::{AtomicBool, Ordering};
use std::sync::Mutex;
use std::time::Instant;

/// `pbnet_step`: run the frame.
pub const PBNET_READY: i32 = 1;
/// `pbnet_step`: don't run a frame, call again next real frame.
pub const PBNET_WAIT: i32 = 0;
/// `pbnet_step`: no lockstep session.
pub const PBNET_OFF: i32 = -1;

struct Live {
    session: Session<Box<dyn Transport + Send>>,
    start: Instant,
}

static LIVE: Mutex<Option<Live>> = Mutex::new(None);
/// N10: a reload is being set up (`crate::recover`): every step waits until the next [`start`].
static HELD: AtomicBool = AtomicBool::new(false);

fn live() -> std::sync::MutexGuard<'static, Option<Live>> {
    LIVE.lock().unwrap_or_else(|e| e.into_inner())
}

/// Starts the lockstep session on `wire` (replacing one that runs).
pub fn start(cfg: Config, wire: Box<dyn Transport + Send>) {
    *live() = Some(Live { session: Session::new(cfg, wire), start: Instant::now() });
    HELD.store(false, Ordering::SeqCst);
}

/// N10: holds the game (every `pbnet_step` waits, `pbnet_lockstep_active` stays 1) until the next [`start`]: a
/// reload's new session must not run a frame before the game took the checkpoint.
pub fn hold() {
    HELD.store(true, Ordering::SeqCst);
}

/// N10: peer `p`'s silence (ms) in the running session (None: no session, or `p` is this machine or no peer).
pub fn quiet_ms(p: u8) -> Option<u64> {
    live().as_ref().and_then(|l| l.session.quiet_ms(p, l.start.elapsed().as_millis() as u64))
}

/// Ends the lockstep session, if one runs. Safe to call any time, from any thread.
#[no_mangle]
pub extern "C" fn pbnet_lockstep_stop() {
    *live() = None;
    HELD.store(false, Ordering::SeqCst);
}

/// 1 if a lockstep session runs, else 0. Safe to call any time, from any thread.
#[no_mangle]
pub extern "C" fn pbnet_lockstep_active() -> i32 {
    (live().is_some() || HELD.load(Ordering::SeqCst)) as i32
}

/// One real frame of the lockstep: `PBNET_READY` with every seat's pads in `out[0..4]` (run the frame),
/// `PBNET_WAIT` (skip the game tick, `out` untouched; also while a reload is held, N10), or `PBNET_OFF` (no
/// session, `out` untouched).
///
/// # Safety
///
/// `local` points to `n_local` readable `PADStatus` (this machine's seats' pads, in seat order; fewer than
/// its seats are padded with idle pads, more are ignored), or is null when `n_local` is 0. `out` points to 4
/// writable `PADStatus` (16 bytes each, aurora's `TARGET_PC` layout) that do not overlap `local`. Both are
/// only used during the call. Thread-safe (serialised by a mutex), but meant for the game thread.
#[no_mangle]
pub unsafe extern "C" fn pbnet_step(local: *const Pad, n_local: i32, out: *mut Pad) -> i32 {
    if HELD.load(Ordering::SeqCst) {
        return PBNET_WAIT;
    }
    let mut g = live();
    let Some(l) = g.as_mut() else { return PBNET_OFF };
    let local = if local.is_null() || n_local <= 0 {
        &[][..]
    } else {
        // SAFETY: the caller guarantees `local` points to `n_local` readable pads for the call.
        unsafe { std::slice::from_raw_parts(local, n_local as usize) }
    };
    let now = l.start.elapsed().as_millis() as u64;
    match l.session.step(now, local) {
        Step::Ready(pads) => {
            if !out.is_null() {
                // SAFETY: the caller guarantees `out` points to 4 writable pads that don't overlap `local`.
                unsafe { std::ptr::copy_nonoverlapping(pads.as_ptr(), out, 4) };
            }
            PBNET_READY
        }
        Step::Wait => PBNET_WAIT,
    }
}

/// The next frame `pbnet_step` runs (0 with no session). Safe to call any time, from any thread.
#[no_mangle]
pub extern "C" fn pbnet_frame() -> u32 {
    live().as_ref().map_or(0, |l| l.session.frame())
}

/// Bit per line-up index of the peers whose pads for the next frame aren't in (0 with no session). Safe to
/// call any time, from any thread.
#[no_mangle]
pub extern "C" fn pbnet_waiting_on() -> u32 {
    live().as_ref().map_or(0, |l| l.session.waiting_on() as u32)
}

/// This machine's hash of the state before `frame`: call it when `pbnet_frame() % 30 == 0`, before the
/// `pbnet_step` that runs that frame. Ignored with no session. Safe to call any time, from any thread.
#[no_mangle]
pub extern "C" fn pbnet_set_hash(frame: u32, hash: u64) {
    if let Some(l) = live().as_mut() {
        l.session.set_hash(frame, hash);
    }
}

/// Takes the oldest hash mismatch found: 1 and its frame, peer (line-up index) and both hashes, or 0 (none,
/// or no session; outputs untouched).
///
/// # Safety
///
/// Each pointer is null (that field is not written) or points to a writable value of its type, for the
/// call only. Thread-safe.
#[no_mangle]
pub unsafe extern "C" fn pbnet_poll_desync(frame: *mut u32, peer: *mut i32, ours: *mut u64, theirs: *mut u64) -> i32 {
    let Some(d) = live().as_mut().and_then(|l| l.session.desync()) else { return 0 };
    // SAFETY: the caller guarantees each non-null pointer is writable for its type.
    unsafe {
        if !frame.is_null() {
            *frame = d.frame;
        }
        if !peer.is_null() {
            *peer = d.peer as i32;
        }
        if !ours.is_null() {
            *ours = d.ours;
        }
        if !theirs.is_null() {
            *theirs = d.theirs;
        }
    }
    1
}

/// The input delay (frames, 2..=15) for a round trip of `rtt_ms`. Pure; safe to call any time.
#[no_mangle]
pub extern "C" fn pbnet_delay_for(rtt_ms: u32) -> u32 {
    delay_for(rtt_ms)
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::lockstep::{IDLE, NO_PEER};

    struct Nowhere;
    impl Transport for Nowhere {
        fn send(&mut self, _: u8, _: &[u8]) {}
        fn recv(&mut self) -> Option<(u8, Vec<u8>)> {
            None
        }
    }

    /// The one test touching the global session (tests run in parallel).
    #[test]
    fn c_abi_steps_a_solo_session() {
        let mut out = [IDLE; 4];
        pbnet_lockstep_stop();
        assert_eq!(unsafe { pbnet_step(std::ptr::null(), 0, out.as_mut_ptr()) }, PBNET_OFF);
        start(Config { me: 0, peers: 1, seats: [NO_PEER, 0, NO_PEER, NO_PEER], delay: 2 }, Box::new(Nowhere));
        assert_eq!(pbnet_lockstep_active(), 1);
        let a = Pad { button: 0x100, stick_x: -3, ..IDLE };
        for f in 0..3 {
            assert_eq!(pbnet_frame(), f);
            pbnet_set_hash(f, 5);
            assert_eq!(unsafe { pbnet_step(&a, 1, out.as_mut_ptr()) }, PBNET_READY);
        }
        assert_eq!(out, [IDLE, a, IDLE, IDLE]);
        assert_eq!(pbnet_waiting_on(), 0);
        assert_eq!(unsafe { pbnet_poll_desync(std::ptr::null_mut(), std::ptr::null_mut(), std::ptr::null_mut(), std::ptr::null_mut()) }, 0);
        pbnet_lockstep_stop();
        assert_eq!(pbnet_lockstep_active(), 0);
        assert_eq!(pbnet_delay_for(150), 6);
    }
}
