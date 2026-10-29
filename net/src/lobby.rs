//! The lobby protocol (ported from opentennis `hst-base/src/net.rs` `Msg`/`Room`, without Bevy or mods).
//!
//! Host-authoritative: the host keeps the one [`Room`] and sends it whole to every guest on every change. On the
//! lobby stream (`transport::Peer<Msg>`) the joiner speaks first:
//!
//! - guest → host [`Msg::Hello`]: its build id, disc revision and local players (name, stable player key). The host
//!   answers [`Msg::Refused`] (other build, other disc, full, already started) or seats the players and sends
//!   [`Msg::Room`] to everyone. A key already in the room is another player's only while that player is away (the
//!   computer plays its seat); a key whose seat is held by someone here (the host's own players too: two copies of
//!   the game sharing a profile, N89) is a different player, seated with a fresh key ([`Seat::key`] in the room).
//! - guest → host [`Msg::Picks`]: characters and teams for its seats, in seat order. A pick the host can't take
//!   (a character another seat has) keeps the seat's old one; the next `Room` says so.
//! - guest → host [`Msg::Ready`]: ready (or not) for line-up [`Room::ver`]. Every change of seats or settings bumps
//!   `ver` and clears the ready flags, so a guest is only ever ready for what it last saw.
//! - host → all [`Msg::Start`]: the final `Room` when the host starts, ready guests or not (N148: the host's menu
//!   asks first when some aren't). Every guest starts on it alike, ready or not, with the seats and picks the host
//!   holds. Open seats are the computer's from then on ([`CPU`]), the match seed is set.
//! - either way [`Msg::Leave`]: a guest leaving frees its seats; the host leaving ends the lobby.
//!
//! **Continue** (N9): a host holding an online save ([`crate::save`]) opens the lobby with [`Host::resume`]. The
//! room is the saved one with the board checkpoint in [`Room::checkpoint`] (so guests need no save of their own),
//! every seat played by the computer until a player takes it: a player whose key holds a seat gets that seat back,
//! anyone else takes a free one (seats the computer played in the save first, so absent players' seats stay theirs
//! as long as possible). Characters, teams and rules are the save's and can't be changed.
//!
//! Everything here is sans-IO: [`Host`] and [`Guest`] take messages and return what to send, so the protocol is
//! tested without a network and the transport task (and the C ABI over it) only moves bytes.

use std::collections::BTreeSet;

use serde::{Deserialize, Serialize};

/// Seats in a match (1P..4P).
pub const SEATS: usize = 4;
/// The host's peer name in a [`Room`] (guests are named by their endpoint IDs).
pub const HOST: &str = "host";
/// The peer name of a seat the computer plays.
pub const CPU: &str = "cpu";
/// Characters on the disc (Mario, Luigi, Peach, Yoshi, Wario, Donkey Kong, Daisy, Waluigi); no two seats share one.
pub const CHARACTERS: u8 = 8;
/// Boards a match can be played on (the six party boards, `GWSystem.board`).
pub const BOARDS: u8 = 6;
/// CPU difficulty levels (easy, normal, hard, very hard).
pub const DIFFICULTIES: u8 = 4;
/// Turn counts the game offers: 10 to 50 in steps of 5.
pub const TURNS_MIN: u8 = 10;
pub const TURNS_MAX: u8 = 50;
/// The largest handicap (stars a player starts with).
pub const HANDICAP_MAX: u8 = 9;

/// One seat as the host keeps it.
#[derive(Clone, Debug, PartialEq, Eq, Serialize, Deserialize)]
pub struct Seat {
    /// The peer whose player sits here ([`HOST`], a guest's endpoint ID, or [`CPU`]).
    pub peer: String,
    /// The player's name, shown in the lobby and the match.
    pub name: String,
    /// The player's stable key (kept on its device; empty for a CPU): online saves give the seat back by it.
    pub key: String,
    /// Character index (`GWPlayerCfg.character`), `0..CHARACTERS`.
    pub character: u8,
    /// The computer plays this seat.
    pub cpu: bool,
    /// CPU difficulty (`GWPlayerCfg.diff`), `0..DIFFICULTIES`; kept for a human seat in case the CPU stands in.
    pub difficulty: u8,
    /// Team (0 or 1, `GWPlayerCfg.group`), used when [`Room::teams`] is on.
    pub team: u8,
}

impl Seat {
    /// A person (on some peer) plays this seat.
    pub fn human(&self) -> bool {
        !self.cpu
    }
}

/// The lobby as the host keeps it and every guest sees it.
#[derive(Clone, Debug, PartialEq, Eq, Serialize, Deserialize)]
pub struct Room {
    /// The match's id, picked by the host; online saves are named by it.
    pub id: String,
    /// Seats 1P..4P, `None` open (filled by the computer at the start).
    pub seats: [Option<Seat>; SEATS],
    /// Board index, `0..BOARDS`.
    pub board: u8,
    /// Turn count, `TURNS_MIN..=TURNS_MAX` in steps of 5.
    pub turns: u8,
    /// Bonus stars at the end of the match.
    pub bonus: bool,
    /// Team battle (2 vs 2 by [`Seat::team`]).
    pub teams: bool,
    /// Each seat's handicap (stars it starts with).
    pub handicaps: [u8; SEATS],
    /// The match seed, set by the host at the start (0 before).
    pub seed: u64,
    /// The peers ready for line-up `ver` (the host always is, so it is never listed).
    pub ready: BTreeSet<String>,
    /// Counts changes to the seats and settings; a guest's [`Msg::Ready`] is for one of them.
    pub ver: u32,
    /// Set in the room sent with [`Msg::Start`].
    pub started: bool,
    /// The lockstep's input delay (frames), picked by the host at the start (0 before; [`Host::start_with`]).
    #[serde(default)]
    pub delay: u32,
    /// A continued match: the board checkpoint it resumes from (None for a new match).
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub checkpoint: Option<Checkpoint>,
}

