//! Online saves (N9): at every board checkpoint (after each minigame, where `resultDll` would write the memory card)
//! every peer writes `<user dir>/online/<match id>.json`, so any of them can host the continue.
//!
//! A save is the `SLSaveBoard` blob (GWSystem + GWPlayer[4], big-endian as on the card, so a save written on one
//! platform loads on another), the started [`Room`] (its seats hold each player's key: the seat → player map), the
//! seat → key map once more for readers that don't want the room, the seeds, the turn and the time. Continuing,
//! the host's lobby is [`Host::resume`](crate::lobby::Host::resume) of the save's room with the blob in
//! [`Room::checkpoint`]; every peer then starts the board from that blob (`netplay_match_continue` in C).
//!
//! Files are written whole to a temporary name and renamed, so a crash mid-write leaves the last good save.

use std::ffi::{c_char, CStr};
use std::fs;
use std::io;
use std::path::{Path, PathBuf};
use std::time::SystemTime;

use serde::{Deserialize, Serialize};

use crate::lobby::{Checkpoint, Room, SEATS};

/// The format of the save files ([`Save::format`]); a reader refuses others.
pub const FORMAT: u32 = 1;
/// The directory under the user dir that holds the saves.
pub const DIR: &str = "online";
/// The largest blob a save holds (the game's is 0x19C bytes; the card's whole save is 16 KiB).
pub const BLOB_MAX: usize = 16384;

/// The seeds when the checkpoint was written: the match's (the room's) and the session's, and the game's three
/// random generators' states. A continue starts a new session from its own room's seed; these are a record (and
/// for desync reports).
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq, Serialize, Deserialize)]
pub struct Seeds {
    #[serde(rename = "match")]
    pub match_seed: u64,
    pub session: u32,
    pub frand: u32,
    pub rand8: u32,
    pub board: u32,
}

/// One online save, as the file holds it.
#[derive(Clone, Debug, PartialEq, Eq, Serialize, Deserialize)]
pub struct Save {
    pub format: u32,
    /// The match id ([`Room::id`]; the file's name).
    pub id: String,
    /// When it was written (Unix seconds).
    pub time: u64,
    /// The lockstep frame of the checkpoint.
    pub frame: u32,
    /// GWSystem.board, .turn and .max_turn at the checkpoint (shown in the list without decoding the blob).
    pub board: u8,
    pub turn: u8,
    pub max_turn: u8,
    /// Each seat's player key ("" for a seat the computer played): who gets which seat back.
    pub keys: [String; SEATS],
    pub seeds: Seeds,
    /// The started room the checkpoint was played in (without a checkpoint of its own).
    pub room: Room,
    /// The `SLSaveBoard` blob, hex.
    pub blob: String,
}

/// What the continue menu lists.
#[derive(Clone, Debug, PartialEq, Eq, Serialize)]
pub struct Summary {
    pub id: String,
    pub path: String,
    pub time: u64,
    pub board: u8,
    pub turn: u8,
    pub max_turn: u8,
    pub seats: Vec<SeatSummary>,
}

#[derive(Clone, Debug, PartialEq, Eq, Serialize)]
pub struct SeatSummary {
    pub name: String,
    pub character: u8,
    pub cpu: bool,
}

pub fn hex(b: &[u8]) -> String {
    b.iter().map(|b| format!("{b:02x}")).collect()
}

pub fn unhex(s: &str) -> Option<Vec<u8>> {
    if !s.len().is_multiple_of(2) {
        return None;
    }
    (0..s.len()).step_by(2).map(|i| s.get(i..i + 2).and_then(|h| u8::from_str_radix(h, 16).ok())).collect()
}

/// The file name for match `id`: its letters, digits, `-` and `_`, anything else `_` (an id comes from the host).
pub fn file_name(id: &str) -> String {
    let s: String = id.chars().take(96).map(|c| if c.is_ascii_alphanumeric() || c == '-' || c == '_' { c } else { '_' }).collect();
    format!("{}.json", if s.is_empty() { "_" } else { &s })
}

fn now() -> u64 {
    SystemTime::now().duration_since(SystemTime::UNIX_EPOCH).unwrap_or_default().as_secs()
}

