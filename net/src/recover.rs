//! Drops never end a match (N10).
//!
//! - **Keep-alive and hold**: every lockstep step sends every peer a datagram, also while it waits, so a peer that
//!   still runs is heard every frame ([`crate::lockstep::Session::quiet_ms`]). One silent for [`SILENCE_MS`] is shown
//!   as "Waiting for <name>" ([`Status`]); the lockstep holds on its own (it has no pads from it).
//! - **Same app, new connection**: a guest whose connection drops redials the host with its rejoin token
//!   ([`crate::lobby::Msg::Rejoin`], `intact`). Its lockstep session still runs, so the host only points its route
//!   at the new connection ([`Outcome::Resume`]): every peer resends the frames the other has not acknowledged (the
//!   input log since the drop), and the match goes on from the frame where it held.
//! - **State lost** (app restarted, or another device): the rejoin is not `intact`. Every peer reloads the last
//!   board checkpoint (N9) and runs a new lockstep session from its frame 0 ([`Outcome::Reload`],
//!   [`crate::lobby::Msg::Reload`], numbered by an epoch so a datagram of the old session is never taken for the new
//!   one: [`tag`]/[`untag`]). The host sends the checkpoint in the room, so the rejoiner needs no save of its own.
//! - **CPU stand-in**: the host may give a waiting peer's seats to the computer ([`stand_in`]); that is a reload
//!   too (the game reads who plays a seat when the board starts, not mid-turn). The player takes the seats back on
//!   rejoin by its token, with one more reload. The host may also save and quit: every peer already holds the last
//!   checkpoint's save, and guests show [`HOST_LEFT`]. Nothing gives up on its own.
//! - **Rejoin token** (kept like opentennis `net/rejoin.rs`): the host gives each guest a random token at the start
//!   ([`Book::issue`]); the guest keeps it with the host's code and the match id in `<user dir>/rejoin.txt`
//!   ([`Rejoin`]), written again while the match runs ([`keep`]), so a restarted app can rejoin
//!   (`pbnet_lobby_rejoin`). The host can't rejoin (there is no other host), so it never writes one.
//!
//! The lobby task (`lobby_c.rs`) moves the messages; everything deciding here is sans-IO and tested on the fake wire.

use std::collections::BTreeMap;
use std::ffi::{c_char, CStr};
use std::hash::{BuildHasher, Hasher};
use std::path::{Path, PathBuf};
use std::sync::Mutex;
use std::time::SystemTime;

use serde::{Deserialize, Serialize};

use crate::lobby::{Checkpoint, Room, CPU, HOST, SEATS};
use crate::lockstep::Transport;
use crate::save::Save;

/// A peer silent this long (ms) is shown as waited for.
pub const SILENCE_MS: u64 = 3000;
/// How long after it was last written a rejoin record still rejoins (s). The match holds for as long as it takes;
/// a record this old is from a match whose host has long quit (its code no longer answers).
pub const FRESH: u64 = 12 * 3600;
/// How often [`keep`] writes the record again while the match goes on (s).
pub const KEEP: u64 = 60;
/// What a guest shows when the host is gone (it saved and quit, or its connection dropped).
pub const HOST_LEFT: &str = "Host left: the match is saved, any player can host Continue";
/// The suffix of a seat's name while the computer stands in for its player.
const STAND_IN: &str = " (CPU)";

fn unix_secs() -> u64 {
    SystemTime::now().duration_since(SystemTime::UNIX_EPOCH).unwrap_or_default().as_secs()
}

/// A random token: 128 bits from the standard library's per-process random hash keys, in hex.
pub fn token() -> String {
    let a = std::collections::hash_map::RandomState::new().build_hasher().finish();
    let mut h = std::collections::hash_map::RandomState::new().build_hasher();
    h.write_u64(a);
    h.write_u128(SystemTime::now().duration_since(SystemTime::UNIX_EPOCH).unwrap_or_default().as_nanos());
    format!("{a:016x}{:016x}", h.finish())
}

// ---------------------------------------------------------------- the rejoin record (guest)

/// The match this machine plays in as a guest, kept so a restarted app can rejoin it.
#[derive(Clone, Debug, PartialEq, Eq, Serialize, Deserialize)]
pub struct Rejoin {
    /// The code the host was dialled with (its ticket or short code).
    pub code: String,
    /// The match id ([`Room::id`]).
    pub id: String,
    /// The host's rejoin token for this machine's seats.
    pub token: String,
    /// When it was written (Unix seconds).
    pub at: u64,
}

/// `<user dir>/rejoin.txt` (beside the config, as opentennis's beside settings.txt).
pub fn record_path(user_dir: &Path) -> PathBuf {
    user_dir.join("rejoin.txt")
}