/// The board checkpoint a continued match starts from (from an online save, [`crate::save::Save`]).
#[derive(Clone, Debug, PartialEq, Eq, Serialize, Deserialize)]
pub struct Checkpoint {
    /// The `SLSaveBoard` blob (GWSystem + GWPlayer[4], big-endian as on the memory card), hex.
    pub blob: String,
    /// The turn the match continues at, and its last.
    pub turn: u8,
    pub max_turn: u8,
    /// When the save was written (Unix seconds).
    pub time: u64,
}

impl Room {
    fn new(id: String) -> Room {
        Room {
            id,
            seats: Default::default(),
            board: 0,
            turns: 20,
            bonus: true,
            teams: false,
            handicaps: [0; SEATS],
            seed: 0,
            ready: BTreeSet::new(),
            ver: 0,
            started: false,
            delay: 0,
            checkpoint: None,
        }
    }

    /// A continued match: everything is the save's.
    pub fn resuming(&self) -> bool {
        self.checkpoint.is_some()
    }

    /// The seats `peer` holds, in seat order.
    pub fn seats_of(&self, peer: &str) -> Vec<usize> {
        (0..SEATS).filter(|&i| self.seats[i].as_ref().is_some_and(|s| s.peer == peer)).collect()
    }

    /// The peers with seats, host first, then the guests in the order of their first seat: the line-up the
    /// lockstep session numbers peers by.
    pub fn peers(&self) -> Vec<String> {
        let mut out = vec![HOST.to_string()];
        for s in self.seats.iter().flatten() {
            if s.peer != CPU && !out.contains(&s.peer) {
                out.push(s.peer.clone());
            }
        }
        out
    }

    /// Seats a new player can take: open ones, and when resuming the ones the computer plays.
    fn open(&self) -> usize {
        (0..SEATS).filter(|&i| self.takeable(i)).count()
    }

    fn takeable(&self, i: usize) -> bool {
        match &self.seats[i] {
            None => true,
            Some(s) => self.resuming() && s.peer == CPU,
        }
    }

    /// The seat a new player takes: the first open one; when resuming, the first the computer played in the save
    /// (no key), then an absent player's.
    fn free_seat(&self) -> Option<usize> {
        let keyless = (0..SEATS).find(|&i| self.takeable(i) && self.seats[i].as_ref().is_none_or(|s| s.key.is_empty()));
        keyless.or_else(|| (0..SEATS).find(|&i| self.takeable(i)))
    }

    /// The lowest character no seat (other than `except`) has.
    fn free_character(&self, except: Option<usize>) -> u8 {
        (0..CHARACTERS).find(|&c| !self.character_taken(c, except)).unwrap_or(0)
    }

    fn character_taken(&self, c: u8, except: Option<usize>) -> bool {
        self.seats.iter().enumerate().any(|(i, s)| Some(i) != except && s.as_ref().is_some_and(|s| s.character == c))
    }

    /// Every guest with a seat is ready for the current line-up.
    pub fn all_ready(&self) -> bool {
        self.peers().iter().skip(1).all(|p| self.ready.contains(p))
    }
}

/// One local player of a peer, as its Hello names it.
#[derive(Clone, Debug, PartialEq, Eq, Serialize, Deserialize)]
pub struct Player {
    pub name: String,
    pub key: String,
}

/// A guest's choice for one of its seats.
#[derive(Clone, Copy, Debug, PartialEq, Eq, Serialize, Deserialize)]
pub struct Pick {
    pub character: u8,
    pub team: u8,
}

/// What a peer must share with the host to play: two builds or two disc revisions can't be trusted to run alike.
#[derive(Clone, Debug, PartialEq, Eq, Serialize, Deserialize)]
pub struct Ident {
    /// The build id (version and commit).
    pub build: String,
    /// The disc's game id and revision (e.g. `GMPE01-00`).
    pub disc: String,
}

impl Ident {
    /// A joining peer's (`self`) against the host's: `Ok` to play, else why not, in words for the player.
    pub fn check(&self, host: &Ident) -> Result<(), String> {
        if self.build != host.build {
            return Err(format!(
                "a different version of the game (host {}, you {}). Update so you both have the same one.",
                host.build, self.build
            ));
        }
        if self.disc != host.disc {
            return Err(format!(
                "a different disc (host {}, you {}). Both players need the same Mario Party 4 (USA) disc.",
                host.disc, self.disc
            ));
        }
        Ok(())
    }
}

#[derive(Clone, Debug, PartialEq, Serialize, Deserialize)]
pub enum Msg {
    Hello { ident: Ident, players: Vec<Player> },
    Room(Room),
    Refused(String),
    Picks(Vec<Pick>),
    Ready { ver: u32, ready: bool },
    Start(Room),
    Leave,
    /// N10 (`crate::recover`), host → one guest at the start: its rejoin token.
    Token(String),
    /// N10, guest → host on a new connection after the start: back in the match with `token`. `intact`: its
    /// lockstep session of `epoch` still runs (same app), else it lost its state and needs a reload.
    Rejoin { token: String, intact: bool, epoch: u32 },
    /// N10, host → the rejoined guest: the lockstep goes on where it is.
    Resumed,
    /// N10, host → all: every peer reloads `room`'s checkpoint (none: the match's start) and runs a new lockstep
    /// session, numbered `epoch`, on `room`'s line-up.
    Reload { room: Room, epoch: u32 },
    /// N44, host → all: going back to the last save point did not mend a desync (the same point desynced again), so
    /// the game holds `secs` seconds before the next try ([`crate::desync::Resync::delay`]).
    Held { secs: u32 },
}

/// Where the host sends a message.
#[derive(Clone, Debug, PartialEq)]
pub enum To {
    /// One peer (also one not seated: a refused joiner).
    Peer(String),
    /// Every seated guest.
    All,
}

/// The host's half: the room, and every guest's messages into it.
pub struct Host {
    room: Room,
    ident: Ident,
    /// Resuming: the seats as the save has them, every one the computer's (a leaving player's seat goes back to it).
    saved: Option<Box<[Option<Seat>; SEATS]>>,
}

