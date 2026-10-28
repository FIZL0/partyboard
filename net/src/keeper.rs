//! N110: a match's word to the others while this machine's frames are held because its player switched away (as
//! opentennis `net/keeper.rs`, B162): iOS took the focus (Control Center, a permission prompt) or put the app in the
//! background, Android paused it, or the desktop window was minimised or hidden. The game runs no frame then and no
//! `pbnet_step`, so nothing else would speak for it:
//!
//! - the game's loop, while it still turns (it holds instead of running the frame), calls [`pbnet_keeper_frame`]: a
//!   keep-alive at most every [`FRAME_EVERY`];
//! - a thread of its own (native builds) sends one every [`EVERY`] while the loop itself stopped too (Android blocks
//!   the game's thread while paused; iOS runs a backgrounded app for a few seconds before suspending it).
//!
//! Every datagram of the session says the player is away (`lockstep::FLAG_HIDDEN`), so the others hold the match with
//! "Waiting for <name>: their game is in the background" (`recover::waiting_or_hidden`) instead of a silent peer.
//! Nothing gives up on its own: a suspended app goes silent, and when it comes back its session resumes (or it
//! rejoins, N10).

use std::sync::atomic::{AtomicBool, AtomicU32, AtomicU64, Ordering};
use std::sync::OnceLock;
use std::time::{Duration, Instant};

use crate::lockstep_c;

/// How often the keeper thread looks, and how long without a keep-alive it waits before sending one.
pub const EVERY: Duration = Duration::from_secs(1);
/// The game loop's keep-alives while it holds (a WAIT sends every frame; this needs only to beat the 3 s silence).
pub const FRAME_EVERY: Duration = Duration::from_millis(250);

static HIDDEN: AtomicBool = AtomicBool::new(false);
/// One more per hide: an old keeper thread ends.
static EPOCH: AtomicU32 = AtomicU32::new(0);
/// The last keep-alive (ms since [`clock`]'s start), `u64::MAX` before the first.
static LAST: AtomicU64 = AtomicU64::new(u64::MAX);

fn clock() -> u64 {
    static START: OnceLock<Instant> = OnceLock::new();
    START.get_or_init(Instant::now).elapsed().as_millis() as u64
}

/// A keep-alive is due at `now` (ms) after the last one at `last` (`u64::MAX`: none yet).
fn due(now: u64, last: u64, every: Duration) -> bool {
    last == u64::MAX || now.saturating_sub(last) >= every.as_millis() as u64
}

/// Sends one keep-alive if one is due after `every` (and a session runs).
fn keep(every: Duration) {
    let now = clock();
    if due(now, LAST.load(Ordering::SeqCst), every) && lockstep_c::keep_alive() {
        LAST.store(now, Ordering::SeqCst);
    }
}

/// This machine's player switched away (the game's frames are held).
pub fn hidden() -> bool {
    HIDDEN.load(Ordering::SeqCst)
}

/// The player switched away (`hidden` 1: the game holds its frames from now on) or is back (0: frames run again).
/// The running lockstep session's datagrams (and those of one started later) say so; on hiding, one keep-alive goes
/// out at once and a keeper thread (native builds) keeps sending one every second until the player is back.
///
/// # Safety
///
/// Safe to call any time, from any thread; with no session only the flag is kept.
#[no_mangle]
pub extern "C" fn pbnet_keeper_hidden(hidden: i32) {
    let hidden = hidden != 0;
    if HIDDEN.swap(hidden, Ordering::SeqCst) == hidden {
        return;
    }
    lockstep_c::set_hidden(hidden);
    let epoch = EPOCH.fetch_add(1, Ordering::SeqCst) + 1;
    if !hidden {
        return;
    }
    LAST.store(u64::MAX, Ordering::SeqCst);
    keep(FRAME_EVERY);
    // no threads in the browser: its page's loop keeps turning while hidden (a timer), so `pbnet_keeper_frame` is
    // enough there
    #[cfg(feature = "iroh")]
    let _ = std::thread::Builder::new().name("pbnet keeper".into()).spawn(move || loop {
        std::thread::sleep(EVERY);
        if EPOCH.load(Ordering::SeqCst) != epoch || !HIDDEN.load(Ordering::SeqCst) {
            return;
        }
        keep(EVERY);
    });
    #[cfg(not(feature = "iroh"))]
    let _ = epoch;
}

/// One real frame the game held because its player switched away (no `pbnet_step`): a keep-alive to the peers if
/// none went out for [`FRAME_EVERY`]. Nothing happens with no session.
///
/// # Safety
///
/// Safe to call any time, from any thread; meant for the game thread.
#[no_mangle]
pub extern "C" fn pbnet_keeper_frame() {
    keep(FRAME_EVERY);
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn a_keep_alive_is_due_first_and_then_every_period() {
        let every = Duration::from_millis(250);
        assert!(due(0, u64::MAX, every));
        assert!(!due(100, 0, every));
        assert!(due(250, 0, every));
        assert!(due(10_000, 9_000, every));
        // a clock read before the last store (another thread) is not due
        assert!(!due(5, 10, every));
    }
}