fn decode(text: &str, now: u64) -> Option<Rejoin> {
    let r: Rejoin = serde_json::from_str(text).ok()?;
    let fresh = now >= r.at && now - r.at < FRESH;
    (fresh && !r.code.is_empty() && !r.token.is_empty()).then_some(r)
}

/// Remembers `r` (its time set to now).
pub fn save_record(user_dir: &Path, r: &Rejoin) {
    let r = Rejoin { at: unix_secs(), ..r.clone() };
    if let Ok(json) = serde_json::to_string(&r) {
        if let Err(e) = std::fs::write(record_path(user_dir), json) {
            eprintln!("pbnet: rejoin record: {e}");
        }
    }
}

/// The match to rejoin, if one was kept lately (it stays until [`forget`]).
pub fn read_record(user_dir: &Path) -> Option<Rejoin> {
    decode(&std::fs::read_to_string(record_path(user_dir)).ok()?, unix_secs())
}

/// The match still goes on: the record (if there is a fresh one) written again with the time now, at most every
/// [`KEEP`] seconds (`last` is the caller's: when it last did).
pub fn keep(user_dir: &Path, last: &mut u64) {
    let now = unix_secs();
    if now.saturating_sub(*last) >= KEEP {
        *last = now;
        if let Some(r) = read_record(user_dir) {
            save_record(user_dir, &r);
        }
    }
}

/// The match is over, or this machine left it.
pub fn forget(user_dir: &Path) {
    let p = record_path(user_dir);
    if p.exists() {
        let _ = std::fs::remove_file(p);
    }
}

// ---------------------------------------------------------------- the host's book

/// What the host does with a rejoin.
#[derive(Clone, Debug, PartialEq, Eq)]
pub enum Outcome {
    /// Its session runs on: route line-up index `index` to the new connection (whose peer name the room and the
    /// line-up now have) and tell it [`crate::lobby::Msg::Resumed`].
    Resume { index: usize },
    /// It needs the current epoch's reload (it missed it while away): send it the room and epoch, nobody else.
    Join,
    /// Every peer reloads the last checkpoint with the room as changed.
    Reload,
    Refused(String),
}

/// The host's tokens: which players' seats each one gives back, and the reload count.
#[derive(Clone, Debug, Default)]
pub struct Book {
    /// token → the player keys of the seats it was issued for.
    tokens: BTreeMap<String, Vec<String>>,
    /// The lockstep session's number: 0 from the start, one more per reload.
    pub epoch: u32,
}

impl Book {
    /// Tokens for every guest of the started `room`: (peer name, token).
    pub fn issue(&mut self, room: &Room) -> Vec<(String, String)> {
        let mut out = vec![];
        for peer in room.peers().into_iter().skip(1) {
            let keys: Vec<String> = room.seats_of(&peer).iter().filter_map(|&i| room.seats[i].as_ref()).map(|s| s.key.clone()).collect();
            if keys.iter().all(String::is_empty) {
                continue;
            }
            let t = token();
            self.tokens.insert(t.clone(), keys);
            out.push((peer, t));
        }
        out
    }

    /// `peer` (a new connection's name) is back with `token`, its session of `epoch` still running if `intact`.
    /// `room` and `lineup` (the running session's) are changed as the outcome says.
    pub fn rejoin(&self, room: &mut Room, lineup: &mut [String], token: &str, peer: &str, intact: bool, epoch: u32) -> Outcome {
        let Some(keys) = self.tokens.get(token) else { return Outcome::Refused("you are not in this match".into()) };
        if peer == HOST || peer == CPU {
            return Outcome::Refused("already in this match".into());
        }
        let seats: Vec<usize> =
            (0..SEATS).filter(|&i| room.seats[i].as_ref().is_some_and(|s| !s.key.is_empty() && keys.contains(&s.key))).collect();
        if seats.is_empty() {
            return Outcome::Refused("your seats are no longer in this match".into());
        }
        let olds: Vec<String> = seats.iter().map(|&i| room.seats[i].as_ref().unwrap().peer.clone()).collect();
        let old = &olds[0];
        let held = olds.iter().all(|p| p == old) && old != CPU;
        if held {
            if let Some(index) = lineup.iter().position(|p| p == old) {
                if old != peer {
                    lineup[index] = peer.into();
                    for &i in &seats {
                        room.seats[i].as_mut().unwrap().peer = peer.into();
                    }
                }
                if intact && epoch == self.epoch {
                    return Outcome::Resume { index };
                }
                if intact {
                    // it missed a reload while away: the others wait for it at the start of this epoch, only it
                    // has to catch up
                    return Outcome::Join;
                }
                // it lost its state: nobody can tell how far the others got, everyone goes back to the checkpoint
                return Outcome::Reload;
            }
        }
        // its seats were the computer's (or another connection's): it takes them back, everyone reloads
        for &i in &seats {
            let s = room.seats[i].as_mut().unwrap();
            s.peer = peer.into();
            s.cpu = false;
            if let Some(n) = s.name.strip_suffix(STAND_IN) {
                s.name = n.into();
            }
        }
        Outcome::Reload
    }
}