impl Host {
    /// A new lobby `id`, its first seats for the host's local players (1 to 4).
    pub fn new(id: String, ident: Ident, players: &[Player]) -> Host {
        let mut h = Host { room: Room::new(id), ident, saved: None };
        h.seat(HOST, players);
        h
    }

    /// A lobby continuing the saved, started `saved` room from `checkpoint`: same id, characters, teams and rules;
    /// every seat the computer's (keeping its player's key) until a player takes it, then the host's local players
    /// seated by key. Err if `saved` isn't a started room with four seats.
    pub fn resume(saved: &Room, checkpoint: Checkpoint, ident: Ident, players: &[Player]) -> Result<Host, String> {
        if !saved.started || saved.seats.iter().any(Option::is_none) {
            return Err("the save's room is not a started match".into());
        }
        let seats = saved.seats.clone().map(|s| {
            s.map(|s| Seat { peer: CPU.into(), name: if s.cpu { s.name } else { format!("{} (CPU)", s.name) }, cpu: true, ..s })
        });
        let mut room = Room::new(saved.id.clone());
        room.seats = seats.clone();
        room.board = saved.board;
        room.turns = saved.turns;
        room.bonus = saved.bonus;
        room.teams = saved.teams;
        room.handicaps = saved.handicaps;
        room.checkpoint = Some(checkpoint);
        let mut h = Host { room, ident, saved: Some(Box::new(seats)) };
        h.seat(HOST, players);
        Ok(h)
    }

    pub fn room(&self) -> &Room {
        &self.room
    }

    /// The room, for the recovery after the start (N10, `crate::recover`): who plays which seat changes there.
    pub fn room_mut(&mut self) -> &mut Room {
        &mut self.room
    }

    /// The seat `key` takes back: one with that key the computer plays (its player is away). A seat whose player is
    /// still here (a connected guest, the host's own) isn't: a Hello with its key is another player (N89: two copies
    /// of the game sharing one profile).
    fn reclaimable(&self, key: &str) -> Option<usize> {
        (0..SEATS).find(|&i| self.room.seats[i].as_ref().is_some_and(|s| !key.is_empty() && s.key == key && s.peer == CPU))
    }

    /// `key`, or when a seat already has it, a fresh one made from it (`key-2`, `key-3`, ...) that no seat has. The
    /// player learns it from the room ([`Room::seats_of`]) and keeps it for later continues.
    fn unique_key(&self, key: &str) -> String {
        let taken = |k: &str| self.room.seats.iter().flatten().any(|s| s.key == k);
        if key.is_empty() || !taken(key) {
            return key.into();
        }
        (2..).map(|n| format!("{key}-{n}")).find(|k| !taken(k)).unwrap()
    }

    /// Seats `players` for `peer`: first the seats their keys hold that the computer plays (a player back), then
    /// the open ones in order.
    fn seat(&mut self, peer: &str, players: &[Player]) {
        for p in players.iter().take(SEATS) {
            if let Some(i) = self.reclaimable(&p.key) {
                let s = self.room.seats[i].as_mut().unwrap();
                self.room.ready.remove(&s.peer);
                s.peer = peer.into();
                s.name = p.name.clone();
                // resuming: the player takes it back from the computer
                s.cpu = false;
                continue;
            }
            let Some(i) = self.room.free_seat() else { return };
            let key = self.unique_key(&p.key);
            if let Some(s) = self.room.seats[i].as_mut() {
                // resuming: the seat is this player's from now on (the next save gives it back by this key)
                s.peer = peer.into();
                s.name = p.name.clone();
                s.key = key;
                s.cpu = false;
                continue;
            }
            let character = self.room.free_character(None);
            self.room.seats[i] = Some(Seat {
                peer: peer.into(),
                name: p.name.clone(),
                key,
                character,
                cpu: false,
                difficulty: 1,
                team: (i / 2) as u8,
            });
        }
    }

    /// A change to the line-up: a new `ver`, nobody ready, and the room to everyone.
    fn changed(&mut self) -> Vec<(To, Msg)> {
        self.room.ver += 1;
        self.room.ready.clear();
        vec![(To::All, Msg::Room(self.room.clone()))]
    }

    /// A message from `peer` (a guest's endpoint ID).
    pub fn on(&mut self, peer: &str, m: Msg) -> Vec<(To, Msg)> {
        let seated = !self.room.seats_of(peer).is_empty();
        match m {
            Msg::Hello { ident, players } => {
                let refuse = |why: String| vec![(To::Peer(peer.into()), Msg::Refused(why))];
                if let Err(why) = ident.check(&self.ident) {
                    return refuse(why);
                }
                if self.room.started {
                    return refuse("the match has started".into());
                }
                if seated || peer == HOST || peer == CPU {
                    return refuse("already in this lobby".into());
                }
                if players.is_empty() || players.len() > SEATS {
                    return refuse("no players".into());
                }
                let back = players.iter().filter(|p| self.reclaimable(&p.key).is_some()).count();
                if players.len() - back > self.room.open() {
                    return refuse("the lobby is full".into());
                }
                self.seat(peer, &players);
                self.changed()
            }
            Msg::Picks(picks) if seated && !self.room.started && !self.room.resuming() => {
                if self.pick(peer, &picks) {
                    self.changed()
                } else {
                    // nothing taken: still tell the guest what it has
                    vec![(To::Peer(peer.into()), Msg::Room(self.room.clone()))]
                }
            }
            Msg::Ready { ver, ready } if seated && !self.room.started => {
                if ver != self.room.ver {
                    return vec![(To::Peer(peer.into()), Msg::Room(self.room.clone()))];
                }
                let was = self.room.ready.contains(peer);
                if ready == was {
                    return vec![];
                }
                if ready {
                    self.room.ready.insert(peer.into());
                } else {
                    self.room.ready.remove(peer);
                }
                vec![(To::All, Msg::Room(self.room.clone()))]
            }
            Msg::Leave => self.gone(peer),
            // a guest never sends these, and nothing after the start goes through the lobby
            _ => vec![],
        }
    }

