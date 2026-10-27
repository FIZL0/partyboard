//! Desync recovery (N11): a hash mismatch never ends a match.
//!
//! - **Input log**: the first mismatch a lockstep session finds writes its input log
//!   ([`crate::lockstep::Session::input_log`]: the pads of the frames it ran and its hashes) to
//!   `<user dir>/desync-<match>-<time>-e<epoch>-f<frame>-p<index>.json`. A mismatch is seen on both sides (each
//!   compares the other's hashes with its own), so both peers write theirs; diff the two.
//! - **Reload**: the host reloads its last checkpoint on a new epoch, the N10 path ([`crate::recover`]: the
//!   checkpoint goes in the room, every peer restarts from it). Only the host decides: it compares its hashes with
//!   every peer's, and two peers that differ can't both agree with it, so it sees every mismatch itself. A guest
//!   only writes its log and waits for the host's reload.
//! - **No loop**: a desync that comes back from the same checkpoint is a deterministic bug a reload can't fix. The
//!   host reloads at most [`RELOADS_PER_CHECKPOINT`] times from one checkpoint ([`Resync`]); after that the match
//!   runs on (its logs written) until the next checkpoint.

use std::path::{Path, PathBuf};
use std::time::SystemTime;

use serde::Serialize;

use crate::lobby::Checkpoint;
use crate::lockstep::{Desync, InputLog};

/// Desync reloads the host does from one checkpoint (or from the match start).
pub const RELOADS_PER_CHECKPOINT: u32 = 3;

/// The host's count of desync reloads from the checkpoint it reloads.
#[derive(Clone, Debug, Default)]
pub struct Resync {
    /// The blob reloaded last (`Some(None)`: the match start), and how many times.
    from: Option<Option<String>>,
    count: u32,
}

impl Resync {
    /// Whether a desync may reload `checkpoint` (None: the match start) once more; counts it if so.
    pub fn allow(&mut self, checkpoint: Option<&Checkpoint>) -> bool {
        let key = checkpoint.map(|c| c.blob.clone());
        if self.from.as_ref() != Some(&key) {
            self.from = Some(key);
            self.count = 0;
        }
        if self.count >= RELOADS_PER_CHECKPOINT {
            return false;
        }
        self.count += 1;
        true
    }
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
/// `<user dir>/desync-*.json` (the current directory if `pbnet_recover_set_dir` set none), and on the host asks
/// every peer to reload the last checkpoint (`pbnet_recover_epoch` moves; `netplay_lobby_poll` starts it as any
/// reload). Later mismatches of the same session do nothing (the reload is under way, or was refused). Returns 1 if
/// a reload was asked, else 0 (a guest, no lobby, a later mismatch, or the reloads from this checkpoint ran out).
///
/// # Safety
///
/// No pointers: safe to call any time, from any thread (meant for the game thread, after `pbnet_poll_desync`).
#[no_mangle]
pub extern "C" fn pbnet_desync_recover(frame: u32, peer: i32, ours: u64, theirs: u64) -> i32 {
    let Some(log) = crate::lockstep_c::input_log_once() else { return 0 };
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
    crate::lobby_c::with_global(|l| l.desync()).unwrap_or(false) as i32
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::lockstep::fake::Net;
    use crate::lockstep::{Config, Session, Step, IDLE, NO_PEER};

    fn cp(blob: &str) -> Checkpoint {
        Checkpoint { blob: blob.into(), turn: 2, max_turn: 20, time: 1 }
    }

    #[test]
    fn reloads_from_one_checkpoint_run_out() {
        let mut r = Resync::default();
        for _ in 0..RELOADS_PER_CHECKPOINT {
            assert!(r.allow(None));
        }
        assert!(!r.allow(None), "the match start keeps desyncing");
        assert!(r.allow(Some(&cp("aa"))), "a new checkpoint starts over");
        for _ in 1..RELOADS_PER_CHECKPOINT {
            assert!(r.allow(Some(&cp("aa"))));
        }
        assert!(!r.allow(Some(&cp("aa"))));
        assert!(r.allow(Some(&cp("bb"))));
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
