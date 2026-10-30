//! Desync recovery (N11, N46): a hash mismatch never ends a match, and the match never runs on desynced.
//!
//! - **Input log**: the first mismatch a lockstep session finds writes its input log
//!   ([`crate::lockstep::Session::input_log`]: the pads of the frames it ran and its hashes) to
//!   `<user dir>/desync-<match>-<time>-e<epoch>-f<frame>-p<index>.json`. A mismatch is seen on both sides (each
//!   compares the other's hashes with its own), so both peers write theirs; diff the two.
//! - **Resync** (N46): the host has every peer reload its last resync point on a new epoch, the N10 path
//!   ([`crate::recover`]: the point goes in the room, every peer restarts from it). A resync point is the host's
//!   board state at the start of every player's turn ([`pbnet_resync_point`]) or after a minigame (the online save,
//!   N9), whichever came last. Only the host decides: it compares its hashes with every peer's, and two peers that
//!   differ can't both agree with it, so it sees every mismatch itself. A guest writes its log, shows "Out of
//!   sync, going back to the last save point..." ([`RESYNC_TEXT`], N44) and waits for the host's reload.
//! - **Never runs on desynced, rate-limited**: every desync ends in a resync. One that comes back from the same point
//!   (a deterministic bug a reload can't fix) waits [`Resync::delay`] first, doubling from [`BACKOFF_FIRST`] up to
//!   [`BACKOFF_MAX`]; the host's game holds meanwhile (so every peer's does: they lack its pads), tells every peer
//!   how long ([`crate::lobby::Msg::Held`], shown as [`held_text`], N44), and [`pbnet_resync_poll`] fires the reload
//!   when the wait is over.
//!
//! Whole-state transfer (the host's state at the current frame, as opentennis does) was spiked and is not possible
//! here yet: research/journal/2026-10-27-n46-desync-resync.

use std::path::{Path, PathBuf};
use std::time::{Duration, SystemTime};

use serde::Serialize;

use crate::lobby::Checkpoint;
use crate::lockstep::{Desync, InputLog};

/// The wait before the second resync from one point; each further one doubles it.
pub const BACKOFF_FIRST: Duration = Duration::from_secs(2);
/// The longest wait before a resync.
pub const BACKOFF_MAX: Duration = Duration::from_secs(16);

/// The host's count of resyncs from the point it reloads.
#[derive(Clone, Debug, Default)]
pub struct Resync {
    /// The blob reloaded last (`Some(None)`: the match start), and how many times.
    from: Option<Option<String>>,
    count: u32,
    /// Resyncs asked so far, from any point.
    pub done: u32,
}

impl Resync {
    /// A desync is to reload `checkpoint` (None: the match start): how long to hold first. The first resync from a
    /// point goes at once; each one more from the same point waits twice as long, up to [`BACKOFF_MAX`]. Never
    /// refuses: running on desynced is not an option.
    pub fn delay(&mut self, checkpoint: Option<&Checkpoint>) -> Duration {
        let key = checkpoint.map(|c| c.blob.clone());
        if self.from.as_ref() != Some(&key) {
            self.from = Some(key);
            self.count = 0;
        }
        self.count += 1;
        self.done += 1;
        match self.count {
            1 => Duration::ZERO,
            n => BACKOFF_FIRST.saturating_mul(1 << (n - 2).min(16)).min(BACKOFF_MAX),
        }
    }
}

/// What the players see while a desync reload runs (N44).
pub const RESYNC_TEXT: &str = "Out of sync, going back to the last save point...";

/// What the players see when going back to a save point did not mend a desync (the same point desynced again): the
/// game holds `secs` seconds before it tries again (N44; the reloads never run out since N46, they back off).
pub fn held_text(secs: u32) -> String {
    let unit = if secs == 1 { "second" } else { "seconds" };
    format!("Still out of sync after going back to the last save point. Trying again in {secs} {unit}...")
}

/// A resync point: the board blob of the start of a turn as a [`Checkpoint`].
pub fn point(blob: &[u8], turn: u8, max_turn: u8) -> Checkpoint {
    let time = SystemTime::now().duration_since(SystemTime::UNIX_EPOCH).unwrap_or_default().as_secs();
    Checkpoint { blob: crate::save::hex(blob), turn, max_turn, time }
}

/// The mismatch as written out.
#[derive(Clone, Debug, PartialEq, Eq, Serialize)]
pub struct Mismatch {
    pub frame: u32,
    /// The other peer's line-up index.
    pub peer: u8,
    pub ours: String,
    pub theirs: String,
}

/// A `desync-*.json` file.
#[derive(Clone, Debug, PartialEq, Eq, Serialize)]
pub struct Report {
    /// The match id (empty without a lobby: a debug run).
    pub id: String,
    pub host: bool,
    pub epoch: u32,
    /// When it was written (Unix seconds).
    pub time: u64,
    pub desync: Mismatch,
    pub log: InputLog,
}