    /// Applies `picks` to `peer`'s seats in order; a character another seat has is skipped. Whether anything changed.
    fn pick(&mut self, peer: &str, picks: &[Pick]) -> bool {
        let mut any = false;
        for (i, p) in self.room.seats_of(peer).into_iter().zip(picks) {
            let free = p.character < CHARACTERS && !self.room.character_taken(p.character, Some(i));
            let s = self.room.seats[i].as_mut().unwrap();
            if free && s.character != p.character {
                s.character = p.character;
                any = true;
            }
            if p.team <= 1 && s.team != p.team {
                s.team = p.team;
                any = true;
            }
        }
        any
    }

    /// `peer`'s connection is gone (or it left): its seats open. Before the start only; a drop mid-match is the
    /// recovery's (N10, `crate::recover`).
    pub fn gone(&mut self, peer: &str) -> Vec<(To, Msg)> {
        if self.room.started || peer == HOST {
            return vec![];
        }
        let seats = self.room.seats_of(peer);
        if seats.is_empty() {
            return vec![];
        }
        for i in seats {
            self.room.seats[i] = self.saved.as_ref().and_then(|s| s[i].clone());
        }
        self.changed()
    }

    /// The host's own picks for its seats.
    pub fn picks(&mut self, picks: &[Pick]) -> Vec<(To, Msg)> {
        if self.room.started || self.room.resuming() || !self.pick(HOST, picks) {
            return vec![];
        }
        self.changed()
    }

    /// The host changes the rules (out-of-range values are clamped to what the game offers).
    pub fn settings(&mut self, board: u8, turns: u8, bonus: bool, teams: bool, handicaps: [u8; SEATS]) -> Vec<(To, Msg)> {
        if self.room.started || self.room.resuming() {
            return vec![];
        }
        let r = &mut self.room;
        let turns = turns.clamp(TURNS_MIN, TURNS_MAX) / 5 * 5;
        let before = (r.board, r.turns, r.bonus, r.teams, r.handicaps);
        r.board = board.min(BOARDS - 1);
        r.turns = turns;
        r.bonus = bonus;
        r.teams = teams;
        r.handicaps = handicaps.map(|h| h.min(HANDICAP_MAX));
        if before == (r.board, r.turns, r.bonus, r.teams, r.handicaps) {
            return vec![];
        }
        self.changed()
    }

    /// The host puts the computer in seat `i` (`difficulty` its level) or opens it again (`None`). A guest's seat
    /// can't be taken from it here. Resuming, every free seat is the computer's: only its level changes.
    pub fn cpu(&mut self, i: usize, difficulty: Option<u8>) -> Vec<(To, Msg)> {
        if self.room.started || i >= SEATS || self.room.seats[i].as_ref().is_some_and(|s| s.human()) {
            return vec![];
        }
        if self.room.resuming() {
            let Some(d) = difficulty else { return vec![] };
            let s = self.room.seats[i].as_mut().unwrap();
            s.difficulty = d.min(DIFFICULTIES - 1);
            return self.changed();
        }
        self.room.seats[i] = difficulty.map(|d| self.cpu_seat(i, d));
        self.changed()
    }

    fn cpu_seat(&self, i: usize, difficulty: u8) -> Seat {
        let character = match &self.room.seats[i] {
            Some(s) => s.character,
            None => self.room.free_character(Some(i)),
        };
        Seat {
            peer: CPU.into(),
            name: "CPU".into(),
            key: String::new(),
            character,
            cpu: true,
            difficulty: difficulty.min(DIFFICULTIES - 1),
            team: (i / 2) as u8,
        }
    }

    /// Starts the match with `seed`, ready guests or not (N148; [`Room::all_ready`] says whether to ask first): open
    /// seats become the computer's (normal difficulty), and every guest gets the final room. Err if it already started.
    pub fn start(&mut self, seed: u64) -> Result<Vec<(To, Msg)>, String> {
        self.start_with(seed, 0)
    }

    /// [`Host::start`] with the lockstep's input `delay` in the final room.
    pub fn start_with(&mut self, seed: u64, delay: u32) -> Result<Vec<(To, Msg)>, String> {
        if self.room.started {
            return Err("already started".into());
        }
        for i in 0..SEATS {
            if self.room.seats[i].is_none() {
                self.room.seats[i] = Some(self.cpu_seat(i, 1));
            }
        }
        self.room.seed = seed;
        self.room.delay = delay;
        self.room.started = true;
        Ok(vec![(To::All, Msg::Start(self.room.clone()))])
    }

    /// The match is over: the same people in an open room for the next one, as match `id` (a new online save), seed
    /// and delay unset, nobody ready, a continue's save forgotten. Seats (the computer's from the start included)
    /// and rules stay. Nothing if no match started.
    pub fn reopen(&mut self, id: String) -> Vec<(To, Msg)> {
        if !self.room.started {
            return vec![];
        }
        self.room.id = id;
        self.room.started = false;
        self.room.seed = 0;
        self.room.delay = 0;
        self.room.checkpoint = None;
        self.saved = None;
        self.changed()
    }
}

/// What a guest heard.
#[derive(Clone, Debug, PartialEq)]
pub enum Event {
    /// The room changed (the first one: seated).
    Room,
    Refused(String),
    Started,
    /// The host closed the lobby.
    HostLeft,
}

/// A guest's half: what the host last said.
pub struct Guest {
    me: String,
    pub room: Option<Room>,
    /// The host's room after the match ([`Host::reopen`]), kept while this guest still plays it ([`Guest::back`]).
    after: Option<Box<Room>>,
}

impl Guest {
    /// `me`: this guest's peer name as the host knows it (its endpoint ID).
    pub fn new(me: String) -> Guest {
        Guest { me, room: None, after: None }
    }