impl Save {
    /// A save of `room` (started) at a checkpoint: `blob` is SLSaveBoard's output.
    pub fn new(room: &Room, blob: &[u8], frame: u32, seeds: Seeds) -> Result<Save, String> {
        if !room.started || room.seats.iter().any(Option::is_none) {
            return Err("the room is not a started match".into());
        }
        if blob.is_empty() || blob.len() > BLOB_MAX {
            return Err(format!("a board blob of {} bytes", blob.len()));
        }
        let keys = std::array::from_fn(|i| room.seats[i].as_ref().map(|s| if s.cpu { String::new() } else { s.key.clone() }).unwrap_or_default());
        let mut room = room.clone();
        room.checkpoint = None;
        room.ready.clear();
        // GWSystem's bytes 4 and 5 (include/game/gamework_data.h); single bytes, so the same in either byte order
        let (turn, max_turn) = (blob.get(4).copied().unwrap_or(0), blob.get(5).copied().unwrap_or(0));
        Ok(Save {
            format: FORMAT,
            id: room.id.clone(),
            time: now(),
            frame,
            board: room.board,
            turn,
            max_turn,
            keys,
            seeds,
            room,
            blob: hex(blob),
        })
    }

    /// Writes `<dir>/online/<id>.json` (a temporary file renamed over it); the path written.
    pub fn write(&self, user_dir: &Path) -> io::Result<PathBuf> {
        let dir = user_dir.join(DIR);
        fs::create_dir_all(&dir)?;
        let path = dir.join(file_name(&self.id));
        let tmp = path.with_extension("json.tmp");
        let json = serde_json::to_vec_pretty(self).map_err(io::Error::other)?;
        fs::write(&tmp, json)?;
        fs::rename(&tmp, &path)?;
        Ok(path)
    }

    /// Reads and checks a save file.
    pub fn read(path: &Path) -> Result<Save, String> {
        let bytes = fs::read(path).map_err(|e| format!("{}: {e}", path.display()))?;
        let s: Save = serde_json::from_slice(&bytes).map_err(|e| format!("{}: {e}", path.display()))?;
        s.check()?;
        Ok(s)
    }

    fn check(&self) -> Result<(), String> {
        if self.format != FORMAT {
            return Err(format!("save format {} (this build reads {FORMAT})", self.format));
        }
        if self.id != self.room.id {
            return Err("the save's id is not its room's".into());
        }
        if !self.room.started || self.room.seats.iter().any(Option::is_none) {
            return Err("the save's room is not a started match".into());
        }
        match unhex(&self.blob) {
            Some(b) if !b.is_empty() && b.len() <= BLOB_MAX => Ok(()),
            _ => Err("the save's board blob is damaged".into()),
        }
    }

    /// The checkpoint a continue of this save starts from.
    pub fn checkpoint(&self) -> Checkpoint {
        Checkpoint { blob: self.blob.clone(), turn: self.turn, max_turn: self.max_turn, time: self.time }
    }

    pub fn summary(&self, path: &Path) -> Summary {
        Summary {
            id: self.id.clone(),
            path: path.to_string_lossy().into_owned(),
            time: self.time,
            board: self.board,
            turn: self.turn,
            max_turn: self.max_turn,
            seats: self
                .room
                .seats
                .iter()
                .flatten()
                .map(|s| SeatSummary { name: s.name.clone(), character: s.character, cpu: s.cpu })
                .collect(),
        }
    }
}

/// Every readable save under `<user_dir>/online`, newest first (unreadable files are skipped, and logged).
pub fn list(user_dir: &Path) -> Vec<Summary> {
    let Ok(rd) = fs::read_dir(user_dir.join(DIR)) else { return vec![] };
    let mut out: Vec<Summary> = rd
        .flatten()
        .map(|e| e.path())
        .filter(|p| p.extension().is_some_and(|x| x == "json"))
        .filter_map(|p| Save::read(&p).map_err(|e| eprintln!("pbnet: save: {e}")).ok().map(|s| s.summary(&p)))
        .collect();
    out.sort_by(|a, b| b.time.cmp(&a.time).then_with(|| a.id.cmp(&b.id)));
    out
}

// ---------------------------------------------------------------- C ABI

/// A C string argument as a path (None: null or not UTF-8).
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