impl Report {
    pub fn new(id: &str, host: bool, epoch: u32, d: &Desync, log: InputLog) -> Report {
        Report {
            id: id.into(),
            host,
            epoch,
            time: SystemTime::now().duration_since(SystemTime::UNIX_EPOCH).unwrap_or_default().as_secs(),
            desync: Mismatch { frame: d.frame, peer: d.peer, ours: format!("{:016x}", d.ours), theirs: format!("{:016x}", d.theirs) },
            log,
        }
    }

    /// `desync-<match>-<time>-e<epoch>-f<frame>-p<index>.json` (the match id kept to letters, digits and `-`).
    pub fn file_name(&self) -> String {
        let id: String = self.id.chars().filter(|c| c.is_ascii_alphanumeric() || *c == '-').take(40).collect();
        let id = if id.is_empty() { "local".into() } else { id };
        format!("desync-{id}-{}-e{}-f{}-p{}.json", self.time, self.epoch, self.desync.frame, self.log.me)
    }

    /// Writes it into `dir`; the file's path.
    pub fn write(&self, dir: &Path) -> std::io::Result<PathBuf> {
        let path = dir.join(self.file_name());
        // pretty: one frame's pads per line, so two peers' files diff by frame
        let json = serde_json::to_string_pretty(self).map_err(std::io::Error::other)?;
        std::fs::write(&path, json)?;
        Ok(path)
    }
}

// ---------------------------------------------------------------- C ABI

/// A hash mismatch from `pbnet_poll_desync`: the first one of a lockstep session writes its input log to
/// `<user dir>/desync-*.json` (the current directory if `pbnet_recover_set_dir` set none). On the host every one asks
/// every peer to resync from the last resync point (`pbnet_recover_epoch` moves; `netplay_lobby_poll` starts it as any
/// reload), unless one is under way; one more from the same point holds the game first and goes from
/// `pbnet_resync_poll`. A guest shows "Out of sync, going back to the last save point..." and waits for the host. Returns 1 if a resync was
/// asked now, else 0 (a guest, no lobby, one under way or held back).
///
/// # Safety
///
/// No pointers: safe to call any time, from any thread (meant for the game thread, after `pbnet_poll_desync`).
#[no_mangle]
pub extern "C" fn pbnet_desync_recover(frame: u32, peer: i32, ours: u64, theirs: u64) -> i32 {
    if let Some(log) = crate::lockstep_c::input_log_once() {
        let d = Desync { frame, peer: peer.clamp(0, 255) as u8, ours, theirs };
        let (id, host, epoch) = crate::lobby_c::with_global(|l| {
            let s = l.snapshot();
            (s.room.map(|r| r.id).unwrap_or_default(), s.host, l.epoch())
        })
        .unwrap_or_default();
        let report = Report::new(&id, host, epoch, &d, log);
        let dir = crate::recover::user_dir().unwrap_or_else(|| PathBuf::from("."));
        match report.write(&dir) {
            Ok(p) => eprintln!("pbnet: desync at frame {frame} with peer {peer}: input log {}", p.display()),
            Err(e) => eprintln!("pbnet: desync at frame {frame} with peer {peer}: input log not written: {e}"),
        }
    }
    crate::lobby_c::with_global(|l| l.desync()).unwrap_or(false) as i32
}

/// N46: call once per loop on the game thread (`netplay_lobby_poll` does). On the host, a resync held back by its
/// wait goes once the wait is over (`pbnet_recover_epoch` moves). Returns 1 if it went, else 0.
///
/// # Safety
///
/// No pointers: safe to call any time, from any thread.
#[no_mangle]
pub extern "C" fn pbnet_resync_poll() -> i32 {
    crate::lobby_c::with_global(|l| l.resync_poll()).unwrap_or(false) as i32
}