    /// The first message, to the host.
    pub fn hello(ident: Ident, players: Vec<Player>) -> Msg {
        Msg::Hello { ident, players }
    }

    /// The seats this guest holds (none before the host's first `Room`).
    pub fn seats(&self) -> Vec<usize> {
        self.room.as_ref().map(|r| r.seats_of(&self.me)).unwrap_or_default()
    }

    /// Ready (or not) for the line-up last seen; None before the first `Room`.
    pub fn ready(&self, ready: bool) -> Option<Msg> {
        self.room.as_ref().map(|r| Msg::Ready { ver: r.ver, ready })
    }

    /// Whether the host lists this guest ready for the current line-up.
    pub fn is_ready(&self) -> bool {
        self.room.as_ref().is_some_and(|r| r.ready.contains(&self.me))
    }

    /// This guest's match is over: back in the open room, the host's room after the match if it came already, else
    /// the started one opened as the host does (nobody ready) until the host's comes. False if no match started.
    pub fn back(&mut self) -> bool {
        let Some(r) = self.room.as_mut().filter(|r| r.started) else { return false };
        match self.after.take() {
            Some(a) => *r = *a,
            None => {
                r.started = false;
                r.seed = 0;
                r.delay = 0;
                r.checkpoint = None;
                r.ready.clear();
            }
        }
        true
    }