/// Writes the online save of a checkpoint (see `pbnet.h`): the room is the lobby's started one, else `room_json`.
/// 1 if written (its path snprintf'd into `path`/`cap`), 0 if not (logged).
///
/// # Safety
///
/// `user_dir` and `room_json` are null or valid NUL-terminated strings; `blob` points to `len` readable bytes;
/// `seeds` is null or points to 4 readable u32; `path` is null or points to `cap` writable bytes. All are used
/// during the call only. Thread-safe.
#[no_mangle]
pub unsafe extern "C" fn pbnet_save_write(
    user_dir: *const c_char,
    room_json: *const c_char,
    blob: *const u8,
    len: i32,
    frame: u32,
    seeds: *const u32,
    path: *mut c_char,
    cap: i32,
) -> i32 {
    // SAFETY: the caller's contract on the strings.
    let (dir, fallback) = unsafe { (arg(user_dir), arg(room_json)) };
    let Some(dir) = dir else { return 0 };
    if blob.is_null() || len <= 0 || len as usize > BLOB_MAX {
        return 0;
    }
    // SAFETY: the caller guarantees `len` readable bytes at `blob`.
    let blob = unsafe { std::slice::from_raw_parts(blob, len as usize) };
    let s = if seeds.is_null() {
        [0; 4]
    } else {
        // SAFETY: the caller guarantees 4 readable u32 at `seeds`.
        let s = unsafe { std::slice::from_raw_parts(seeds, 4) };
        [s[0], s[1], s[2], s[3]]
    };
    let room = crate::lobby_c::started_room().or_else(|| {
        fallback.and_then(|j| serde_json::from_str::<Room>(j).map_err(|e| eprintln!("pbnet: save: room: {e}")).ok())
    });
    let Some(room) = room else {
        eprintln!("pbnet: save: no started room");
        return 0;
    };
    let seeds = Seeds { match_seed: room.seed, session: s[0], frand: s[1], rand8: s[2], board: s[3] };
    let r = Save::new(&room, blob, frame, seeds).and_then(|sv| {
        // N10: the host's reloads send the last checkpoint
        crate::recover::checkpoint_written(&sv);
        sv.write(Path::new(dir)).map_err(|e| e.to_string())
    });
    match r {
        Ok(p) => {
            // SAFETY: the caller's contract on `path` and `cap`.
            unsafe { put(&p.to_string_lossy(), path, cap) };
            1
        }
        Err(e) => {
            eprintln!("pbnet: save: {e}");
            0
        }
    }
}

/// The saves under `<user_dir>/online` as a JSON array, newest first, snprintf-style (returns the full length):
/// `[{"id", "path", "time", "board", "turn", "max_turn", "seats": [{"name", "character", "cpu"}]}]`.
///
/// # Safety
///
/// `user_dir` is null (an empty list) or a valid NUL-terminated string; `buf` is null or points to `cap` writable
/// bytes. Used during the call only. Thread-safe.
#[no_mangle]
pub unsafe extern "C" fn pbnet_save_list(user_dir: *const c_char, buf: *mut c_char, cap: i32) -> i32 {
    // SAFETY: the caller's contract on `user_dir`.
    let saves = unsafe { arg(user_dir) }.map(|d| list(Path::new(d))).unwrap_or_default();
    let json = serde_json::to_string(&saves).unwrap_or_else(|_| "[]".into());
    // SAFETY: the caller's contract on `buf` and `cap`.
    unsafe { put(&json, buf, cap) }
}

/// The `SLSaveBoard` blob of the save file at `path`: copies at most `cap` bytes into `buf`, returns its length; 0 if
/// the file isn't a readable save (logged).
///
/// # Safety
///
/// `path` is null or a valid NUL-terminated string; `buf` is null (only the length is returned) or points to `cap`
/// writable bytes. Used during the call only. Thread-safe.
#[no_mangle]
pub unsafe extern "C" fn pbnet_save_blob(path: *const c_char, buf: *mut u8, cap: i32) -> i32 {
    // SAFETY: the caller's contract on `path`.
    let Some(p) = (unsafe { arg(path) }) else { return 0 };
    let blob = match Save::read(Path::new(p)) {
        Ok(s) => unhex(&s.blob).unwrap_or_default(),
        Err(e) => {
            eprintln!("pbnet: save: {e}");
            return 0;
        }
    };
    if !buf.is_null() && cap > 0 {
        let n = blob.len().min(cap as usize);
        // SAFETY: the caller guarantees `cap` writable bytes at `buf`; n <= cap.
        unsafe { std::ptr::copy_nonoverlapping(blob.as_ptr(), buf, n) };
    }
    blob.len().min(i32::MAX as usize) as i32
}