/// N46: the board state at the start of a player's turn (`blob`: the `SLSaveBoard` blob, as `pbnet_save_write`
/// takes it; `turn`/`max_turn` of GWSystem) is this machine's newest resync point of the started match: the host's
/// next desync resync reloads it (N149: and a player who joined the match since takes a seat there, every peer
/// reloading it at once). Nothing is written to disk. Returns 1 if taken, 0 if not (no started lobby, no
/// blob or one too big).
///
/// # Safety
///
/// `blob` is null or points to `len` readable bytes, used during the call only. Thread-safe.
#[no_mangle]
pub unsafe extern "C" fn pbnet_resync_point(blob: *const u8, len: i32, turn: i32, max_turn: i32) -> i32 {
    if blob.is_null() || len <= 0 || len as usize > crate::save::BLOB_MAX {
        return 0;
    }
    let Some(room) = crate::lobby_c::started_room() else { return 0 };
    // SAFETY: the caller guarantees `len` readable bytes at `blob`.
    let blob = unsafe { std::slice::from_raw_parts(blob, len as usize) };
    crate::recover::point_taken(&room.id, point(blob, turn.clamp(0, 255) as u8, max_turn.clamp(0, 255) as u8));
    // N149: players who joined since take their seats from this point
    crate::lobby_c::with_global(|l| l.point());
    1
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::lockstep::fake::{End, Net};
    use crate::lockstep::{Config, Pad, Session, Step, HASH_EVERY, IDLE, NO_PEER};
    use crate::recover::Epoch;

    fn cp(blob: &str) -> Checkpoint {
        Checkpoint { blob: blob.into(), turn: 2, max_turn: 20, time: 1 }
    }

    #[test]
    fn a_resync_never_is_refused_and_repeats_back_off() {
        let mut r = Resync::default();
        assert_eq!(r.delay(None), Duration::ZERO, "the first from a point goes at once");
        assert_eq!(r.delay(None), BACKOFF_FIRST);
        assert_eq!(r.delay(None), BACKOFF_FIRST * 2);
        assert_eq!(r.delay(None), BACKOFF_FIRST * 4);
        for _ in 0..40 {
            assert_eq!(r.delay(None), BACKOFF_MAX, "capped, never refused");
        }
        assert_eq!(r.delay(Some(&cp("aa"))), Duration::ZERO, "a new point starts over");
        assert_eq!(r.delay(Some(&cp("aa"))), BACKOFF_FIRST);
        assert_eq!(r.delay(Some(&cp("bb"))), Duration::ZERO);
        assert_eq!(r.done, 47);
    }

    #[test]
    fn the_texts_say_what_happens() {
        assert_eq!(RESYNC_TEXT, "Out of sync, going back to the last save point...");
        assert_eq!(held_text(2), "Still out of sync after going back to the last save point. Trying again in 2 seconds...");
        assert!(held_text(1).ends_with("in 1 second..."));
        assert_eq!(point(&[1, 0xab], 3, 20), Checkpoint { blob: "01ab".into(), turn: 3, max_turn: 20, time: point(&[], 0, 0).time });
    }

    /// The game of one peer on the fake wire: its state folds the pads of every frame it runs, and the host keeps a
    /// resync point every `TURN` frames (the start of a turn).
    struct Peer {
        s: Session<Epoch<End>>,
        state: u64,
        /// Hashes this peer set, by frame of the current session.
        hashes: Vec<(u32, u64)>,
    }

    const TURN: u32 = 240;

    fn fold(state: u64, pads: &[Pad; 4]) -> u64 {
        pads.iter().fold(state, |s, q| (s ^ q.button as u64).wrapping_mul(0x100000001b3))
    }

    fn input(me: usize, f: u32) -> Pad {
        Pad { button: ((f / 7 + me as u32 * 3) % 5) as u16, ..IDLE }
    }

    /// A forced divergence on the guest mid-session: the host sees the mismatch, has everyone resync from its last
    /// point on a new epoch, and play goes on in step with equal hashes, never running the desynced session further.
    /// A divergence that comes back every time (a deterministic bug) is resynced again and again, each after a
    /// longer hold, never left running.
    #[test]
    fn a_forced_mismatch_resyncs_and_play_goes_on_with_equal_hashes() {
        let net = Net::new(30, 0.02, 9);
        let cfg = |me: u8| Config { me, peers: 2, seats: [0, 1, NO_PEER, NO_PEER], delay: 4 };
        let session = |me: u8, epoch: u32| Session::new(cfg(me), Epoch { inner: net.end(me), epoch });
        let mut peers: Vec<Peer> = (0..2).map(|me| Peer { s: session(me, 0), state: 1, hashes: vec![] }).collect();
        let mut epoch = 0u32;
        let mut resync = Resync::default();
        // the host's resync point: (blob = its state, the frame of the session it was taken at)
        let mut point_state = 1u64;
        let mut now = 0u64;
        let mut held_until: Option<u64> = None;
        let mut resyncs = vec![];
        // the guest corrupts its state at this frame of every session (None: never again)
        let mut corrupt_at: Option<u32> = Some(500);
        let mut corrupt_every_session = false;
        let mut ran_after_desync = 0;
        let mut desync_seen = false;
        // a guest takes the reload this many ms after the host (the Reload message is under way)
        const LATE_MS: u64 = 80;
        let mut pending_guest: Option<(u64, u32, u64)> = None;
        let mut frames_since_resync = 0u32;
        for _ in 0..12_000 {
            now += 16;
            net.set_now(now);
            if let Some((at, e, st)) = pending_guest {
                if now >= at {
                    peers[1] = Peer { s: session(1, e), state: st, hashes: vec![] };
                    pending_guest = None;
                }
            }
            if let Some(t) = held_until {
                if now < t {
                    continue; // the host holds; the guest can't run without its pads
                }
                held_until = None;
                epoch += 1;
                peers[0] = Peer { s: session(0, epoch), state: point_state, hashes: vec![] };
                pending_guest = Some((now + LATE_MS, epoch, point_state));
                resyncs.push(now);
                frames_since_resync = 0;
                desync_seen = false;
                if !corrupt_every_session {
                    corrupt_at = None;
                }
            }
            for (i, p) in peers.iter_mut().enumerate() {
                if i == 1 && pending_guest.is_some() {
                    continue; // still loading the host's point
                }
                let f = p.s.frame();
                if f % HASH_EVERY == 0 && p.hashes.last().map(|h| h.0) != Some(f) {
                    p.s.set_hash(f, p.state);
                    p.hashes.push((f, p.state));
                }
                if let Step::Ready(pads) = p.s.step(now, &[input(i, f)]) {
                    if i == 0 && desync_seen {
                        ran_after_desync += 1;
                    }
                    p.state = fold(p.state, &pads);
                    let f = p.s.frame();
                    if i == 0 && f % TURN == 0 {
                        point_state = p.state;
                    }
                    if i == 1 && Some(f) == corrupt_at {
                        p.state ^= 1; // the forced divergence (a flipped seed)
                    }
                    if i == 0 {
                        frames_since_resync += 1;
                    }
                }
            }
            // the host polls its mismatches (the guest sees them too, and only waits)
            while let Some(d) = peers[0].s.desync() {
                assert_eq!(d.peer, 1);
                if desync_seen || held_until.is_some() {
                    continue;
                }
                desync_seen = true;
                let wait = resync.delay(Some(&cp(&format!("{point_state:x}"))));
                held_until = Some(now + wait.as_millis() as u64);
            }
            while peers[1].s.desync().is_some() {}
            // phase 2 once the first resync ran a while in step: the bug comes back every session
            if resyncs.len() == 1 && frames_since_resync == 1500 {
                let (a, b) = (&peers[0].hashes, &peers[1].hashes);
                let n = a.len().min(b.len());
                assert!(n > 40, "{n} hashes after the resync");
                assert_eq!(a[..n], b[..n], "in step after the resync");
                corrupt_every_session = true;
                corrupt_at = Some(peers[1].s.frame() + 100);
            }
        }
        assert!(resyncs.len() >= 4, "{resyncs:?}");
        assert_eq!(ran_after_desync, 0, "the host never ran a frame of a session it knew was desynced");
        // the first resync went at once, the repeats from one point waited longer and longer
        let gaps: Vec<u64> = resyncs.windows(2).map(|w| w[1] - w[0]).collect();
        assert!(gaps.windows(2).skip(1).all(|g| g[1] >= g[0]), "{gaps:?}");
        assert!(*gaps.last().unwrap() >= BACKOFF_FIRST.as_millis() as u64 * 2, "{gaps:?}");
    }

    #[test]
    fn a_report_is_written_with_the_log() {
        let net = Net::new(0, 0.0, 1);
        let mut s = Session::new(Config { me: 1, peers: 2, seats: [0, 1, NO_PEER, NO_PEER], delay: 2 }, net.end(1));
        s.set_hash(0, 7);
        assert_eq!(s.step(0, &[IDLE]), Step::Ready([IDLE; 4]));
        let d = Desync { frame: 0, peer: 0, ours: 7, theirs: 0x10 };
        let r = Report::new("m/1 x", false, 2, &d, s.input_log());
        assert_eq!(r.file_name(), format!("desync-m1x-{}-e2-f0-p1.json", r.time));
        assert_eq!(Report::new("", true, 0, &d, s.input_log()).file_name(), format!("desync-local-{}-e0-f0-p1.json", r.time));
        let dir = std::env::temp_dir().join(format!("pbnet-desync-{}", std::process::id()));
        std::fs::create_dir_all(&dir).unwrap();
        let p = r.write(&dir).unwrap();
        let v: serde_json::Value = serde_json::from_str(&std::fs::read_to_string(&p).unwrap()).unwrap();
        assert_eq!(v["desync"]["ours"], "0000000000000007");
        assert_eq!(v["desync"]["theirs"], "0000000000000010");
        assert_eq!(v["log"]["me"], 1);
        assert_eq!(v["log"]["frames"].as_array().unwrap().len(), 1);
        assert_eq!(v["log"]["hashes"][0][1], "0000000000000007");
        let _ = std::fs::remove_dir_all(&dir);
    }
}