/// The host gives `peer`'s seats to the computer (it keeps their keys, so the player takes them back on rejoin).
/// Whether it had any (then every peer reloads).
pub fn stand_in(room: &mut Room, peer: &str) -> bool {
    if peer == HOST || peer == CPU {
        return false;
    }
    let seats = room.seats_of(peer);
    for &i in &seats {
        let s = room.seats[i].as_mut().unwrap();
        s.peer = CPU.into();
        s.cpu = true;
        if !s.name.ends_with(STAND_IN) {
            s.name.push_str(STAND_IN);
        }
    }
    !seats.is_empty()
}

/// The room every peer reloads: `room` with the match's last checkpoint (`last`, if it is this match's), else the
/// one it already has (a continue's), else none (the match restarts from its first frame).
pub fn reload_room(room: &Room, last: Option<Checkpoint>) -> Room {
    let mut r = room.clone();
    if last.is_some() {
        r.checkpoint = last;
    }
    r.ready.clear();
    r
}

/// The last checkpoint this process saved (`pbnet_save_write`): match id and checkpoint.
static LAST: Mutex<Option<(String, Checkpoint)>> = Mutex::new(None);

/// An online save was written (`save.rs`): the host's reloads send its checkpoint.
pub fn checkpoint_written(s: &Save) {
    *LAST.lock().unwrap_or_else(|e| e.into_inner()) = Some((s.id.clone(), s.checkpoint()));
}

/// N46: the host's resync point of match `id` at the start of a turn (`pbnet_resync_point`): a reload takes it as
/// it takes the last online save.
pub fn point_taken(id: &str, c: Checkpoint) {
    *LAST.lock().unwrap_or_else(|e| e.into_inner()) = Some((id.to_string(), c));
}

/// The last checkpoint saved for match `id`.
pub fn last_checkpoint(id: &str) -> Option<Checkpoint> {
    LAST.lock().unwrap_or_else(|e| e.into_inner()).as_ref().filter(|(i, _)| i == id).map(|(_, c)| c.clone())
}

// ---------------------------------------------------------------- epochs on the wire

/// A lockstep datagram of session `epoch`: the epoch's low byte in front.
pub fn tag(epoch: u32, d: &[u8]) -> Vec<u8> {
    let mut out = Vec::with_capacity(d.len() + 1);
    out.push(epoch as u8);
    out.extend_from_slice(d);
    out
}

/// A [`tag`]ged datagram's lockstep bytes if it is of session `epoch` (an old session's is dropped).
pub fn untag(epoch: u32, d: &[u8]) -> Option<&[u8]> {
    match d.split_first() {
        Some((&e, rest)) if e == epoch as u8 => Some(rest),
        _ => None,
    }
}

/// A lockstep transport carrying session `epoch` only.
pub struct Epoch<T> {
    pub inner: T,
    pub epoch: u32,
}

impl<T: Transport> Transport for Epoch<T> {
    fn send(&mut self, to: u8, d: &[u8]) {
        self.inner.send(to, &tag(self.epoch, d));
    }
    fn recv(&mut self) -> Option<(u8, Vec<u8>)> {
        while let Some((from, d)) = self.inner.recv() {
            if let Some(d) = untag(self.epoch, &d) {
                return Some((from, d.to_vec()));
            }
        }
        None
    }
}

// ---------------------------------------------------------------- what the match shows

/// One peer the match waits for.
#[derive(Clone, Debug, PartialEq, Eq, Serialize)]
pub struct Waiting {
    /// Its line-up index (`pbnet_recover_cpu`'s argument).
    pub index: u8,
    /// Its players' names.
    pub name: String,
    pub seats: Vec<usize>,
    pub quiet_ms: u64,
}