    /// A message from the host.
    pub fn on(&mut self, m: Msg) -> Option<Event> {
        match m {
            Msg::Room(r) if !self.room.as_ref().is_some_and(|o| o.started) => {
                self.room = Some(r);
                Some(Event::Room)
            }
            // the host is back in the open room while this guest still plays the match
            Msg::Room(r) if !r.started => {
                self.after = Some(Box::new(r));
                None
            }
            Msg::Start(r) => {
                self.room = Some(r);
                Some(Event::Started)
            }
            Msg::Refused(why) => Some(Event::Refused(why)),
            Msg::Leave => Some(Event::HostLeft),
            _ => None,
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::collections::BTreeMap;

    fn ident() -> Ident {
        Ident { build: "0.1.0 abc".into(), disc: "GMPE01-00".into() }
    }

    fn player(n: &str) -> Player {
        Player { name: n.into(), key: format!("key-{n}") }
    }

    /// A host and guests on an in-memory wire; every message goes through JSON as on the stream.
    struct Net {
        host: Host,
        guests: BTreeMap<String, Guest>,
        heard: Vec<(String, Event)>,
    }

    fn wire(m: &Msg) -> Msg {
        serde_json::from_slice(&serde_json::to_vec(m).unwrap()).unwrap()
    }

    impl Net {
        fn new() -> Net {
            Net { host: Host::new("m1".into(), ident(), &[player("h")]), guests: BTreeMap::new(), heard: vec![] }
        }

        fn deliver(&mut self, out: Vec<(To, Msg)>) {
            for (to, m) in out {
                let names: Vec<String> = match to {
                    To::Peer(p) => vec![p],
                    To::All => self.host.room().peers().into_iter().skip(1).collect(),
                };
                for n in names {
                    if let Some(g) = self.guests.get_mut(&n) {
                        if let Some(e) = g.on(wire(&m)) {
                            self.heard.push((n, e));
                        }
                    }
                }
            }
        }

        fn send(&mut self, from: &str, m: Msg) {
            let out = self.host.on(from, wire(&m));
            self.deliver(out);
        }

        fn join(&mut self, name: &str, ident: Ident, players: Vec<Player>) {
            self.guests.insert(name.into(), Guest::new(name.into()));
            self.send(name, Guest::hello(ident, players));
        }

        fn ready(&mut self, name: &str) {
            let m = self.guests[name].ready(true).unwrap();
            self.send(name, m);
        }

        fn last(&self, name: &str) -> Option<&Event> {
            self.heard.iter().rev().find(|(n, _)| n == name).map(|(_, e)| e)
        }
    }

    #[test]
    fn four_peers_join_pick_start_and_agree() {
        let mut n = Net::new();
        for g in ["g1", "g2", "g3"] {
            n.join(g, ident(), vec![player(g)]);
            assert_eq!(n.last(g), Some(&Event::Room), "{g} seated");
        }
        assert_eq!(n.host.room().peers(), ["host", "g1", "g2", "g3"]);
        n.host.settings(3, 30, false, false, [0, 1, 0, 2]).into_iter().for_each(|o| n.deliver(vec![o]));
        let out = n.host.picks(&[Pick { character: 7, team: 0 }]);
        n.deliver(out);
        for (g, c) in [("g1", 4), ("g2", 5), ("g3", 6)] {
            n.send(g, Msg::Picks(vec![Pick { character: c, team: 1 }]));
        }
        assert!(!n.host.room().all_ready(), "nobody ready yet");
        for g in ["g1", "g2", "g3"] {
            n.ready(g);
        }
        let out = n.host.start(42).unwrap();
        n.deliver(out);

        let room = n.host.room().clone();
        assert!(room.started);
        assert_eq!((room.board, room.turns, room.bonus, room.seed), (3, 30, false, 42));
        assert_eq!(room.handicaps, [0, 1, 0, 2]);
        let chars: Vec<u8> = room.seats.iter().map(|s| s.as_ref().unwrap().character).collect();
        assert_eq!(chars, [7, 4, 5, 6]);
        assert!(room.seats.iter().flatten().all(Seat::human));
        for (name, g) in &n.guests {
            assert_eq!(n.last(name), Some(&Event::Started));
            assert_eq!(g.room.as_ref(), Some(&room), "{name} sees the host's room");
        }
        assert_eq!(n.guests["g2"].seats(), [2]);
    }

    /// After the match the host reopens the room; a guest still playing keeps its started room until it is back
    /// itself, one back before the host's room came sees the started room opened, and the next match starts alike.
    #[test]
    fn back_in_the_lobby_after_the_match() {
        let mut n = Net::new();
        for g in ["g1", "g2"] {
            n.join(g, ident(), vec![player(g)]);
        }
        n.ready("g1");
        n.ready("g2");
        let out = n.host.start(42).unwrap();
        n.deliver(out);
        let started = n.host.room().clone();
        assert!(started.seats[3].as_ref().unwrap().cpu, "the open seat is the computer's");
        // g1's match ends first: before the host's room comes, it sees the started room opened
        assert!(n.guests.get_mut("g1").unwrap().back());
        let g1 = n.guests["g1"].room.clone().unwrap();
        assert!(!g1.started && g1.seed == 0 && g1.ready.is_empty() && g1.id == "m1");

        // the host is done: the room reopens for everyone; g2 still plays
        let out = n.host.reopen("m2".into());
        n.deliver(out);
        let open = n.host.room().clone();
        assert!(!open.started && open.seed == 0 && open.delay == 0 && open.ready.is_empty() && open.checkpoint.is_none());
        assert_eq!((open.id.as_str(), open.ver), ("m2", started.ver + 1));
        assert_eq!(open.seats, started.seats, "same seats, the computer's included");
        assert_eq!(n.guests["g1"].room.as_ref(), Some(&open), "g1 (back) took it");
        assert_eq!(n.guests["g2"].room.as_ref(), Some(&started), "g2 still plays the started room");
        assert!(n.host.reopen("m3".into()).is_empty(), "reopens once");
        assert!(n.guests.get_mut("g2").unwrap().back());
        assert_eq!(n.guests["g2"].room.as_ref(), Some(&open), "g2 back: the host's room");
        assert!(!n.guests.get_mut("g2").unwrap().back(), "back once");

        // a stale ready (old line-up) is answered with the room; then the next match
        n.send("g1", Msg::Ready { ver: started.ver, ready: true });
        assert!(!n.host.room().all_ready());
        n.ready("g1");
        n.ready("g2");
        let out = n.host.start(7).unwrap();
        n.deliver(out);
        for g in ["g1", "g2"] {
            assert_eq!(n.last(g), Some(&Event::Started));
            assert_eq!(n.guests[g].room.as_ref().map(|r| (r.id.as_str(), r.seed)), Some(("m2", 7)));
        }
    }

    #[test]
    fn empty_seats_become_cpu_at_start() {
        let mut n = Net::new();
        n.join("g1", ident(), vec![player("g1")]);
        n.ready("g1");
        let out = n.host.start(7).unwrap();
        n.deliver(out);
        let r = n.guests["g1"].room.clone().unwrap();
        assert_eq!(&r, n.host.room());
        let cpus: Vec<&Seat> = r.seats.iter().flatten().filter(|s| s.cpu).collect();
        assert_eq!(cpus.len(), 2);
        assert!(cpus.iter().all(|s| s.peer == CPU && s.key.is_empty()));
        let mut chars: Vec<u8> = r.seats.iter().flatten().map(|s| s.character).collect();
        chars.sort();
        chars.dedup();
        assert_eq!(chars.len(), SEATS, "every seat a different character");
    }

    /// N148: the host starts with guests not ready; only the host can, and every guest starts on the same room with
    /// the picks it made (ready or not).
    #[test]
    fn host_starts_without_everyone_ready() {
        let mut n = Net::new();
        for g in ["g1", "g2", "g3"] {
            n.join(g, ident(), vec![player(g)]);
        }
        n.send("g2", Msg::Picks(vec![Pick { character: 5, team: 1 }]));
        n.ready("g1");
        assert!(!n.host.room().all_ready());

        // a guest can't start it: a Start from one is ignored
        let mut fake = n.host.room().clone();
        fake.started = true;
        fake.seed = 99;
        let heard = n.heard.len();
        n.send("g1", Msg::Start(fake));
        assert!(!n.host.room().started && n.heard.len() == heard, "a guest's Start does nothing");

        let out = n.host.start(42).unwrap();
        n.deliver(out);
        let room = n.host.room().clone();
        assert!(room.started && room.seed == 42);
        assert_eq!(room.seats[2].as_ref().map(|s| (s.peer.as_str(), s.character)), Some(("g2", 5)), "g2's pick");
        for g in ["g1", "g2", "g3"] {
            assert_eq!(n.last(g), Some(&Event::Started), "{g} started, ready or not");
            assert_eq!(n.guests[g].room.as_ref(), Some(&room), "{g} holds the host's room");
        }
        // a late ready changes nothing, and it starts once
        n.ready("g3");
        assert_eq!(n.host.room(), &room);
        assert!(n.host.start(42).is_err(), "already started");
    }

    #[test]
    fn mismatched_build_or_disc_is_refused() {
        let mut n = Net::new();
        n.join("old", Ident { build: "0.0.9 xyz".into(), ..ident() }, vec![player("o")]);
        match n.last("old") {
            Some(Event::Refused(why)) => assert!(why.contains("different version"), "{why}"),
            e => panic!("{e:?}"),
        }
        n.join("pal", Ident { disc: "GMPP01-00".into(), ..ident() }, vec![player("p")]);
        match n.last("pal") {
            Some(Event::Refused(why)) => assert!(why.contains("different disc"), "{why}"),
            e => panic!("{e:?}"),
        }
        assert_eq!(n.host.room().peers(), ["host"], "nobody seated");
        assert_eq!(n.host.room().ver, 0);
    }

    #[test]
    fn full_lobby_refuses_and_two_local_players_take_two_seats() {
        let mut n = Net::new();
        n.join("g1", ident(), vec![player("a"), player("b")]);
        assert_eq!(n.guests["g1"].seats(), [1, 2]);
        n.join("g2", ident(), vec![player("c"), player("d")]);
        assert!(matches!(n.last("g2"), Some(Event::Refused(w)) if w.contains("full")));
        n.join("g3", ident(), vec![player("c")]);
        assert_eq!(n.guests["g3"].seats(), [3]);
        assert!(!n.host.room().all_ready());
    }

    #[test]
    fn taken_character_is_kept_and_stale_ready_ignored() {
        let mut n = Net::new();
        n.join("g1", ident(), vec![player("g1")]);
        n.join("g2", ident(), vec![player("g2")]);
        let host_char = n.host.room().seats[0].as_ref().unwrap().character;
        let before = n.host.room().seats[1].clone();
        n.send("g1", Msg::Picks(vec![Pick { character: host_char, team: before.as_ref().unwrap().team }]));
        assert_eq!(n.host.room().seats[1], before, "character held by the host");

        let stale = n.guests["g1"].ready(true).unwrap();
        n.send("g2", Msg::Picks(vec![Pick { character: 6, team: 0 }]));
        n.send("g1", stale);
        assert!(!n.guests["g1"].is_ready(), "ready for an old line-up");
        n.ready("g1");
        assert!(n.guests["g1"].is_ready());
        n.ready("g2");
        let out = n.host.settings(1, 35, true, false, [0; SEATS]);
        n.deliver(out);
        assert!(n.host.room().ready.is_empty(), "a change clears ready");
        assert!(!n.host.room().all_ready());
    }

    #[test]
    fn leave_frees_seats() {
        let mut n = Net::new();
        n.join("g1", ident(), vec![player("g1")]);
        assert_eq!(n.guests["g1"].seats(), [1]);
        n.send("g1", Msg::Leave);
        assert_eq!(n.host.room().peers(), ["host"]);
        assert!(n.host.room().seats[1].is_none());
    }

    /// N89: two copies of the game on one PC share a profile, so the guest's Hello carries the host's own key. The
    /// host's seat is held (the host is here): the guest is another player, with a seat and a fresh key of its own.
    /// Likewise a second guest with a connected guest's key.
    #[test]
    fn a_key_whose_player_is_here_takes_a_new_seat() {
        let mut n = Net::new();
        n.join("g1", ident(), vec![player("h")]);
        assert_eq!(n.last("g1"), Some(&Event::Room));
        assert_eq!(n.guests["g1"].seats(), [1], "a seat of its own");
        let r = n.host.room();
        assert_eq!(r.seats_of(HOST), [0], "the host keeps its seat");
        assert_eq!(r.seats.iter().flatten().filter(|s| s.human()).count(), 2);
        assert_eq!(r.seats[0].as_ref().unwrap().key, "key-h");
        assert_eq!(r.seats[1].as_ref().unwrap().key, "key-h-2", "a fresh key, which the guest sees");
        assert_eq!(n.guests["g1"].room.as_ref().unwrap().seats[1].as_ref().unwrap().key, "key-h-2");

        n.join("g2", ident(), vec![player("h"), player("g2")]);
        assert_eq!(n.guests["g2"].seats(), [2, 3]);
        let keys: Vec<&str> = n.host.room().seats.iter().flatten().map(|s| s.key.as_str()).collect();
        assert_eq!(keys, ["key-h", "key-h-2", "key-h-3", "key-g2"], "every key once");
        n.ready("g1");
        n.ready("g2");
        n.host.start(1).unwrap();
        assert_eq!(n.host.room().seats.iter().flatten().filter(|s| s.human()).count(), 4);
    }

    /// N89: resuming, a seat whose player is away (the computer plays it) still goes back to its key; one whose
    /// player is here doesn't, and the same key on a new connection is someone else. A player who drops and comes
    /// back gets its seat again.
    #[test]
    fn continue_reclaims_only_seats_of_absent_players() {
        let saved = played_match();
        let mut n = resumed(&saved, &[player("h")]);
        // the host's own key from another copy: not seat 0, the seat the computer played in the save (3)
        n.join("twin", ident(), vec![player("h")]);
        assert_eq!(n.guests["twin"].seats(), [3]);
        assert_eq!(n.host.room().seats_of(HOST), [0]);
        assert_eq!(n.host.room().seats[3].as_ref().unwrap().key, "key-h-2");
        // g1 back by key, then dropped: the computer's again, still with g1's key; g1 rejoins and takes it back
        n.join("g1", ident(), vec![player("g1")]);
        assert_eq!(n.guests["g1"].seats(), [1]);
        n.join("g1-twin", ident(), vec![player("g1")]);
        assert_eq!(n.guests["g1-twin"].seats(), [2], "g1 is here: an absent player's seat, not g1's");
        n.send("g1-twin", Msg::Leave);
        let out = n.host.gone("g1");
        n.deliver(out);
        let s1 = n.host.room().seats[1].as_ref().unwrap();
        assert_eq!((s1.peer.as_str(), s1.key.as_str()), (CPU, "key-g1"));
        n.join("g1b", ident(), vec![player("g1")]);
        assert_eq!(n.guests["g1b"].seats(), [1], "back by key");
        assert_eq!(n.host.room().seats[1].as_ref().unwrap().character, 5, "the save's character");
    }

    #[test]
    fn host_settings_clamp_and_cpu_seats() {
        let mut n = Net::new();
        n.host.settings(99, 47, true, true, [20, 0, 0, 0]);
        let r = n.host.room();
        assert_eq!((r.board, r.turns, r.handicaps[0]), (BOARDS - 1, 45, HANDICAP_MAX));
        n.host.cpu(2, Some(9));
        assert_eq!(n.host.room().seats[2].as_ref().unwrap().difficulty, DIFFICULTIES - 1);
        assert!(n.host.cpu(0, None).is_empty(), "a human seat isn't the host's to clear");
        n.join("g1", ident(), vec![player("g1")]);
        assert_eq!(n.guests["g1"].seats(), [1]);
        n.host.cpu(2, None);
        assert!(n.host.room().seats[2].is_none());
    }

    /// A started room: host "h" in seat 0, guests g1 (character 5) and g2 in seats 1 and 2, the computer in 3.
    fn played_match() -> Room {
        let mut n = Net::new();
        n.join("g1", ident(), vec![player("g1")]);
        n.join("g2", ident(), vec![player("g2")]);
        n.send("g1", Msg::Picks(vec![Pick { character: 5, team: 1 }]));
        let out = n.host.settings(4, 35, false, true, [1, 0, 2, 0]);
        n.deliver(out);
        let out = n.host.cpu(3, Some(2));
        n.deliver(out);
        n.ready("g1");
        n.ready("g2");
        n.host.start(11).unwrap();
        n.host.room().clone()
    }

    fn checkpoint() -> Checkpoint {
        Checkpoint { blob: "00ff".into(), turn: 12, max_turn: 35, time: 1 }
    }

    /// A Net whose host continues `saved` (players: the host's local ones).
    fn resumed(saved: &Room, host_players: &[Player]) -> Net {
        let host = Host::resume(saved, checkpoint(), ident(), host_players).unwrap();
        Net { host, guests: BTreeMap::new(), heard: vec![] }
    }

    #[test]
    fn continue_gives_seats_back_by_key() {
        let saved = played_match();
        // g2 hosts the continue this time; the old host and g1 join from new endpoints
        let mut n = resumed(&saved, &[player("g2")]);
        let r = n.host.room().clone();
        assert!(r.resuming() && !r.started && r.seed == 0);
        assert_eq!((r.id.as_str(), r.board, r.turns, r.teams), (saved.id.as_str(), 4, 35, true));
        assert_eq!(r.seats_of(HOST), [2], "g2's key holds seat 2");
        assert!(r.seats[0].as_ref().unwrap().cpu && r.seats[1].as_ref().unwrap().cpu, "absent players: the computer");
        assert_eq!(r.seats[0].as_ref().unwrap().key, "key-h", "kept for its player");
        n.join("new-h", ident(), vec![player("h")]);
        n.join("new-g1", ident(), vec![player("g1")]);
        assert_eq!(n.guests["new-h"].seats(), [0]);
        assert_eq!(n.guests["new-g1"].seats(), [1]);
        let r = n.host.room();
        for i in 0..SEATS {
            let (a, b) = (r.seats[i].as_ref().unwrap(), saved.seats[i].as_ref().unwrap());
            assert_eq!((a.character, a.team, a.cpu), (b.character, b.team, b.cpu), "seat {i} as saved");
        }
        assert_eq!(r.seats[1].as_ref().unwrap().character, 5);
        // the guests see the checkpoint (they need no save of their own)
        assert_eq!(n.guests["new-g1"].room.as_ref().unwrap().checkpoint, Some(checkpoint()));
        n.ready("new-h");
        n.ready("new-g1");
        let out = n.host.start(99).unwrap();
        n.deliver(out);
        let r = n.host.room().clone();
        assert!(r.started && r.seed == 99 && r.checkpoint.is_some());
        assert_eq!(n.guests["new-h"].room.as_ref(), Some(&r));
    }

    #[test]
    fn continue_unclaimed_seats_go_to_anyone_or_stay_cpu() {
        let saved = played_match();
        let mut n = resumed(&saved, &[player("h")]);
        // a stranger takes the seat the computer played (3) before an absent player's (1, 2)
        n.join("s1", ident(), vec![player("stranger")]);
        assert_eq!(n.guests["s1"].seats(), [3]);
        // a second stranger: an absent player's seat, which is now theirs (their key)
        n.join("s2", ident(), vec![player("other")]);
        assert_eq!(n.guests["s2"].seats(), [1]);
        assert_eq!(n.host.room().seats[1].as_ref().unwrap().key, "key-other");
        assert_eq!(n.host.room().seats[1].as_ref().unwrap().character, 5, "the save's character");
        // characters, teams and rules are the save's
        let before = n.host.room().clone();
        n.send("s2", Msg::Picks(vec![Pick { character: 0, team: 0 }]));
        assert!(n.host.settings(0, 10, true, false, [0; SEATS]).is_empty());
        assert!(n.host.picks(&[Pick { character: 7, team: 0 }]).is_empty());
        assert_eq!(n.host.room(), &before);
        // the host sets the level of the computer in seat 2 (g2 didn't come back), can't open it
        assert!(n.host.cpu(2, None).is_empty());
        let out = n.host.cpu(2, Some(3));
        n.deliver(out);
        assert_eq!(n.host.room().seats[2].as_ref().unwrap().difficulty, 3);
        assert!(n.host.cpu(1, Some(0)).is_empty(), "a player's seat");
        // s2 leaves: seat 1 is the save's again (g1's key, the computer)
        n.send("s2", Msg::Leave);
        let s1 = n.host.room().seats[1].as_ref().unwrap();
        assert_eq!((s1.peer.as_str(), s1.key.as_str(), s1.cpu), (CPU, "key-g1", true));
        // g1 is back and takes it; the lobby is then full for one more
        n.join("g1b", ident(), vec![player("g1")]);
        assert_eq!(n.guests["g1b"].seats(), [1]);
        n.join("late", ident(), vec![player("x"), player("y")]);
        assert!(matches!(n.last("late"), Some(Event::Refused(w)) if w.contains("full")));
        for g in ["s1", "g1b"] {
            n.ready(g);
        }
        n.host.start(5).unwrap();
        let r = n.host.room();
        assert!(r.seats[2].as_ref().unwrap().cpu, "nobody took seat 2: the computer plays it");
        assert_eq!(r.seats.iter().flatten().filter(|s| s.human()).count(), 3);
    }

    #[test]
    fn continue_needs_a_started_room() {
        let n = Net::new();
        assert!(Host::resume(n.host.room(), checkpoint(), ident(), &[player("h")]).is_err());
    }

    #[test]
    fn nothing_changes_after_start() {
        let mut n = Net::new();
        n.host.start(3).unwrap();
        n.join("late", ident(), vec![player("l")]);
        assert!(matches!(n.last("late"), Some(Event::Refused(w)) if w.contains("started")));
        assert!(n.host.settings(2, 10, false, false, [0; SEATS]).is_empty());
        assert!(n.host.start(3).is_err());
    }
}