/// Deletes the save file at `path` (a path from `pbnet_save_list`). 1 if it was deleted.
///
/// # Safety
///
/// `path` is null or a valid NUL-terminated string, used during the call only. Thread-safe.
#[no_mangle]
pub unsafe extern "C" fn pbnet_save_delete(path: *const c_char) -> i32 {
    // SAFETY: the caller's contract on `path`.
    let Some(p) = (unsafe { arg(path) }) else { return 0 };
    let p = Path::new(p);
    // only save files: never anything else a bad path could name
    if !p.extension().is_some_and(|x| x == "json") || !p.parent().is_some_and(|d| d.ends_with(DIR)) {
        return 0;
    }
    fs::remove_file(p).is_ok() as i32
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::lobby::{Host, Ident, Player, CPU};

    fn tmp(name: &str) -> PathBuf {
        let d = std::env::temp_dir().join(format!("pbnet-save-{name}-{}", std::process::id()));
        let _ = fs::remove_dir_all(&d);
        d
    }

    pub(crate) fn started_room() -> Room {
        let ident = Ident { build: "b".into(), disc: "d".into() };
        let mut h = Host::new("m-1".into(), ident, &[Player { name: "h".into(), key: "kh".into() }]);
        h.start(77).unwrap();
        h.room().clone()
    }

    fn blob() -> Vec<u8> {
        let mut b = vec![0u8; 0x19C];
        b[4] = 7;
        b[5] = 20;
        b[0x50] = 0xAB;
        b
    }

    #[test]
    fn write_read_list_round_trip() {
        let d = tmp("rt");
        let room = started_room();
        let seeds = Seeds { match_seed: 77, session: 1, frand: 2, rand8: 3, board: 4 };
        let s = Save::new(&room, &blob(), 1234, seeds).unwrap();
        assert_eq!((s.turn, s.max_turn, s.board), (7, 20, 0));
        assert_eq!(s.keys, ["kh".to_string(), String::new(), String::new(), String::new()]);
        let p = s.write(&d).unwrap();
        assert_eq!(p, d.join("online").join("m-1.json"));
        let back = Save::read(&p).unwrap();
        assert_eq!(back, s);
        assert_eq!(unhex(&back.blob).unwrap(), blob());
        let l = list(&d);
        assert_eq!(l.len(), 1);
        assert_eq!((l[0].id.as_str(), l[0].turn, l[0].seats.len()), ("m-1", 7, 4));
        assert!(l[0].seats[1].cpu && l[0].seats[1].name == "CPU");
        // the next checkpoint overwrites the same file
        let mut b2 = blob();
        b2[4] = 8;
        Save::new(&room, &b2, 2000, seeds).unwrap().write(&d).unwrap();
        let l = list(&d);
        assert_eq!((l.len(), l[0].turn), (1, 8));
        assert!(!d.join("online").join("m-1.json.tmp").exists());
        fs::remove_dir_all(&d).unwrap();
    }

    #[test]
    fn damaged_and_foreign_files_are_skipped() {
        let d = tmp("bad");
        let room = started_room();
        let s = Save::new(&room, &blob(), 1, Seeds::default()).unwrap();
        s.write(&d).unwrap();
        let on = d.join("online");
        fs::write(on.join("junk.json"), "{not json").unwrap();
        fs::write(on.join("notes.txt"), "x").unwrap();
        let mut bad = s.clone();
        bad.id = "other".into();
        bad.room.id = "other".into();
        bad.blob = "zz".into();
        fs::write(on.join("other.json"), serde_json::to_vec(&bad).unwrap()).unwrap();
        let mut future = s.clone();
        future.id = "f".into();
        future.room.id = "f".into();
        future.format = FORMAT + 1;
        fs::write(on.join("f.json"), serde_json::to_vec(&future).unwrap()).unwrap();
        let l = list(&d);
        assert_eq!(l.iter().map(|s| s.id.as_str()).collect::<Vec<_>>(), ["m-1"]);
        assert!(Save::read(&on.join("f.json")).unwrap_err().contains("format"));
        fs::remove_dir_all(&d).unwrap();
    }

    #[test]
    fn unstarted_room_or_bad_blob_is_no_save() {
        let ident = Ident { build: "b".into(), disc: "d".into() };
        let h = Host::new("m".into(), ident, &[Player { name: "h".into(), key: "k".into() }]);
        assert!(Save::new(h.room(), &blob(), 0, Seeds::default()).is_err());
        assert!(Save::new(&started_room(), &[], 0, Seeds::default()).is_err());
    }

    /// The room C writes for a match with no lobby (`netplay_match_room_json`), as tools/netplay/match_test.sh hands
    /// it over in `MATCH_TEST_ROOM`: a Room pbnet reads and saves. Without the variable (plain `cargo test`): skipped.
    #[test]
    fn c_room_json_is_a_room() {
        let Ok(path) = std::env::var("MATCH_TEST_ROOM") else { return };
        let json = fs::read_to_string(&path).unwrap();
        let room: Room = serde_json::from_str(&json).unwrap_or_else(|e| panic!("{e}: {json}"));
        assert!(room.started && room.id == "local-1111111122222222" && room.board == 3);
        assert_eq!(room.seats[0].as_ref().unwrap().key, "local-1");
        assert!(room.seats[3].as_ref().unwrap().cpu && room.seats[3].as_ref().unwrap().key.is_empty());
        let s = Save::new(&room, &blob(), 1, Seeds::default()).unwrap();
        assert_eq!(s.keys, ["local-1".to_string(), "local-2".into(), String::new(), String::new()]);
    }

    #[test]
    fn file_names_are_safe() {
        assert_eq!(file_name("abc-12_x"), "abc-12_x.json");
        assert_eq!(file_name("../../etc/passwd"), "______etc_passwd.json");
        assert_eq!(file_name(""), "_.json");
        assert_eq!(unhex("0aFf"), Some(vec![0x0a, 0xff]));
        assert_eq!(unhex("0"), None);
        assert_eq!(unhex("zz"), None);
        assert_eq!(hex(&[1, 0xab]), "01ab");
    }

    #[test]
    fn c_abi_write_list_delete() {
        let d = tmp("c");
        let dir = std::ffi::CString::new(d.to_str().unwrap()).unwrap();
        let room = std::ffi::CString::new(serde_json::to_string(&started_room()).unwrap()).unwrap();
        let b = blob();
        let seeds = [1u32, 2, 3, 4];
        let mut path = [0 as c_char; 512];
        // no lobby runs in this test: the room comes from the JSON
        let ok = unsafe {
            pbnet_save_write(dir.as_ptr(), room.as_ptr(), b.as_ptr(), b.len() as i32, 99, seeds.as_ptr(), path.as_mut_ptr(), 512)
        };
        assert_eq!(ok, 1);
        let p = unsafe { CStr::from_ptr(path.as_ptr()) }.to_str().unwrap().to_string();
        let s = Save::read(Path::new(&p)).unwrap();
        assert_eq!((s.frame, s.seeds.session, s.seeds.board, s.seeds.match_seed), (99, 1, 4, 77));
        assert_eq!(s.room.seats[3].as_ref().unwrap().peer, CPU);

        let n = unsafe { pbnet_save_list(dir.as_ptr(), std::ptr::null_mut(), 0) };
        let mut buf = vec![0 as c_char; n as usize + 1];
        assert_eq!(unsafe { pbnet_save_list(dir.as_ptr(), buf.as_mut_ptr(), buf.len() as i32) }, n);
        let v: serde_json::Value = serde_json::from_str(unsafe { CStr::from_ptr(buf.as_ptr()) }.to_str().unwrap()).unwrap();
        assert_eq!(v[0]["id"], "m-1");
        assert_eq!(v[0]["path"], p.as_str());

        let pc = std::ffi::CString::new(p.clone()).unwrap();
        let mut got = [0u8; 0x200];
        assert_eq!(unsafe { pbnet_save_blob(pc.as_ptr(), got.as_mut_ptr(), got.len() as i32) }, b.len() as i32);
        assert_eq!(&got[..b.len()], &b[..]);
        assert_eq!(unsafe { pbnet_save_blob(pc.as_ptr(), std::ptr::null_mut(), 0) }, b.len() as i32);
        let none = std::ffi::CString::new(d.join("none.json").to_str().unwrap()).unwrap();
        assert_eq!(unsafe { pbnet_save_blob(none.as_ptr(), got.as_mut_ptr(), 4) }, 0);

        // refused: no room, empty blob, a path outside the saves
        assert_eq!(unsafe { pbnet_save_write(dir.as_ptr(), std::ptr::null(), b.as_ptr(), b.len() as i32, 0, std::ptr::null(), std::ptr::null_mut(), 0) }, 0);
        assert_eq!(unsafe { pbnet_save_write(dir.as_ptr(), room.as_ptr(), b.as_ptr(), 0, 0, std::ptr::null(), std::ptr::null_mut(), 0) }, 0);
        let outside = std::ffi::CString::new(d.join("x.json").to_str().unwrap()).unwrap();
        fs::write(d.join("x.json"), "{}").unwrap();
        assert_eq!(unsafe { pbnet_save_delete(outside.as_ptr()) }, 0);
        assert_eq!(unsafe { pbnet_save_delete(pc.as_ptr()) }, 1);
        assert!(list(&d).is_empty());
        fs::remove_dir_all(&d).unwrap();
    }
}