/// The recovery as the match's UI sees it (`pbnet_recover_json`).
#[derive(Clone, Debug, Default, PartialEq, Eq, Serialize)]
pub struct Status {
    /// A started match runs (this machine is in it).
    pub running: bool,
    pub host: bool,
    pub epoch: u32,
    /// The peers silent for [`SILENCE_MS`] or more.
    pub waiting: Vec<Waiting>,
    /// Guest: reconnecting to the host (its connection dropped).
    pub reconnecting: bool,
    /// Guest: the host is gone ([`HOST_LEFT`] in `message`).
    pub host_left: bool,
    /// A reload is being set up.
    pub reloading: bool,
    /// What to tell the player, if anything.
    pub message: String,
    /// N46: a desync is being mended: "Resyncing with <host>..." (a short notice, play goes on), else empty.
    pub resync: String,
    /// N46, host: resyncs asked so far in this lobby (a new one is a new notice).
    pub resyncs: u32,
}

/// The peers of a running match's `lineup` silent for [`SILENCE_MS`]: `quiet` gives a line-up index's silence (ms).
pub fn waiting(room: &Room, lineup: &[String], me: &str, quiet: impl Fn(u8) -> Option<u64>) -> Vec<Waiting> {
    let mut out = vec![];
    for (i, p) in lineup.iter().enumerate() {
        if p == me {
            continue;
        }
        let Some(q) = quiet(i as u8).filter(|&q| q >= SILENCE_MS) else { continue };
        let seats = room.seats_of(p);
        let name = seats.iter().filter_map(|&s| room.seats[s].as_ref()).map(|s| s.name.as_str()).collect::<Vec<_>>().join(", ");
        out.push(Waiting { index: i as u8, name: if name.is_empty() { p.clone() } else { name }, seats, quiet_ms: q });
    }
    out
}

// ---------------------------------------------------------------- C ABI

/// The user dir for the rejoin record (set by the menu, `pbnet_recover_set_dir`).
static USER_DIR: Mutex<Option<PathBuf>> = Mutex::new(None);

pub fn user_dir() -> Option<PathBuf> {
    USER_DIR.lock().unwrap_or_else(|e| e.into_inner()).clone()
}

/// A C string argument (None: null or not UTF-8).
///
/// # Safety
///
/// `p` is null or a valid NUL-terminated string for the call.
unsafe fn arg<'a>(p: *const c_char) -> Option<&'a str> {
    if p.is_null() {
        return None;
    }
    // SAFETY: the caller guarantees a valid NUL-terminated string.
    unsafe { CStr::from_ptr(p) }.to_str().ok()
}

/// Copies `s` into `buf` as snprintf does; returns `s`'s length.
///
/// # Safety
///
/// `buf` is null or points to `cap` writable bytes.
unsafe fn put(s: &str, buf: *mut c_char, cap: i32) -> i32 {
    if !buf.is_null() && cap > 0 {
        let n = s.len().min(cap as usize - 1);
        // SAFETY: the caller guarantees `cap` writable bytes at `buf`; n + 1 <= cap.
        unsafe {
            std::ptr::copy_nonoverlapping(s.as_ptr(), buf.cast::<u8>(), n);
            *buf.add(n) = 0;
        }
    }
    s.len().min(i32::MAX as usize) as i32
}

/// Sets the user dir the rejoin record lives in (`<dir>/rejoin.txt`); call before hosting or joining.
///
/// # Safety
///
/// `dir` is null (no record is kept) or a valid NUL-terminated UTF-8 string, read during the call only.
/// Thread-safe.
#[no_mangle]
pub unsafe extern "C" fn pbnet_recover_set_dir(dir: *const c_char) {
    // SAFETY: the caller's contract on `dir`.
    let d = unsafe { arg(dir) }.filter(|d| !d.is_empty()).map(PathBuf::from);
    *USER_DIR.lock().unwrap_or_else(|e| e.into_inner()) = d;
}

/// The match this machine can rejoin as JSON (`{"code","id","token","at"}`), snprintf-style; 0 (and nothing
/// written) if there is none (no fresh record, or no user dir set).
///
/// # Safety
///
/// `buf` is null (only the length is returned) or points to `cap` writable bytes, during the call only. Thread-safe.
#[no_mangle]
pub unsafe extern "C" fn pbnet_rejoin_record(buf: *mut c_char, cap: i32) -> i32 {
    let Some(r) = user_dir().and_then(|d| read_record(&d)) else { return 0 };
    let json = serde_json::to_string(&r).unwrap_or_default();
    // SAFETY: the caller's contract on `buf` and `cap`.
    unsafe { put(&json, buf, cap) }
}

/// Forgets the rejoin record (the player won't rejoin). Safe to call any time, from any thread.
#[no_mangle]
pub extern "C" fn pbnet_rejoin_forget() {
    if let Some(d) = user_dir() {
        forget(&d);
    }
}

