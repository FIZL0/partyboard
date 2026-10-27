//! The lobby protocol (ported from opentennis `hst-base/src/net.rs` `Msg`/`Room`, without Bevy or mods).
//!
//! Host-authoritative: the host keeps the one [`Room`] and sends it whole to every guest on every change. On the
//! lobby stream (`transport::Peer<Msg>`) the joiner speaks first:
//!
//! - guest → host [`Msg::Hello`]: its build id, disc revision and local players (name, stable player key). The host
//!   answers [`Msg::Refused`] (other build, other disc, full, already started) or seats the players and sends
//!   [`Msg::Room`] to everyone.
//! - guest → host [`Msg::Picks`]: characters and teams for its seats, in seat order. A pick the host can't take
//!   (a character another seat has) keeps the seat's old one; the next `Room` says so.
//! - guest → host [`Msg::Ready`]: ready (or not) for line-up [`Room::ver`]. Every change of seats or settings bumps
//!   `ver` and clears the ready flags, so a guest is only ever ready for what it last saw.
//! - host → all [`Msg::Start`]: the final `Room` once every guest is ready and the host starts. Open seats are the
//!   computer's from then on ([`CPU`]), the match seed is set.
//! - either way [`Msg::Leave`]: a guest leaving frees its seats; the host leaving ends the lobby.
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
        }
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

    fn open(&self) -> usize {
        self.seats.iter().filter(|s| s.is_none()).count()
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
}

impl Host {
    /// A new lobby `id`, its first seats for the host's local players (1 to 4).
    pub fn new(id: String, ident: Ident, players: &[Player]) -> Host {
        let mut h = Host { room: Room::new(id), ident };
        h.seat(HOST, players);
        h
    }

    pub fn room(&self) -> &Room {
        &self.room
    }

    /// Seats `players` for `peer`: first the seats their keys already hold (a player back after a reconnect),
    /// then the open ones in order.
    fn seat(&mut self, peer: &str, players: &[Player]) {
        for p in players.iter().take(SEATS) {
            let back = (0..SEATS).find(|&i| {
                self.room.seats[i].as_ref().is_some_and(|s| !p.key.is_empty() && s.key == p.key && s.peer != peer)
            });
            if let Some(i) = back {
                let s = self.room.seats[i].as_mut().unwrap();
                self.room.ready.remove(&s.peer);
                s.peer = peer.into();
                s.name = p.name.clone();
                continue;
            }
            let Some(i) = (0..SEATS).find(|&i| self.room.seats[i].is_none()) else { return };
            let character = self.room.free_character(None);
            self.room.seats[i] = Some(Seat {
                peer: peer.into(),
                name: p.name.clone(),
                key: p.key.clone(),
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
                let back =
                    players.iter().filter(|p| !p.key.is_empty() && self.room.seats.iter().flatten().any(|s| s.key == p.key)).count();
                if players.len() - back > self.room.open() {
                    return refuse("the lobby is full".into());
                }
                self.seat(peer, &players);
                self.changed()
            }
            Msg::Picks(picks) if seated && !self.room.started => {
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
    /// recovery's (N10).
    pub fn gone(&mut self, peer: &str) -> Vec<(To, Msg)> {
        if self.room.started || peer == HOST {
            return vec![];
        }
        let seats = self.room.seats_of(peer);
        if seats.is_empty() {
            return vec![];
        }
        for i in seats {
            self.room.seats[i] = None;
        }
        self.changed()
    }

    /// The host's own picks for its seats.
    pub fn picks(&mut self, picks: &[Pick]) -> Vec<(To, Msg)> {
        if self.room.started || !self.pick(HOST, picks) {
            return vec![];
        }
        self.changed()
    }

    /// The host changes the rules (out-of-range values are clamped to what the game offers).
    pub fn settings(&mut self, board: u8, turns: u8, bonus: bool, teams: bool, handicaps: [u8; SEATS]) -> Vec<(To, Msg)> {
        if self.room.started {
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
    /// can't be taken from it here.
    pub fn cpu(&mut self, i: usize, difficulty: Option<u8>) -> Vec<(To, Msg)> {
        if self.room.started || i >= SEATS || self.room.seats[i].as_ref().is_some_and(|s| s.human()) {
            return vec![];
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

    /// Starts the match with `seed` once every guest is ready: open seats become the computer's (normal
    /// difficulty), and every guest gets the final room. Err (why) if someone isn't ready or it already started.
    pub fn start(&mut self, seed: u64) -> Result<Vec<(To, Msg)>, String> {
        if self.room.started {
            return Err("already started".into());
        }
        if !self.room.all_ready() {
            return Err("not everyone is ready".into());
        }
        for i in 0..SEATS {
            if self.room.seats[i].is_none() {
                self.room.seats[i] = Some(self.cpu_seat(i, 1));
            }
        }
        self.room.seed = seed;
        self.room.started = true;
        Ok(vec![(To::All, Msg::Start(self.room.clone()))])
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
}

impl Guest {
    /// `me`: this guest's peer name as the host knows it (its endpoint ID).
    pub fn new(me: String) -> Guest {
        Guest { me, room: None }
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

    /// A message from the host.
    pub fn on(&mut self, m: Msg) -> Option<Event> {
        match m {
            Msg::Room(r) if !self.room.as_ref().is_some_and(|o| o.started) => {
                self.room = Some(r);
                Some(Event::Room)
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
        assert!(n.host.start(42).is_err(), "nobody ready yet");
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
        assert!(n.host.start(1).is_err());
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
        assert!(n.host.start(1).is_err());
    }

    #[test]
    fn leave_frees_seats_and_key_takes_seat_back() {
        let mut n = Net::new();
        n.join("g1", ident(), vec![player("g1")]);
        n.send("g1", Msg::Picks(vec![Pick { character: 5, team: 1 }]));
        // the same player on a new connection (another endpoint) gets its seat and pick back
        n.join("g1b", ident(), vec![player("g1")]);
        assert_eq!(n.guests["g1b"].seats(), [1]);
        assert_eq!(n.host.room().seats[1].as_ref().unwrap().character, 5);
        assert_eq!(n.host.room().peers(), ["host", "g1b"]);
        n.send("g1b", Msg::Leave);
        assert_eq!(n.host.room().peers(), ["host"]);
        assert!(n.host.room().seats[1].is_none());
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