/// Rejoins the match of the rejoin record (replacing a lobby that runs): 1 if dialling, 0 if `setup` isn't valid
/// or there is no record. The host answers with a reload: the lobby goes to `PBNET_LOBBY_STARTED` with the match's
/// room and checkpoint, and `netplay_lobby_poll` starts it as any start.
///
/// # Safety
///
/// `setup_json` is null or a valid NUL-terminated UTF-8 string (the lobby setup), read during the call only.
/// Thread-safe.
#[no_mangle]
pub unsafe extern "C" fn pbnet_lobby_rejoin(setup_json: *const c_char) -> i32 {
    // SAFETY: the caller's contract on `setup_json`.
    let Some(setup) = (unsafe { arg(setup_json) }).and_then(crate::lobby_c::parse_setup) else { return 0 };
    let Some(r) = user_dir().and_then(|d| read_record(&d)) else { return 0 };
    crate::lobby_c::rejoin_global(setup, r) as i32
}

/// The recovery's [`Status`] as JSON, snprintf-style (`{"running":false,...}` with no started match).
///
/// # Safety
///
/// `buf` is null (only the length is returned) or points to `cap` writable bytes, during the call only. Thread-safe.
#[no_mangle]
pub unsafe extern "C" fn pbnet_recover_json(buf: *mut c_char, cap: i32) -> i32 {
    let st = crate::lobby_c::with_global(|l| l.status()).unwrap_or_default();
    let json = serde_json::to_string(&st).unwrap_or_default();
    // SAFETY: the caller's contract on `buf` and `cap`.
    unsafe { put(&json, buf, cap) }
}

/// The started match's epoch: 0 from the start, one more per reload (0 with no lobby). When it moves while the lobby
/// is started, the game starts the room again (from its checkpoint) and calls `pbnet_recover_go`. Thread-safe.
#[no_mangle]
pub extern "C" fn pbnet_recover_epoch() -> u32 {
    crate::lobby_c::with_global(|l| l.epoch()).unwrap_or(0)
}

/// The game took epoch `epoch`'s room (the match start or reload is set up): its lockstep session starts. 1 if it
/// started or nothing was held, 0 if `epoch` is not the current one. Thread-safe.
#[no_mangle]
pub extern "C" fn pbnet_recover_go(epoch: u32) -> i32 {
    crate::lobby_c::with_global(|l| l.go(epoch)).unwrap_or(true) as i32
}

/// Host: the computer plays the seats of the peer at line-up index `index` (a waiting one) from the last
/// checkpoint on; every peer reloads it. 1 if done. Thread-safe.
#[no_mangle]
pub extern "C" fn pbnet_recover_cpu(index: i32) -> i32 {
    if !(1..SEATS as i32).contains(&index) {
        return 0;
    }
    crate::lobby_c::with_global(|l| l.stand_in(index as usize)).unwrap_or(false) as i32
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::lobby::{Host, Ident, Msg, Player, Seat};
    use crate::lockstep::fake::{End, Net};
    use crate::lockstep::{delay_for, Config, Pad, Session, Step, IDLE, NO_PEER};

    // ------------------------------------------------ the wire: a "game" whose state folds every frame's pads

    fn input(p: u8, n: u32) -> Pad {
        let v = n.wrapping_mul(2654435761).wrapping_add(p as u32 * 977);
        Pad { button: (v >> 7) as u16, stick_x: (v >> 3) as i8, ..IDLE }
    }

    fn fold(state: u64, pads: &[Pad; 4]) -> u64 {
        pads.iter().fold(state, |s, q| (s ^ q.button as u64 ^ (q.stick_x as u8 as u64) << 16).wrapping_mul(0x100000001b3))
    }

    /// One peer: its session (None: its app is gone), the game state and every frame's pads it ran.
    struct Peer {
        s: Option<Session<Epoch<End>>>,
        state: u64,
        log: Vec<[Pad; 4]>,
        reads: u32,
    }

    struct Match {
        net: Net,
        seats: [u8; 4],
        delay: u32,
        epoch: u32,
        peers: Vec<Peer>,
        now: u64,
    }

    impl Match {
        fn new(seats: [u8; 4], lag_ms: u64, loss: f64, seed: u64) -> Match {
            let n = seats.iter().filter(|&&p| p != NO_PEER).max().unwrap() + 1;
            let net = Net::new(lag_ms, loss, seed);
            let delay = delay_for(2 * lag_ms as u32);
            let mut m = Match { net, seats, delay, epoch: 0, peers: vec![], now: 0 };
            m.peers = (0..n).map(|me| Peer { s: Some(m.session(me)), state: 1, log: vec![], reads: 0 }).collect();
            m
        }

        fn session(&self, me: u8) -> Session<Epoch<End>> {
            let cfg = Config { me, peers: self.peers_n(), seats: self.seats, delay: self.delay };
            Session::new(cfg, Epoch { inner: self.net.end(me), epoch: self.epoch })
        }

        fn peers_n(&self) -> u8 {
            self.seats.iter().filter(|&&p| p != NO_PEER).max().unwrap() + 1
        }

        /// One real frame (60 Hz) on every peer whose app runs.
        fn frame(&mut self) {
            self.now += 1000 / 60;
            self.net.set_now(self.now);
            for (i, p) in self.peers.iter_mut().enumerate() {
                let Some(s) = p.s.as_mut() else { continue };
                let local: Vec<Pad> = (0..s.local_seats()).map(|_| input(i as u8, p.reads)).collect();
                let before = s.frame();
                if let Step::Ready(pads) = s.step(self.now, &local) {
                    p.state = fold(p.state, &pads);
                    p.log.push(pads);
                }
                // a new read per frame run (the test's input pattern only needs to be the same everywhere)
                if s.frame() != before {
                    p.reads += 1;
                }
            }
        }

        fn frames(&mut self, n: u32) {
            for _ in 0..n {
                self.frame();
            }
        }

        fn ticks(&self) -> Vec<u32> {
            self.peers.iter().map(|p| p.s.as_ref().map_or(0, |s| s.frame())).collect()
        }

        /// Every peer ran the same pads and has the same state up to the shortest log.
        fn assert_in_step(&self) {
            let shortest = self.peers.iter().map(|p| p.log.len()).min().unwrap();
            for (i, p) in self.peers.iter().enumerate() {
                assert_eq!(p.log[..shortest], self.peers[0].log[..shortest], "peer {i} ran other pads");
            }
        }
    }

    const THREE: [u8; 4] = [0, 1, 2, NO_PEER];

    /// Same app, link down for 10 s: everyone holds, sees the others silent after 3 s, and when the link is back the
    /// lockstep resumes on the same frame (the unacknowledged input log is resent), never apart.
    #[test]
    fn reconnect_resumes_on_the_same_frame() {
        let mut m = Match::new(THREE, 50, 0.05, 21);
        m.frames(300);
        let at = m.ticks();
        assert!(at.iter().all(|&t| t > 200), "{at:?}");
        m.net.cut(2, true);
        m.frames(60);
        let held = m.ticks();
        m.frames(600);
        // held: nobody ran a frame without peer 2's pads (at most the frames already in flight)
        assert_eq!(m.ticks(), held, "ran on during the drop");
        let now = m.now;
        let s0 = m.peers[0].s.as_ref().unwrap();
        let s2 = m.peers[2].s.as_ref().unwrap();
        assert_eq!(s0.silent(now, SILENCE_MS), 0b100, "peer 0 waits for 2");
        assert_eq!(s0.waiting_on() & 0b100, 0b100);
        assert_eq!(s2.silent(now, SILENCE_MS), 0b011, "peer 2 hears nobody");
        assert!(m.peers[1].s.as_ref().unwrap().quiet_ms(0, now).unwrap() < 100, "0 and 1 keep each other alive");
        let logs: Vec<usize> = m.peers.iter().map(|p| p.log.len()).collect();
        m.net.cut(2, false);
        m.frames(600);
        m.assert_in_step();
        let after = m.ticks();
        assert!(after.iter().all(|&t| t > held[0] + 500), "{held:?} -> {after:?}");
        // no frame was skipped or run twice: the logs go on from where they held
        for (i, p) in m.peers.iter().enumerate() {
            assert!(p.log.len() > logs[i]);
            assert_eq!(p.log.len() as u32, p.s.as_ref().unwrap().frame());
        }
        assert_eq!(m.peers[0].s.as_ref().unwrap().silent(m.now, SILENCE_MS), 0);
        let shortest = m.peers.iter().map(|p| p.log.len()).min().unwrap();
        // replay the common prefix: same state everywhere
        let replay = m.peers[0].log[..shortest].iter().fold(1, fold);
        for p in &m.peers {
            assert_eq!(p.log[..shortest].iter().fold(1, fold), replay);
        }
    }

    fn ident() -> Ident {
        Ident { build: "b".into(), disc: "GMPE01-01".into() }
    }

    fn player(n: &str) -> Player {
        Player { name: n.into(), key: format!("key-{n}") }
    }

    /// A started three-peer room: host, guests "g1" and "g2" (one seat each), a CPU in 4P.
    fn started() -> (Host, Book, Vec<(String, String)>) {
        let mut h = Host::new("m1".into(), ident(), &[player("h")]);
        for g in ["g1", "g2"] {
            h.on(g, Msg::Hello { ident: ident(), players: vec![player(g)] });
        }
        let ver = h.room().ver;
        for g in ["g1", "g2"] {
            h.on(g, Msg::Ready { ver, ready: true });
        }
        h.start_with(77, 2).unwrap();
        let mut b = Book::default();
        let tokens = b.issue(h.room());
        (h, b, tokens)
    }

    /// State lost on one peer: it rejoins with its token, the host reloads; every peer restarts from the
    /// checkpoint on a new epoch (while old-epoch datagrams are still in flight) and they run alike from there.
    #[test]
    fn a_lost_peer_rejoins_and_everyone_reloads_the_checkpoint() {
        let (mut h, mut book, tokens) = started();
        let lineup0 = h.room().peers();
        assert_eq!(lineup0, ["host", "g1", "g2"]);
        let mut m = Match::new(THREE, 50, 0.02, 5);
        // the checkpoint: every peer's state at a frame they all ran
        m.frames(400);
        let cp_frame = 300usize;
        let cp = m.peers[0].log[..cp_frame].iter().fold(1, fold);
        for p in &m.peers {
            assert_eq!(p.log[..cp_frame].iter().fold(1, fold), cp);
        }
        m.frames(50);
        // g2's app restarts: its session and state are gone, it dials from a new endpoint
        m.peers[2] = Peer { s: None, state: 0, log: vec![], reads: 0 };
        m.frames(300);
        let held = m.ticks();
        assert!(m.peers[0].s.as_ref().unwrap().silent(m.now, SILENCE_MS) & 0b100 != 0);

        let t2 = tokens.iter().find(|(p, _)| p == "g2").unwrap().1.clone();
        let mut lineup = lineup0.clone();
        let out = book.rejoin(h.room_mut(), &mut lineup, &t2, "g2-new", false, 0);
        assert_eq!(out, Outcome::Reload);
        assert_eq!(h.room().seats[2].as_ref().unwrap().peer, "g2-new");
        book.epoch += 1;
        let cpb = Checkpoint { blob: format!("{cp:016x}"), turn: 3, max_turn: 20, time: 1 };
        let room = reload_room(h.room(), Some(cpb.clone()));
        assert_eq!(room.checkpoint, Some(cpb));
        assert_eq!(room.peers(), ["host", "g1", "g2-new"]);

        // the old epoch's datagrams are still in flight to everyone
        m.epoch = book.epoch;
        for i in 0..3u8 {
            let state = u64::from_str_radix(&room.checkpoint.as_ref().unwrap().blob, 16).unwrap();
            let s = m.session(i);
            m.peers[i as usize] = Peer { s: Some(s), state, log: vec![], reads: 0 };
        }
        // one peer takes the reload late (the game loads the checkpoint first): the others wait for it
        let late = m.peers[1].s.take();
        m.frames(30);
        m.peers[1].s = late;
        m.frames(900);
        m.assert_in_step();
        assert!(m.ticks().iter().all(|&t| t > 700), "{:?} (held at {held:?})", m.ticks());
        let s: Vec<u64> = m.peers.iter().map(|p| p.state).collect();
        let shortest = m.peers.iter().map(|p| p.log.len()).min().unwrap();
        let replay = m.peers[0].log[..shortest].iter().fold(cp, fold);
        for p in &m.peers {
            assert_eq!(p.log[..shortest].iter().fold(cp, fold), replay, "{s:?}");
        }
    }

    #[test]
    fn a_stale_epoch_datagram_is_dropped() {
        assert_eq!(untag(3, &tag(3, b"abc")), Some(&b"abc"[..]));
        assert_eq!(untag(4, &tag(3, b"abc")), None);
        assert_eq!(untag(0, &[]), None);
        assert_eq!(untag(256, &tag(0, b"x")), Some(&b"x"[..]), "the low byte");
    }

    #[test]
    fn an_intact_rejoin_resumes_and_a_new_endpoint_is_routed() {
        let (mut h, book, tokens) = started();
        let mut lineup = h.room().peers();
        let t1 = &tokens.iter().find(|(p, _)| p == "g1").unwrap().1;
        assert_eq!(book.rejoin(h.room_mut(), &mut lineup, t1, "g1", true, 0), Outcome::Resume { index: 1 });
        assert_eq!(lineup, ["host", "g1", "g2"]);
        assert_eq!(book.rejoin(h.room_mut(), &mut lineup, t1, "g1b", true, 0), Outcome::Resume { index: 1 });
        assert_eq!(lineup, ["host", "g1b", "g2"]);
        assert_eq!(h.room().seats[1].as_ref().unwrap().peer, "g1b");
        assert_eq!(h.room().peers(), lineup, "the line-up order is kept");
        // it missed a reload while away: only it reloads
        assert_eq!(book.rejoin(h.room_mut(), &mut lineup, t1, "g1b", true, 1), Outcome::Join);
        assert_eq!(book.rejoin(h.room_mut(), &mut lineup, "nope", "x", true, 0), Outcome::Refused("you are not in this match".into()));
        assert!(matches!(book.rejoin(h.room_mut(), &mut lineup, t1, HOST, true, 0), Outcome::Refused(_)));
    }

    #[test]
    fn a_stand_in_keeps_the_seat_for_its_player() {
        let (mut h, book, tokens) = started();
        let mut lineup = h.room().peers();
        assert!(stand_in(h.room_mut(), "g2"));
        let s: &Seat = h.room().seats[2].as_ref().unwrap();
        assert!(s.cpu && s.peer == CPU && s.name == "g2 (CPU)" && s.key == "key-g2");
        assert!(!stand_in(h.room_mut(), "g2"), "nothing left");
        assert!(!stand_in(h.room_mut(), HOST));
        assert_eq!(h.room().peers(), ["host", "g1"], "the reload's line-up has no g2");
        // its app still ran: it takes the seat back, with a reload for all
        let t2 = &tokens.iter().find(|(p, _)| p == "g2").unwrap().1;
        assert_eq!(book.rejoin(h.room_mut(), &mut lineup, t2, "g2", true, 0), Outcome::Reload);
        let s = h.room().seats[2].as_ref().unwrap();
        assert!(!s.cpu && s.peer == "g2" && s.name == "g2");
        assert_eq!(h.room().peers(), ["host", "g1", "g2"]);
    }

    #[test]
    fn tokens_are_issued_to_guests_only_and_differ() {
        let (h, _, tokens) = started();
        assert_eq!(tokens.iter().map(|(p, _)| p.as_str()).collect::<Vec<_>>(), ["g1", "g2"]);
        assert_ne!(tokens[0].1, tokens[1].1);
        assert_eq!(tokens[0].1.len(), 32);
        assert!(h.room().seats[3].as_ref().unwrap().cpu);
    }

    #[test]
    fn waiting_names_the_silent_peers() {
        let (h, _, _) = started();
        let lineup = h.room().peers();
        let w = waiting(h.room(), &lineup, HOST, |p| Some([0, 100, 4000][p as usize]));
        assert_eq!(w, vec![Waiting { index: 2, name: "g2".into(), seats: vec![2], quiet_ms: 4000 }]);
        assert!(waiting(h.room(), &lineup, "g1", |_| None).is_empty());
    }

    #[test]
    fn a_record_rejoins_while_fresh() {
        let r = Rejoin { code: "AB3Q".into(), id: "m1".into(), token: "t".into(), at: 1000 };
        let t = serde_json::to_string(&r).unwrap();
        assert_eq!(decode(&t, 1000), Some(r.clone()));
        assert_eq!(decode(&t, 1000 + FRESH - 1), Some(r));
        assert_eq!(decode(&t, 1000 + FRESH), None, "too old");
        assert_eq!(decode(&t, 900), None, "from the future");
        assert_eq!(decode("", 1000), None);
    }

    #[test]
    fn a_record_is_kept_and_forgotten() {
        let dir = std::env::temp_dir().join(format!("pbnet-rejoin-{}", std::process::id()));
        std::fs::create_dir_all(&dir).unwrap();
        let r = Rejoin { code: "c".into(), id: "m".into(), token: "t".into(), at: 0 };
        save_record(&dir, &r);
        let got = read_record(&dir).unwrap();
        assert_eq!((got.code, got.id, got.token), (r.code.clone(), r.id.clone(), r.token.clone()));
        assert!(got.at > 0);
        let mut last = 0;
        keep(&dir, &mut last);
        assert!(last > 0 && read_record(&dir).is_some());
        forget(&dir);
        assert_eq!(read_record(&dir), None);
        let mut last = 0;
        keep(&dir, &mut last);
        assert_eq!(read_record(&dir), None, "forgotten stays forgotten");
        let _ = std::fs::remove_dir_all(&dir);
    }

    #[test]
    fn reload_room_takes_the_last_checkpoint_of_this_match() {
        let (h, _, _) = started();
        assert_eq!(reload_room(h.room(), None).checkpoint, None, "no checkpoint yet: the match restarts");
        let c = Checkpoint { blob: "00".into(), turn: 2, max_turn: 20, time: 5 };
        let r = reload_room(h.room(), Some(c.clone()));
        assert_eq!(r.checkpoint, Some(c.clone()));
        assert_eq!(reload_room(&r, None).checkpoint, Some(c), "a continue's own checkpoint stays");
    }
}
