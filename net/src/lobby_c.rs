//! The lobby on the network, and its C ABI (`include/port/pbnet.h`) for the online menu (N7).
//!
//! A [`Lobby`] is a tokio task driving [`lobby::Host`] or [`lobby::Guest`] over `transport::Peer<Msg>`:
//!
//! - **Host**: binds an endpoint, shows its ticket (and a short code, republished while the lobby is open), accepts
//!   joiners; each connection gets a task that feeds its messages into the `Host` and writes what the `Host` sends
//!   it. A connection that ends is [`Host::gone`] (its seats open again before the start).
//! - **Guest**: dials the code, says Hello, and keeps the `Guest`'s view of the room. A refusal, the host's Leave or
//!   a lost connection before the start closes the lobby with the reason in words.
//!
//! Both sides keep everything the menu needs in one [`Shared`] behind a mutex; the menu's commands (picks, ready,
//! settings, start) run on the caller's thread against it, so they answer at once, and only bytes go through the
//! tasks. The menu polls [`Snapshot`] (as JSON in C) when [`Snapshot::gen`] moved.
//!
//! At the start the lockstep runs on the same connections ([`Wire`]): datagrams, addressed by line-up index
//! ([`Room::peers`]), guest to guest through the host. The host picks the input delay from its connections' round
//! trips and puts it in the final room; every peer then hands its [`Config`] and [`Wire`] to the [`Starter`]
//! (`lockstep_c::start` in C). A drop after the start is N10's.
//!
//! On a shared network an open host lobby puts a [`Listing`] in its endpoint's mDNS record
//! ([`Transport::set_lan_listing`]), kept to the room's open seats and taken down at the start or close. A [`Browse`]
//! binds an endpoint with no lobby to hear them ([`Transport::lan`]); a join then takes that endpoint over.

use std::collections::BTreeMap;
use std::ffi::{c_char, CStr};
use std::future::Future;
use std::sync::{Arc, Mutex, OnceLock};
use std::time::{Duration, SystemTime};

use serde::{Deserialize, Serialize};
use tokio::runtime::{Handle, Runtime};
use tokio::sync::{mpsc, oneshot};

use crate::codes;
use crate::lobby::{Event, Guest, Host, Ident, Msg, Pick, Player, Room, To, CPU, HOST, SEATS};
use crate::lockstep::{self, delay_for, Config, NO_PEER};
use crate::lockstep_c;
use crate::transport::{datagram, undatagram, Incoming, LanHost, Peer, Relay, Transport};

/// No lobby.
pub const PBNET_LOBBY_NONE: i32 = 0;
/// Binding the endpoint (host) or dialling and waiting to be seated (guest).
pub const PBNET_LOBBY_CONNECTING: i32 = 1;
/// Hosting, or seated in the host's room.
pub const PBNET_LOBBY_OPEN: i32 = 2;
/// The match started: the room is final.
pub const PBNET_LOBBY_STARTED: i32 = 3;
/// Over (refused, the host left, the connection failed): the snapshot's `error` says why.
pub const PBNET_LOBBY_CLOSED: i32 = 4;

/// How long a refused joiner's connection stays up so the refusal arrives.
const REFUSED_LINGER: Duration = Duration::from_secs(2);
/// How long a leaving side waits for its Leave to go out before closing.
const LEAVE_LINGER: Duration = Duration::from_millis(300);
/// How often the host reads a guest connection's round trip (for the input delay).
const RTT_EVERY: Duration = Duration::from_millis(250);

/// Takes the lockstep a started lobby made: this machine's line-up and the wire to the others.
pub type Starter = Arc<dyn Fn(Config, Wire) + Send + Sync>;

/// The process's lockstep session ([`lockstep_c::start`]): the C ABI's starter.
pub fn global_starter() -> Starter {
    Arc::new(|cfg, wire| lockstep_c::start(cfg, Box::new(wire)))
}

/// The lockstep's datagrams on the lobby's connections.
///
/// On the wire each is a [`datagram`] whose peer name is a line-up index in decimal (the sender's from the host,
/// the receiver's to it), not an endpoint ID: a lockstep datagram with a full backlog is ~1075 bytes, and 64
/// characters of ID would push it past [`crate::transport::DATAGRAM_MAX`] (lost every time, so the stall that made
/// the backlog would never end). The host delivers its own and relays guest to guest, renamed to the sender.
pub struct Wire {
    /// Host: each guest's datagram outbox by line-up index (None for the host); guest: the host's, alone.
    out: Vec<Option<mpsc::UnboundedSender<Vec<u8>>>>,
    host: bool,
    inbox: mpsc::UnboundedReceiver<(u8, Vec<u8>)>,
}

impl lockstep::Transport for Wire {
    fn send(&mut self, to: u8, d: &[u8]) {
        let (via, name) = if self.host { (to as usize, "0".to_string()) } else { (0, to.to_string()) };
        if let Some(Some(tx)) = self.out.get(via) {
            let _ = tx.send(datagram(&name, d));
        }
    }

    fn recv(&mut self) -> Option<(u8, Vec<u8>)> {
        self.inbox.try_recv().ok()
    }
}

/// The input delay for a line-up: [`delay_for`] the slowest round trip, which is guest to guest through the host
/// (the two slowest guests' round trips to it) with two or more guests. Unmeasured round trips count as 0.
fn delay_of(rtts: &[u32]) -> u32 {
    let mut r = rtts.to_vec();
    r.sort_unstable_by(|a, b| b.cmp(a));
    delay_for(r.iter().take(2).sum())
}

#[derive(Clone, Copy, Debug, PartialEq, Eq, Serialize)]
#[serde(rename_all = "lowercase")]
pub enum Phase {
    Connecting,
    Open,
    Started,
    Closed,
}

impl Phase {
    fn c(self) -> i32 {
        match self {
            Phase::Connecting => PBNET_LOBBY_CONNECTING,
            Phase::Open => PBNET_LOBBY_OPEN,
            Phase::Started => PBNET_LOBBY_STARTED,
            Phase::Closed => PBNET_LOBBY_CLOSED,
        }
    }
}

enum Role {
    Host(Host),
    Guest(Guest),
}

/// What the tasks and the menu share.
struct Shared {
    phase: Phase,
    host: bool,
    role: Option<Role>,
    me: String,
    ticket: String,
    short: String,
    error: String,
    /// Host: each guest connection's outbox by peer name; guest: the host's under [`HOST`].
    peers: BTreeMap<String, mpsc::UnboundedSender<Msg>>,
    /// The same connections' datagram outboxes.
    dgrams: BTreeMap<String, mpsc::UnboundedSender<Vec<u8>>>,
    /// Host: each guest connection's round trip (ms), last read.
    rtts: BTreeMap<String, u32>,
    /// Once the lockstep runs: the line-up ([`Room::peers`]) and its session's inbox.
    lineup: Vec<String>,
    inbox: Option<mpsc::UnboundedSender<(u8, Vec<u8>)>>,
    /// The lockstep of the current match was handed to the starter (cleared by [`Shared::back`]). The line-up and
    /// inbox above outlive it until the next start: the drain of the match that ended, and the host's relay between
    /// guests still finishing it, go on over them.
    lockstep_on: bool,
    /// Host: the first characters of its endpoint ID, for the next match's id.
    id_prefix: String,
    start: Starter,
    gen: u32,
    /// Host: the endpoint whose mDNS record carries the LAN listing (and the runtime to set it on), the listing it
    /// carries now, and the name it gives.
    lan: Option<(Arc<Transport>, Handle)>,
    listed: Option<String>,
    host_name: String,
}

impl Shared {
    fn new(host: bool, start: Starter) -> Shared {
        Shared {
            phase: Phase::Connecting,
            host,
            role: None,
            me: String::new(),
            ticket: String::new(),
            short: String::new(),
            error: String::new(),
            peers: BTreeMap::new(),
            dgrams: BTreeMap::new(),
            rtts: BTreeMap::new(),
            lineup: Vec::new(),
            inbox: None,
            lockstep_on: false,
            id_prefix: String::new(),
            start,
            gen: 0,
            lan: None,
            listed: None,
            host_name: String::new(),
        }
    }

    fn bump(&mut self) {
        self.gen = self.gen.wrapping_add(1);
        self.relist();
    }

    /// Host: the LAN listing for the room as it is now (None unless open).
    fn listing(&self) -> Option<String> {
        let Some(Role::Host(h)) = &self.role else { return None };
        if self.phase != Phase::Open {
            return None;
        }
        let room = h.room();
        Listing { id: room.id.clone(), name: self.host_name.clone(), open: room.seats.iter().filter(|s| s.is_none()).count() as u8 }.fit()
    }

    /// Host: puts the listing in the mDNS record when it changed (none once started or closed).
    fn relist(&mut self) {
        let Some((t, rt)) = &self.lan else { return };
        let l = self.listing();
        if l != self.listed {
            // the endpoint republishes on its runtime; the menu's commands come from the caller's thread
            let _g = rt.enter();
            if let Err(e) = t.set_lan_listing(l.as_deref()) {
                eprintln!("pbnet: LAN listing: {e}");
            }
            self.listed = l;
        }
    }

    fn close(&mut self, why: &str) {
        if self.phase != Phase::Closed {
            self.phase = Phase::Closed;
            self.error = why.into();
            self.bump();
        }
    }

    fn host(&mut self) -> Option<&mut Host> {
        match &mut self.role {
            Some(Role::Host(h)) => Some(h),
            _ => None,
        }
    }

    /// Sends the host's messages to their peers.
    fn dispatch(&mut self, out: Vec<(To, Msg)>) {
        let Some(Role::Host(h)) = &self.role else { return };
        let guests: Vec<String> = h.room().peers().into_iter().skip(1).collect();
        for (to, m) in out {
            if matches!(m, Msg::Start(_)) {
                self.phase = Phase::Started;
            }
            let names = match to {
                To::Peer(p) => vec![p],
                To::All => guests.clone(),
            };
            for n in names {
                if let Some(tx) = self.peers.get(&n) {
                    let _ = tx.send(m.clone());
                }
            }
        }
        self.bump();
    }

    /// A host command: applied, sent, 1; or 0 when this isn't an open host lobby.
    fn host_cmd(&mut self, f: impl FnOnce(&mut Host) -> Vec<(To, Msg)>) -> bool {
        if self.phase != Phase::Open {
            return false;
        }
        let Some(h) = self.host() else { return false };
        let out = f(h);
        self.dispatch(out);
        true
    }

    /// A guest's message to the host: 1 if queued.
    fn send_host(&mut self, m: Msg) -> bool {
        match self.peers.get(HOST) {
            Some(tx) if self.phase == Phase::Open => tx.send(m).is_ok(),
            _ => false,
        }
    }

    /// Host: the input delay for the room's guests' round trips.
    fn delay(&self) -> u32 {
        let Some(Role::Host(h)) = &self.role else { return delay_for(0) };
        let rtts: Vec<u32> = h.room().peers().iter().skip(1).map(|p| self.rtts.get(p).copied().unwrap_or(0)).collect();
        delay_of(&rtts)
    }

    /// The match started: the lockstep on the started room's line-up, handed to the starter. Once.
    fn lockstep(&mut self) {
        if self.lockstep_on {
            return;
        }
        let room = match &self.role {
            Some(Role::Host(h)) => h.room().clone(),
            Some(Role::Guest(Guest { room: Some(r), .. })) => r.clone(),
            _ => return,
        };
        let lineup = room.peers();
        let Some(me) = lineup.iter().position(|p| *p == self.me) else { return };
        let mut seats = [NO_PEER; SEATS];
        for (i, s) in room.seats.iter().enumerate() {
            if let Some(s) = s.as_ref().filter(|s| s.peer != CPU) {
                seats[i] = lineup.iter().position(|p| *p == s.peer).map_or(NO_PEER, |p| p as u8);
            }
        }
        let out = if self.host {
            lineup.iter().map(|p| if p == HOST { None } else { self.dgrams.get(p).cloned() }).collect()
        } else {
            vec![self.dgrams.get(HOST).cloned()]
        };
        let (tx, inbox) = mpsc::unbounded_channel();
        let cfg = Config { me: me as u8, peers: lineup.len() as u8, seats, delay: room.delay.clamp(2, 15) };
        self.lineup = lineup;
        self.inbox = Some(tx);
        self.lockstep_on = true;
        (self.start)(cfg, Wire { out, host: self.host, inbox });
    }

    /// This machine's match is over: the lobby is open again with the same people. The host reopens the room for
    /// everyone as a new match id (a guest whose connection is gone loses its seats); a guest goes back to the room the
    /// host sent after the match, or will send, and is closed if its connection to the host is gone. False unless
    /// started.
    fn back(&mut self) -> bool {
        if self.phase != Phase::Started {
            return false;
        }
        self.lockstep_on = false;
        if self.host {
            let secs = SystemTime::now().duration_since(SystemTime::UNIX_EPOCH).unwrap_or_default().as_secs();
            let id = format!("{}-{secs}", self.id_prefix);
            let connected: Vec<String> = self.peers.keys().cloned().collect();
            let Some(h) = self.host() else { return false };
            let mut out = h.reopen(id);
            for p in h.room().peers().into_iter().skip(1) {
                if !connected.contains(&p) {
                    out.extend(h.gone(&p));
                }
            }
            self.phase = Phase::Open;
            self.dispatch(out);
            return true;
        }
        if let Some(Role::Guest(g)) = &mut self.role {
            g.back();
        }
        if self.peers.contains_key(HOST) {
            self.phase = Phase::Open;
            self.bump();
        } else {
            self.close("Lost the connection to the host.");
        }
        true
    }

    /// A lockstep datagram from the connection to `peer`: delivered here, or (host) relayed to the guest it is for.
    fn datagram_in(&self, peer: &str, d: &[u8]) {
        let (Some(inbox), Some((name, data))) = (&self.inbox, undatagram(d)) else { return };
        let Ok(idx) = name.parse::<usize>() else { return };
        if !self.host {
            // from the host: the name is the sender's
            if self.lineup.get(idx).is_some_and(|p| *p != self.me) {
                let _ = inbox.send((idx as u8, data.to_vec()));
            }
            return;
        }
        // from a guest: the name is the receiver's
        let Some(from) = self.lineup.iter().position(|p| p == peer) else { return };
        if idx == 0 {
            let _ = inbox.send((from as u8, data.to_vec()));
        } else if idx != from {
            if let Some(tx) = self.lineup.get(idx).and_then(|p| self.dgrams.get(p)) {
                let _ = tx.send(datagram(&from.to_string(), data));
            }
        }
    }

    fn snapshot(&self) -> Snapshot {
        let (room, seats) = match &self.role {
            Some(Role::Host(h)) => (Some(h.room().clone()), h.room().seats_of(HOST)),
            Some(Role::Guest(g)) => (g.room.clone(), g.seats()),
            None => (None, vec![]),
        };
        Snapshot {
            state: self.phase,
            host: self.host,
            me: self.me.clone(),
            code: self.ticket.clone(),
            short: self.short.clone(),
            error: self.error.clone(),
            seats,
            room,
            gen: self.gen,
        }
    }
}

type Sh = Arc<Mutex<Shared>>;

fn lock(s: &Sh) -> std::sync::MutexGuard<'_, Shared> {
    s.lock().unwrap_or_else(|e| e.into_inner())
}

/// The lobby as the menu sees it (`pbnet_lobby_json`).
#[derive(Clone, Debug, PartialEq, Serialize)]
pub struct Snapshot {
    pub state: Phase,
    /// This machine hosts.
    pub host: bool,
    /// This machine's peer name in the room ([`HOST`] for the host, else its endpoint ID).
    pub me: String,
    /// Host: its whole ticket (a join code that needs no DHT lookup); empty until the endpoint is up.
    pub code: String,
    /// Host: its short code once it is online; empty before (or if it couldn't be put online).
    pub short: String,
    /// Why the lobby closed, or why the short code isn't online.
    pub error: String,
    /// This machine's seats, in seat order.
    pub seats: Vec<usize>,
    /// The room as the host last sent it (None before a guest is seated).
    pub room: Option<Room>,
    /// Changes on every change above.
    pub gen: u32,
}

/// What hosting or joining needs, as the menu passes it (`pbnet_lobby_host` / `pbnet_lobby_join` JSON).
#[derive(Clone, Debug, Deserialize)]
pub struct Setup {
    pub build: String,
    pub disc: String,
    pub players: Vec<Player>,
    /// `n0`, `own` (with `relay_url`) or `off`, as [`Relay::parse`].
    #[serde(default)]
    pub relay: String,
    #[serde(default)]
    pub relay_url: String,
    /// The host's match id (default: from its endpoint ID and the time).
    #[serde(default)]
    pub id: Option<String>,
    /// The host puts a short code online (default on; tests without internet turn it off).
    #[serde(default = "yes")]
    pub short: bool,
    /// Host: continue the online save at this path (`pbnet_save_list`'s `path`): its room and checkpoint (N9).
    #[serde(default)]
    pub resume: Option<String>,
}

fn yes() -> bool {
    true
}

impl Setup {
    fn ident(&self) -> Ident {
        Ident { build: self.build.clone(), disc: self.disc.clone() }
    }
}

/// A running lobby: the shared state and the main task's quit switch.
pub struct Lobby {
    shared: Sh,
    quit: Option<oneshot::Sender<()>>,
}

impl Lobby {
    /// Hosts a lobby on the endpoint `bind` makes, its tasks on `rt`; its lockstep goes to `start`.
    pub fn host<B>(rt: &Handle, bind: B, setup: Setup, start: Starter) -> Lobby
    where
        B: Future<Output = Result<Transport, String>> + Send + 'static,
    {
        let shared: Sh = Arc::new(Mutex::new(Shared::new(true, start)));
        let (quit, q) = oneshot::channel();
        rt.spawn(run_host(bind, shared.clone(), setup, q));
        Lobby { shared, quit: Some(quit) }
    }

    /// Joins the lobby at `code` (a short code, ticket, bare endpoint ID or a link with one) from the endpoint
    /// `bind` makes; its lockstep goes to `start`.
    pub fn join<B>(rt: &Handle, bind: B, setup: Setup, code: &str, start: Starter) -> Lobby
    where
        B: Future<Output = Result<Transport, String>> + Send + 'static,
    {
        let shared: Sh = Arc::new(Mutex::new(Shared::new(false, start)));
        let (quit, q) = oneshot::channel();
        let code = codes::code_in(code).unwrap_or_else(|| code.trim().to_string());
        rt.spawn(run_guest(bind, shared.clone(), setup, code, q));
        Lobby { shared, quit: Some(quit) }
    }

    pub fn snapshot(&self) -> Snapshot {
        lock(&self.shared).snapshot()
    }

    /// Characters and teams for this machine's seats, in seat order. False if not open.
    pub fn picks(&self, picks: Vec<Pick>) -> bool {
        let mut s = lock(&self.shared);
        if s.host {
            s.host_cmd(|h| h.picks(&picks))
        } else {
            s.send_host(Msg::Picks(picks))
        }
    }

    /// Guest: ready (or not) for the line-up last seen. False for the host (always ready) or before seated.
    pub fn ready(&self, ready: bool) -> bool {
        let mut s = lock(&self.shared);
        let m = match &s.role {
            Some(Role::Guest(g)) => g.ready(ready),
            _ => None,
        };
        m.is_some_and(|m| s.send_host(m))
    }

    /// Host: the rules (clamped by [`Host::settings`]).
    pub fn settings(&self, board: u8, turns: u8, bonus: bool, teams: bool, handicaps: [u8; SEATS]) -> bool {
        lock(&self.shared).host_cmd(|h| h.settings(board, turns, bonus, teams, handicaps))
    }

    /// Host: the computer in seat `i` at `difficulty`, or the seat open again (None).
    pub fn cpu(&self, i: usize, difficulty: Option<u8>) -> bool {
        lock(&self.shared).host_cmd(|h| h.cpu(i, difficulty))
    }

    /// Host: starts the match with `seed` (0: one from the clock), and its lockstep. Err: why not.
    pub fn start(&self, seed: u64) -> Result<(), String> {
        let seed = if seed == 0 { clock_seed() } else { seed };
        let mut s = lock(&self.shared);
        if s.phase != Phase::Open {
            return Err("the lobby isn't open".into());
        }
        let delay = s.delay();
        let out = s.host().ok_or("only the host starts")?.start_with(seed, delay)?;
        s.dispatch(out);
        s.lockstep();
        Ok(())
    }

    /// This lobby started a lockstep (it reached the start) and its match is not over.
    pub fn lockstep_started(&self) -> bool {
        lock(&self.shared).lockstep_on
    }

    /// This machine's match is over: back in the open lobby ([`Shared::back`]). False unless started.
    pub fn back(&self) -> bool {
        lock(&self.shared).back()
    }

    /// Leaves: the host closes the lobby for everyone, a guest frees its seats. The tasks wind down on their own.
    pub fn leave(mut self) {
        if let Some(q) = self.quit.take() {
            let _ = q.send(());
        }
    }
}

impl Drop for Lobby {
    fn drop(&mut self) {
        if let Some(q) = self.quit.take() {
            let _ = q.send(());
        }
    }
}

fn clock_seed() -> u64 {
    let t = SystemTime::now().duration_since(SystemTime::UNIX_EPOCH).unwrap_or_default();
    (t.as_nanos() as u64) | 1
}

// ---------------------------------------------------------------- host

async fn run_host<B: Future<Output = Result<Transport, String>>>(bind: B, sh: Sh, setup: Setup, mut quit: oneshot::Receiver<()>) {
    let t = match bind.await {
        Ok(t) => Arc::new(t),
        Err(e) => return lock(&sh).close(&format!("Couldn't go online: {e}")),
    };
    t.ready().await;
    let id = setup.id.clone().unwrap_or_else(|| {
        let secs = SystemTime::now().duration_since(SystemTime::UNIX_EPOCH).unwrap_or_default().as_secs();
        format!("{}-{secs}", &t.id().to_string()[..8])
    });
    let host = match &setup.resume {
        None => Ok(Host::new(id, setup.ident(), &setup.players)),
        Some(path) => crate::save::Save::read(std::path::Path::new(path))
            .and_then(|sv| Host::resume(&sv.room, sv.checkpoint(), setup.ident(), &setup.players)),
    };
    let host = match host {
        Ok(h) => h,
        Err(e) => {
            lock(&sh).close(&format!("Couldn't continue the save: {e}"));
            return t.close().await;
        }
    };
    {
        let mut s = lock(&sh);
        s.role = Some(Role::Host(host));
        s.me = HOST.into();
        s.host_name = setup.players.first().map(|p| p.name.clone()).unwrap_or_default();
        s.lan = Some((t.clone(), Handle::current()));
        s.id_prefix = t.id().to_string()[..8].to_string();
        s.ticket = t.ticket();
        s.phase = Phase::Open;
        s.bump();
    }
    if setup.short {
        tokio::spawn(keep_code(t.clone(), Arc::downgrade(&sh)));
    }
    loop {
        tokio::select! {
            p = t.accept::<Msg>() => match p {
                Some(p) => drop(tokio::spawn(guest_conn(p, sh.clone()))),
                None => break,
            },
            _ = &mut quit => break,
        }
    }
    let leaving = {
        let mut s = lock(&sh);
        if s.phase != Phase::Started {
            for tx in s.peers.values() {
                let _ = tx.send(Msg::Leave);
            }
        }
        s.close("You closed the lobby.");
        !s.peers.is_empty()
    };
    if leaving {
        tokio::time::sleep(LEAVE_LINGER).await;
    }
    t.close().await;
}

/// The short code: rolled, then republished while the lobby is open (until the [`Lobby`] is gone).
async fn keep_code(t: Arc<Transport>, sh: std::sync::Weak<Mutex<Shared>>) {
    let mut code = None;
    loop {
        let r = t.publish_code(code.clone()).await;
        {
            let Some(sh) = sh.upgrade() else { return };
            let mut s = lock(&sh);
            if s.phase == Phase::Closed {
                return;
            }
            match r {
                Ok(c) => {
                    if s.short != c {
                        s.short = c.clone();
                        s.bump();
                    }
                    code = Some(c);
                }
                // the ticket still joins; the next round tries again
                Err(e) => {
                    s.error = format!("The short code isn't online ({e}); share the long code instead.");
                    s.bump();
                }
            }
        }
        tokio::time::sleep(if code.is_some() { codes::REPUBLISH } else { Duration::from_secs(10) }).await;
    }
}

/// One joiner's connection on the host: its messages into the `Host`, the `Host`'s to it; gone when it ends.
async fn guest_conn(mut p: Peer<Msg>, sh: Sh) {
    let peer = p.id().to_string();
    let (tx, mut outbox) = mpsc::unbounded_channel();
    let (dtx, mut dgrams) = mpsc::unbounded_channel::<Vec<u8>>();
    {
        let mut s = lock(&sh);
        if s.phase == Phase::Closed {
            p.close("closed");
            return;
        }
        s.peers.insert(peer.clone(), tx);
        s.dgrams.insert(peer.clone(), dtx);
    }
    let mut refused = None;
    let mut rtt = tokio::time::interval(RTT_EVERY);
    loop {
        tokio::select! {
            Some(d) = dgrams.recv() => p.send_datagram(d),
            _ = rtt.tick() => {
                if let Some(r) = p.rtt() {
                    lock(&sh).rtts.insert(peer.clone(), r.as_millis().min(u32::MAX as u128) as u32);
                }
            }
            m = p.recv_any() => match m {
                Incoming::Datagram(d) => lock(&sh).datagram_in(&peer, &d),
                Incoming::Msg(Ok(m)) => {
                    let mut s = lock(&sh);
                    let Some(h) = s.host() else { break };
                    let out = h.on(&peer, m);
                    s.dispatch(out);
                }
                Incoming::Msg(Err(_)) => break,
            },
            Some(m) = outbox.recv() => {
                if matches!(m, Msg::Refused(_)) {
                    refused = Some(tokio::time::Instant::now() + REFUSED_LINGER);
                }
                p.send(m);
            }
            _ = tokio::time::sleep_until(refused.unwrap_or_else(tokio::time::Instant::now)), if refused.is_some() => break,
        }
    }
    p.close("bye");
    let mut s = lock(&sh);
    s.peers.remove(&peer);
    s.dgrams.remove(&peer);
    s.rtts.remove(&peer);
    if let Some(h) = s.host() {
        let out = h.gone(&peer);
        if !out.is_empty() {
            s.dispatch(out);
        }
    }
}

// ---------------------------------------------------------------- guest

async fn run_guest<B: Future<Output = Result<Transport, String>>>(bind: B, sh: Sh, setup: Setup, code: String, mut quit: oneshot::Receiver<()>) {
    let t = match bind.await {
        Ok(t) => t,
        Err(e) => return lock(&sh).close(&format!("Couldn't go online: {e}")),
    };
    let me = t.id().to_string();
    {
        let mut s = lock(&sh);
        s.role = Some(Role::Guest(Guest::new(me.clone())));
        s.me = me;
        s.bump();
    }
    let mut p = tokio::select! {
        r = t.dial::<Msg>(&code) => match r {
            Ok(p) => p,
            Err(e) => {
                lock(&sh).close(&e);
                return t.close().await;
            }
        },
        _ = &mut quit => {
            lock(&sh).close("You left.");
            return t.close().await;
        }
    };
    let (tx, mut outbox) = mpsc::unbounded_channel();
    let (dtx, mut dgrams) = mpsc::unbounded_channel::<Vec<u8>>();
    {
        let mut s = lock(&sh);
        s.peers.insert(HOST.into(), tx);
        s.dgrams.insert(HOST.into(), dtx);
    }
    p.send(Guest::hello(setup.ident(), setup.players.clone()));
    loop {
        tokio::select! {
            Some(d) = dgrams.recv() => p.send_datagram(d),
            m = p.recv_any() => {
                let m = match m {
                    Incoming::Datagram(d) => {
                        lock(&sh).datagram_in(HOST, &d);
                        continue;
                    }
                    Incoming::Msg(m) => m,
                };
                let mut s = lock(&sh);
                let started = s.phase == Phase::Started;
                let ev = match (m, &mut s.role) {
                    (Ok(m), Some(Role::Guest(g))) => g.on(m),
                    (Ok(_), _) => None,
                    (Err(_), _) if started => {
                        // the match's connection is gone: N10's to recover; the room stays as it started
                        s.error = "Lost the connection to the host.".into();
                        s.bump();
                        break;
                    }
                    (Err(_), _) => {
                        s.close("Lost the connection to the host.");
                        break;
                    }
                };
                match ev {
                    Some(Event::Room) => {
                        if s.phase == Phase::Connecting {
                            s.phase = Phase::Open;
                        }
                        s.bump();
                    }
                    Some(Event::Started) => {
                        s.phase = Phase::Started;
                        s.lockstep();
                        s.bump();
                    }
                    Some(Event::Refused(why)) => {
                        s.close(&format!("The host refused: {why}"));
                        break;
                    }
                    Some(Event::HostLeft) => {
                        s.close("The host closed the lobby.");
                        break;
                    }
                    None => {}
                }
            }
            Some(m) = outbox.recv() => p.send(m),
            _ = &mut quit => {
                let started = {
                    let mut s = lock(&sh);
                    let started = s.phase == Phase::Started;
                    s.close("You left.");
                    started
                };
                if !started {
                    p.send(Msg::Leave);
                }
                tokio::time::sleep(LEAVE_LINGER).await;
                break;
            }
        }
    }
    {
        let mut s = lock(&sh);
        s.peers.clear();
        s.dgrams.clear();
    }
    p.close("bye");
    t.close().await;
}

// ---------------------------------------------------------------- LAN

/// What an open host lobby says about itself on the LAN (JSON in its mDNS record).
#[derive(Clone, Debug, PartialEq, Serialize, Deserialize)]
pub struct Listing {
    /// The room's match id.
    pub id: String,
    /// The host's first player's name.
    pub name: String,
    /// Seats nobody has taken (the computer's are taken).
    pub open: u8,
}

/// How much of the mDNS record's user data a listing may fill (iroh's `UserData::MAX_LENGTH`, less its tag).
const LISTING_MAX: usize = 245 - 3;

impl Listing {
    /// As JSON short enough for the record: the name shortened if need be; None if even an empty name won't fit.
    fn fit(mut self) -> Option<String> {
        loop {
            let j = serde_json::to_string(&self).ok()?;
            if j.len() <= LISTING_MAX {
                return Some(j);
            }
            if self.name.pop().is_none() {
                eprintln!("pbnet: LAN listing: match id too long to list");
                return None;
            }
        }
    }
}

/// A lobby heard on the LAN: its host's endpoint ID (a join code) and listing.
#[derive(Clone, Debug, PartialEq, Serialize)]
pub struct LanGame {
    pub code: String,
    #[serde(flatten)]
    pub listing: Listing,
}

/// What a [`Browse`] hears (`pbnet_lan_browse`).
#[derive(Clone, Debug, PartialEq, Serialize)]
pub struct LanList {
    /// `binding` (no endpoint yet), `open` (listening) or `closed` (it couldn't bind: `error` says why).
    pub state: &'static str,
    /// mDNS is on (false: this device can't hear the LAN, e.g. an iPhone without the multicast entitlement).
    pub lan: bool,
    pub error: String,
    /// In endpoint ID order. Hosts whose record isn't a listing are left out.
    pub games: Vec<LanGame>,
}

fn lan_games(hosts: Vec<LanHost>) -> Vec<LanGame> {
    hosts
        .into_iter()
        .filter_map(|h| Some(LanGame { code: h.id.to_string(), listing: serde_json::from_str(&h.listing).ok()? }))
        .collect()
}

type Slot = Arc<tokio::sync::Mutex<Option<Result<Transport, String>>>>;

/// An endpoint bound with no lobby, to hear the LAN's lobbies; a join takes it over ([`Browse::take`]), so the
/// host it heard is dialled from the endpoint that heard it. Dropped untaken, it closes the endpoint.
pub struct Browse {
    pub relay: Relay,
    rt: Handle,
    /// Locked while binding; None once taken.
    slot: Slot,
}

impl Browse {
    /// Binds the endpoint `bind` makes (relay setting `relay`) on `rt`.
    pub fn new<B>(rt: &Handle, relay: Relay, bind: B) -> Browse
    where
        B: Future<Output = Result<Transport, String>> + Send + 'static,
    {
        let slot: Slot = Arc::new(tokio::sync::Mutex::new(None));
        let mut binding = slot.clone().try_lock_owned().expect("a new mutex is free");
        rt.spawn(async move { *binding = Some(bind.await) });
        Browse { relay, rt: rt.clone(), slot }
    }

    /// The lobbies heard now. Never waits.
    pub fn list(&self) -> LanList {
        let (state, lan, error, games) = match self.slot.try_lock().as_deref() {
            Err(_) => ("binding", false, String::new(), vec![]),
            Ok(Some(Ok(t))) => ("open", t.has_lan(), String::new(), lan_games(t.lan())),
            Ok(Some(Err(e))) => ("closed", false, format!("Couldn't go online: {e}"), vec![]),
            Ok(None) => ("closed", false, "taken by a join".into(), vec![]),
        };
        LanList { state, lan, error, games }
    }

    /// The endpoint, once bound, for a lobby to use (None if it couldn't bind).
    pub fn take(mut self) -> impl Future<Output = Option<Transport>> + Send + 'static {
        // the drop below closes nothing: the endpoint goes with the future
        let slot = std::mem::replace(&mut self.slot, Arc::new(tokio::sync::Mutex::new(None)));
        async move { slot.lock().await.take().and_then(Result::ok) }
    }

    /// A lobby's endpoint: `browse`'s if there is one with `relay` and it bound, else a new one.
    pub fn or_bind(browse: Option<Browse>, relay: Relay) -> impl Future<Output = Result<Transport, String>> + Send + 'static {
        let browse = browse.filter(|b| b.relay == relay);
        let taken = browse.map(Browse::take);
        async move {
            if let Some(t) = taken {
                if let Some(t) = t.await {
                    return Ok(t);
                }
            }
            Transport::bind(&relay).await
        }
    }
}

impl Drop for Browse {
    fn drop(&mut self) {
        let slot = self.slot.clone();
        self.rt.spawn(async move {
            if let Some(Ok(t)) = slot.lock().await.take() {
                t.close().await;
            }
        });
    }
}

// ---------------------------------------------------------------- C ABI

/// The process's runtime for the lobby's tasks (two worker threads, made on first use).
fn rt() -> &'static Runtime {
    static RT: OnceLock<Runtime> = OnceLock::new();
    RT.get_or_init(|| {
        tokio::runtime::Builder::new_multi_thread()
            .worker_threads(2)
            .thread_name("pbnet")
            .enable_all()
            .build()
            .expect("pbnet: no tokio runtime")
    })
}

static LOBBY: Mutex<Option<Lobby>> = Mutex::new(None);
static BROWSE: Mutex<Option<Browse>> = Mutex::new(None);

fn global() -> std::sync::MutexGuard<'static, Option<Lobby>> {
    LOBBY.lock().unwrap_or_else(|e| e.into_inner())
}

/// The process lobby's room once its match started (what an online save records), else None.
pub fn started_room() -> Option<Room> {
    let g = global();
    let s = g.as_ref()?.snapshot();
    (s.state == Phase::Started).then_some(s.room).flatten()
}

fn browse() -> std::sync::MutexGuard<'static, Option<Browse>> {
    BROWSE.lock().unwrap_or_else(|e| e.into_inner())
}

/// A C string argument as UTF-8 (None: null or not UTF-8).
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

fn setup(json: &str) -> Option<(Setup, Relay)> {
    let s: Setup = serde_json::from_str(json).map_err(|e| eprintln!("pbnet: lobby setup: {e}")).ok()?;
    if s.players.is_empty() || s.players.len() > SEATS {
        eprintln!("pbnet: lobby setup: 1 to {SEATS} players");
        return None;
    }
    let relay = Relay::parse(&s.relay, &s.relay_url);
    Some((s, relay))
}

/// Hosts a lobby (replacing one that runs): 1 if it is starting, 0 if `setup` isn't a valid setup (see
/// `pbnet.h`).
///
/// # Safety
///
/// `setup` is null or a valid NUL-terminated UTF-8 string, read during the call only. Thread-safe.
#[no_mangle]
pub unsafe extern "C" fn pbnet_lobby_host(setup_json: *const c_char) -> i32 {
    // SAFETY: the caller's contract on `setup_json`.
    let Some((s, relay)) = (unsafe { arg(setup_json) }).and_then(setup) else { return 0 };
    // a host doesn't browse: its own endpoint is the one listed
    browse().take();
    let l = Lobby::host(rt().handle(), async move { Transport::bind(&relay).await }, s, global_starter());
    *global() = Some(l);
    1
}

/// Joins the lobby at `code` (replacing one that runs): 1 if it is dialling, 0 if `setup` isn't valid or `code`
/// is empty. It dials from the LAN browse's endpoint when one runs with the same relay setting (ending the browse).
///
/// # Safety
///
/// `setup` and `code` are null or valid NUL-terminated UTF-8 strings, read during the call only. Thread-safe.
#[no_mangle]
pub unsafe extern "C" fn pbnet_lobby_join(setup_json: *const c_char, code: *const c_char) -> i32 {
    // SAFETY: the caller's contract on both strings.
    let (s, code) = unsafe { (arg(setup_json), arg(code)) };
    let (Some((s, relay)), Some(code)) = (s.and_then(setup), code.filter(|c| !c.trim().is_empty())) else { return 0 };
    let bind = Browse::or_bind(browse().take(), relay);
    let l = Lobby::join(rt().handle(), bind, s, code, global_starter());
    *global() = Some(l);
    1
}

/// Leaves the lobby, if one runs (the host's closes for everyone), and ends the lockstep its start began. Safe to
/// call any time, from any thread.
#[no_mangle]
pub extern "C" fn pbnet_lobby_leave() {
    if let Some(l) = global().take() {
        if l.lockstep_started() {
            lockstep_c::pbnet_lockstep_stop();
        }
        l.leave();
    }
}

/// This machine's started match is over (call after `pbnet_lockstep_finish`): back to `PBNET_LOBBY_OPEN` with the
/// same people. The host reopens the room for everyone as a new match (new id, nobody ready, the seats and rules
/// kept); a guest takes the host's room after the match, which it may hear before or after this call. 1 if the lobby
/// was started; 0 otherwise (no lobby, not started). Safe to call any time, from any thread.
#[no_mangle]
pub extern "C" fn pbnet_lobby_back() -> i32 {
    global().as_ref().is_some_and(|l| l.back()) as i32
}

/// The lobby's `PBNET_LOBBY_*` state (`PBNET_LOBBY_NONE` with none). Safe to call any time, from any thread.
#[no_mangle]
pub extern "C" fn pbnet_lobby_state() -> i32 {
    global().as_ref().map_or(PBNET_LOBBY_NONE, |l| lock(&l.shared).phase.c())
}

/// Counts the lobby's changes (0 with none): redraw when it moves. Safe to call any time, from any thread.
#[no_mangle]
pub extern "C" fn pbnet_lobby_gen() -> u32 {
    global().as_ref().map_or(0, |l| lock(&l.shared).gen)
}

/// Copies `s` into `buf` as snprintf does: at most `cap - 1` bytes and a NUL; returns `s`'s length.
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

/// The lobby as JSON (`{"state":"none"}` with none), snprintf-style: writes at most `cap - 1` bytes and a NUL to
/// `buf`, returns the whole JSON's length (call again with a bigger buffer if it is `>= cap`).
///
/// # Safety
///
/// `buf` is null (only the length is returned) or points to `cap` writable bytes, used during the call only.
/// Thread-safe.
#[no_mangle]
pub unsafe extern "C" fn pbnet_lobby_json(buf: *mut c_char, cap: i32) -> i32 {
    let json = match global().as_ref() {
        Some(l) => serde_json::to_string(&l.snapshot()).unwrap_or_default(),
        None => r#"{"state":"none"}"#.to_string(),
    };
    // SAFETY: the caller's contract on `buf` and `cap`.
    unsafe { put(&json, buf, cap) }
}

/// This machine's picks for its seats in seat order: `characters[i]` and `teams[i]` for `n` seats. 1 if sent.
///
/// # Safety
///
/// `characters` and `teams` each point to `n` readable bytes (or are null with `n` 0), during the call only.
/// Thread-safe.
#[no_mangle]
pub unsafe extern "C" fn pbnet_lobby_picks(characters: *const u8, teams: *const u8, n: i32) -> i32 {
    let picks = if n <= 0 || characters.is_null() || teams.is_null() {
        vec![]
    } else {
        let n = (n as usize).min(SEATS);
        // SAFETY: the caller guarantees `n` readable bytes at each pointer.
        let (c, t) = unsafe { (std::slice::from_raw_parts(characters, n), std::slice::from_raw_parts(teams, n)) };
        c.iter().zip(t).map(|(&character, &team)| Pick { character, team }).collect()
    };
    global().as_ref().is_some_and(|l| l.picks(picks)) as i32
}

/// Guest: ready (nonzero) or not for the line-up it last saw. 1 if sent. Safe to call any time, from any thread.
#[no_mangle]
pub extern "C" fn pbnet_lobby_ready(ready: i32) -> i32 {
    global().as_ref().is_some_and(|l| l.ready(ready != 0)) as i32
}

/// Host: board, turns, bonus stars, team battle and the 4 seats' handicaps. 1 if applied.
///
/// # Safety
///
/// `handicaps` is null (all 0) or points to 4 readable bytes, during the call only. Thread-safe.
#[no_mangle]
pub unsafe extern "C" fn pbnet_lobby_settings(board: i32, turns: i32, bonus: i32, teams: i32, handicaps: *const u8) -> i32 {
    let mut h = [0u8; SEATS];
    if !handicaps.is_null() {
        // SAFETY: the caller guarantees 4 readable bytes.
        h.copy_from_slice(unsafe { std::slice::from_raw_parts(handicaps, SEATS) });
    }
    let b = board.clamp(0, 255) as u8;
    let t = turns.clamp(0, 255) as u8;
    global().as_ref().is_some_and(|l| l.settings(b, t, bonus != 0, teams != 0, h)) as i32
}

/// Host: the computer plays seat `seat` at `difficulty` (0..3), or the seat opens again (`difficulty` < 0). 1 if
/// applied. Safe to call any time, from any thread.
#[no_mangle]
pub extern "C" fn pbnet_lobby_cpu(seat: i32, difficulty: i32) -> i32 {
    if !(0..SEATS as i32).contains(&seat) {
        return 0;
    }
    let d = (difficulty >= 0).then(|| difficulty.min(255) as u8);
    global().as_ref().is_some_and(|l| l.cpu(seat as usize, d)) as i32
}

/// Host: starts the match with `seed` (0: one from the clock) once every guest is ready. 1 if started; else 0 and,
/// if `why` is not null, the reason snprintf'd into `why`/`cap`.
///
/// # Safety
///
/// `why` is null or points to `cap` writable bytes, during the call only. Thread-safe.
#[no_mangle]
pub unsafe extern "C" fn pbnet_lobby_start(seed: u64, why: *mut c_char, cap: i32) -> i32 {
    let r = match global().as_ref() {
        Some(l) => l.start(seed),
        None => Err("no lobby".into()),
    };
    match r {
        Ok(()) => 1,
        Err(e) => {
            // SAFETY: the caller's contract on `why` and `cap`.
            unsafe { put(&e, why, cap) };
            0
        }
    }
}

/// The board checkpoint of a started continue (the save's `SLSaveBoard` blob, `Room::checkpoint`): copies at most
/// `cap` bytes into `buf` and returns the blob's length; 0 when the lobby's started room continues no save (a new
/// match) or the blob is damaged.
///
/// # Safety
///
/// `buf` is null (only the length is returned) or points to `cap` writable bytes, used during the call only.
/// Thread-safe.
#[no_mangle]
pub unsafe extern "C" fn pbnet_lobby_checkpoint(buf: *mut u8, cap: i32) -> i32 {
    let Some(blob) = started_room().and_then(|r| r.checkpoint).and_then(|c| crate::save::unhex(&c.blob)) else { return 0 };
    if !buf.is_null() && cap > 0 {
        let n = blob.len().min(cap as usize);
        // SAFETY: the caller guarantees `cap` writable bytes at `buf`; n <= cap.
        unsafe { std::ptr::copy_nonoverlapping(blob.as_ptr(), buf, n) };
    }
    blob.len().min(i32::MAX as usize) as i32
}

/// The relay setting in a setup JSON (any other fields ignored).
#[derive(Deserialize)]
struct RelaySetup {
    #[serde(default)]
    relay: String,
    #[serde(default)]
    relay_url: String,
}

/// The lobbies heard on the LAN, as JSON snprintf'd into `buf`/`cap` (see `pbnet.h`); the first call (or one with
/// another relay setting) binds the browse endpoint. Returns the JSON's full length, or -1 if `setup` isn't JSON.
///
/// # Safety
///
/// `setup` is null or a valid NUL-terminated UTF-8 string, read during the call only; `buf` is null or points to
/// `cap` writable bytes, used during the call only. Thread-safe.
#[no_mangle]
pub unsafe extern "C" fn pbnet_lan_browse(setup_json: *const c_char, buf: *mut c_char, cap: i32) -> i32 {
    // SAFETY: the caller's contract on `setup_json`.
    let relay = match unsafe { arg(setup_json) } {
        None => Relay::parse("", ""),
        Some(j) => match serde_json::from_str::<RelaySetup>(j) {
            Ok(r) => Relay::parse(&r.relay, &r.relay_url),
            Err(e) => {
                eprintln!("pbnet: LAN browse setup: {e}");
                return -1;
            }
        },
    };
    let mut b = browse();
    if b.as_ref().is_none_or(|b| b.relay != relay) {
        let r = relay.clone();
        *b = Some(Browse::new(rt().handle(), relay, async move { Transport::bind(&r).await }));
    }
    let json = serde_json::to_string(&b.as_ref().map(Browse::list)).unwrap_or_default();
    // SAFETY: the caller's contract on `buf` and `cap`.
    unsafe { put(&json, buf, cap) }
}

/// Ends the LAN browse (closing its endpoint), if one runs. Safe to call any time, from any thread.
#[no_mangle]
pub extern "C" fn pbnet_lan_stop() {
    browse().take();
}

#[cfg(test)]
mod tests {
    use super::*;
    use n0_mainline::{Dht, Testnet};

    fn setup(name: &str, disc: &str) -> Setup {
        Setup {
            build: "0.1.0 test".into(),
            disc: disc.into(),
            players: vec![Player { name: name.into(), key: format!("key-{name}") }],
            relay: "off".into(),
            relay_url: String::new(),
            id: Some("m1".into()),
            short: false,
            resume: None,
        }
    }

    /// A starter that hands the lockstep to the test.
    fn catch() -> (Starter, std::sync::mpsc::Receiver<(Config, Wire)>) {
        let (tx, rx) = std::sync::mpsc::channel();
        let tx = Mutex::new(tx);
        (Arc::new(move |c, w| drop(tx.lock().unwrap().send((c, w)))), rx)
    }

    fn bind(net: &Testnet) -> impl Future<Output = Result<Transport, String>> + Send + 'static {
        let dht = Dht::builder().bootstrap(&net.bootstrap).build().unwrap();
        async move { Transport::bind_with(&Relay::Off, dht).await }
    }

    /// Waits (20 s at most) until `l`'s snapshot passes `f`.
    async fn until(l: &Lobby, what: &str, f: impl Fn(&Snapshot) -> bool) -> Snapshot {
        for _ in 0..1000 {
            let s = l.snapshot();
            if f(&s) {
                return s;
            }
            tokio::time::sleep(Duration::from_millis(20)).await;
        }
        panic!("{what}: never; last {:?}", l.snapshot());
    }

    /// Two real endpoints in-process (relays off, a local DHT testnet): the guest joins by the host's ticket, both
    /// pick, the guest gets ready, the host starts, and both hold the same started room. A third joiner with another
    /// disc is refused; a fourth's seat opens again when it leaves.
    #[test]
    fn two_endpoints_reach_start() {
        let rt = Runtime::new().unwrap();
        rt.block_on(async {
            let net = Testnet::new(3).await.unwrap();
            let h = Handle::current();
            let host = Lobby::host(&h, bind(&net), setup("h", "GMPE01-00"), catch().0);
            let s = until(&host, "host open", |s| s.state == Phase::Open && !s.code.is_empty()).await;
            assert!(s.host && s.me == HOST);
            assert_eq!(s.seats, [0]);
            assert!(!host.ready(true), "the host is always ready");

            let guest = Lobby::join(&h, bind(&net), setup("g", "GMPE01-00"), &s.code, catch().0);
            let g = until(&guest, "guest seated", |s| s.state == Phase::Open).await;
            assert_eq!(g.seats, [1]);
            assert!(!g.host && g.me != HOST);
            until(&host, "host sees guest", |s| s.room.as_ref().unwrap().peers().len() == 2).await;

            // a pick, a rule, the guest ready for what it saw: start
            assert!(host.settings(2, 25, false, false, [0, 1, 0, 0]));
            assert!(guest.picks(vec![Pick { character: 6, team: 1 }]));
            until(&guest, "guest sees settings and pick", |s| {
                let r = s.room.as_ref().unwrap();
                r.board == 2 && r.seats[1].as_ref().unwrap().character == 6
            })
            .await;
            assert!(host.start(9).is_err(), "guest not ready");
            assert!(guest.ready(true));
            until(&host, "guest ready", |s| s.room.as_ref().unwrap().all_ready()).await;

            // another disc: refused, closed with why
            let pal = Lobby::join(&h, bind(&net), setup("p", "GMPP01-00"), &s.code, catch().0);
            let p = until(&pal, "pal refused", |s| s.state == Phase::Closed).await;
            assert!(p.error.contains("different disc"), "{}", p.error);

            // a fourth joins (ready cleared), then leaves: its seat is open again
            let late = Lobby::join(&h, bind(&net), setup("l", "GMPE01-00"), &s.code, catch().0);
            until(&late, "late seated", |s| s.state == Phase::Open).await;
            until(&host, "late in", |s| s.room.as_ref().unwrap().seats[2].is_some()).await;
            late.leave();
            until(&host, "late gone", |s| s.room.as_ref().unwrap().seats[2].is_none()).await;
            let ver = until(&guest, "guest sees late gone", |s| s.room.as_ref().unwrap().seats[2].is_none()).await.room.unwrap().ver;
            until(&host, "same ver", |s| s.room.as_ref().unwrap().ver == ver).await;
            assert!(guest.ready(true));
            until(&host, "guest ready again", |s| s.room.as_ref().unwrap().all_ready()).await;

            host.start(9).unwrap();
            let hs = host.snapshot();
            assert_eq!(hs.state, Phase::Started);
            let gs = until(&guest, "guest started", |s| s.state == Phase::Started).await;
            let room = hs.room.unwrap();
            assert!(room.started && room.seed == 9);
            assert_eq!(gs.room.as_ref(), Some(&room), "guest holds the host's room");
            assert_eq!(room.seats[1].as_ref().unwrap().character, 6);
            assert_eq!((room.board, room.turns, room.bonus), (2, 25, false));
            assert!(room.seats[2].as_ref().unwrap().cpu && room.seats[3].as_ref().unwrap().cpu);
            assert!(!host.settings(3, 10, true, false, [0; SEATS]), "nothing after the start");
            host.leave();
            guest.leave();
        });
    }

    /// Continue (N9) over real endpoints: a save written after a match of "h" and "g" is hosted by "g" this time;
    /// "h" joins and gets its seat back by key; both hold the same started room with the save's checkpoint. A save
    /// that can't be read closes the host's lobby with why.
    #[test]
    fn continue_a_save_on_two_endpoints() {
        let dir = std::env::temp_dir().join(format!("pbnet-continue-{}", std::process::id()));
        let _ = std::fs::remove_dir_all(&dir);
        let ident = Ident { build: "0.1.0 test".into(), disc: "GMPE01-00".into() };
        let pl = |n: &str| Player { name: n.into(), key: format!("key-{n}") };
        let mut played = Host::new("m1".into(), ident, &[pl("h")]);
        played.on("g-old", Msg::Hello { ident: Ident { build: "0.1.0 test".into(), disc: "GMPE01-00".into() }, players: vec![pl("g")] });
        played.on("g-old", Msg::Ready { ver: played.room().ver, ready: true });
        played.start(5).unwrap();
        let blob = vec![0x11u8; 0x19C];
        let save = crate::save::Save::new(played.room(), &blob, 600, Default::default()).unwrap();
        let path = save.write(&dir).unwrap();

        let rt = Runtime::new().unwrap();
        rt.block_on(async {
            let net = Testnet::new(3).await.unwrap();
            let h = Handle::current();
            let mut gs = setup("g", "GMPE01-00");
            gs.resume = Some(path.to_string_lossy().into_owned());
            let host = Lobby::host(&h, bind(&net), gs, catch().0);
            let s = until(&host, "host open", |s| s.state == Phase::Open && !s.code.is_empty()).await;
            assert_eq!(s.seats, [1], "g's seat by its key");
            assert!(s.room.as_ref().unwrap().resuming());

            let guest = Lobby::join(&h, bind(&net), setup("h", "GMPE01-00"), &s.code, catch().0);
            let g = until(&guest, "h seated", |s| s.state == Phase::Open).await;
            assert_eq!(g.seats, [0]);
            assert!(guest.ready(true));
            until(&host, "h ready", |s| s.room.as_ref().unwrap().all_ready()).await;
            host.start(8).unwrap();
            let gs = until(&guest, "guest started", |s| s.state == Phase::Started).await;
            let room = host.snapshot().room.unwrap();
            assert_eq!(gs.room.as_ref(), Some(&room));
            let c = room.checkpoint.as_ref().unwrap();
            assert_eq!(crate::save::unhex(&c.blob).unwrap(), blob);
            assert_eq!((room.id.as_str(), room.seed), ("m1", 8));
            host.leave();
            guest.leave();

            let mut bad = setup("g", "GMPE01-00");
            bad.resume = Some(dir.join("online").join("nope.json").to_string_lossy().into_owned());
            let l = Lobby::host(&h, bind(&net), bad, catch().0);
            let b = until(&l, "bad save", |s| s.state == Phase::Closed).await;
            assert!(b.error.starts_with("Couldn't continue the save"), "{}", b.error);
        });
        std::fs::remove_dir_all(&dir).unwrap();
    }

    /// The host leaving closes the guest's lobby with words; a bad code closes the joiner's.
    #[test]
    fn host_leaving_closes_guest() {
        let rt = Runtime::new().unwrap();
        rt.block_on(async {
            let net = Testnet::new(3).await.unwrap();
            let h = Handle::current();
            let host = Lobby::host(&h, bind(&net), setup("h", "GMPE01-00"), catch().0);
            let s = until(&host, "host open", |s| s.state == Phase::Open).await;
            let guest = Lobby::join(&h, bind(&net), setup("g", "GMPE01-00"), &s.code, catch().0);
            until(&guest, "guest seated", |s| s.state == Phase::Open).await;
            host.leave();
            let g = until(&guest, "guest closed", |s| s.state == Phase::Closed).await;
            assert_eq!(g.error, "The host closed the lobby.");
            assert!(!guest.ready(true));

            let bad = Lobby::join(&h, bind(&net), setup("b", "GMPE01-00"), "not a code", catch().0);
            let b = until(&bad, "bad code", |s| s.state == Phase::Closed).await;
            assert!(b.error.contains("isn't a lobby code"), "{}", b.error);
        });
    }

    /// Three real endpoints start a match and run its lockstep on the lobby's connections: every seat's pads reach
    /// every peer (guest to guest through the host), all three run the same frames on the same pads, with the
    /// host's delay, and the hashes they swap agree.
    #[test]
    fn started_lobby_runs_the_lockstep() {
        use crate::lockstep::{Pad, Session, Step, IDLE};
        let rt = Runtime::new().unwrap();
        rt.block_on(async {
            let net = Testnet::new(3).await.unwrap();
            let h = Handle::current();
            let (hs, hrx) = catch();
            let host = Lobby::host(&h, bind(&net), setup("h", "GMPE01-00"), hs);
            let s = until(&host, "host open", |s| s.state == Phase::Open && !s.code.is_empty()).await;
            let (as_, arx) = catch();
            let a = Lobby::join(&h, bind(&net), setup("a", "GMPE01-00"), &s.code, as_);
            until(&a, "a seated", |s| s.state == Phase::Open).await;
            let (bs, brx) = catch();
            let b = Lobby::join(&h, bind(&net), setup("b", "GMPE01-00"), &s.code, bs);
            until(&b, "b seated", |s| s.state == Phase::Open).await;
            until(&host, "both in", |s| s.room.as_ref().unwrap().peers().len() == 3).await;
            for g in [&a, &b] {
                until(g, "sees three", |s| s.room.as_ref().unwrap().peers().len() == 3).await;
                assert!(g.ready(true));
            }
            until(&host, "all ready", |s| s.room.as_ref().unwrap().all_ready()).await;
            assert!(!host.lockstep_started());
            host.start(7).unwrap();
            until(&a, "a started", |s| s.state == Phase::Started).await;
            until(&b, "b started", |s| s.state == Phase::Started).await;

            let got = |rx: &std::sync::mpsc::Receiver<(Config, Wire)>| rx.recv_timeout(Duration::from_secs(1)).unwrap();
            let lineup: Vec<_> = [got(&hrx), got(&arx), got(&brx)].into_iter().collect();
            let room = host.snapshot().room.unwrap();
            assert!(room.delay >= 2 && room.delay <= 15, "{}", room.delay);
            for (i, (c, _)) in lineup.iter().enumerate() {
                assert_eq!((c.me, c.peers, c.delay), (i as u8, 3, room.delay));
                assert_eq!(c.seats, [0, 1, 2, NO_PEER], "seat 4 is the computer's");
            }
            assert!(hrx.try_recv().is_err(), "started once");
            let mut peers: Vec<Session<Wire>> = lineup.into_iter().map(|(c, w)| Session::new(c, w)).collect();

            const FRAMES: u32 = 90;
            let pad = |p: usize, f: u32| Pad { button: 1 << p, stick_x: (f % 100) as i8, stick_y: p as i8, ..IDLE };
            let mut runs: Vec<Vec<[Pad; 4]>> = vec![vec![]; 3];
            let t0 = std::time::Instant::now();
            while runs.iter().any(|r| (r.len() as u32) < FRAMES) {
                assert!(t0.elapsed() < Duration::from_secs(20), "stuck at {:?}", runs.iter().map(Vec::len).collect::<Vec<_>>());
                for (p, sess) in peers.iter_mut().enumerate() {
                    if runs[p].len() as u32 >= FRAMES {
                        // keep sending (acks) so the others finish
                        sess.step(t0.elapsed().as_millis() as u64, &[]);
                        continue;
                    }
                    let f = sess.frame();
                    if f % lockstep::HASH_EVERY == 0 {
                        sess.set_hash(f, 0xabc + f as u64);
                    }
                    if let Step::Ready(pads) = sess.step(t0.elapsed().as_millis() as u64, &[pad(p, f + sess.delay())]) {
                        runs[p].push(pads);
                    }
                }
                tokio::time::sleep(Duration::from_millis(4)).await;
            }
            for p in 0..3 {
                assert_eq!(runs[p][..FRAMES as usize], runs[0][..FRAMES as usize], "peer {p} ran other pads");
                assert!(peers[p].desync().is_none());
            }
            let d = room.delay as usize;
            for (f, ran) in runs[0].iter().enumerate().take(FRAMES as usize) {
                let want = if f < d { [IDLE; 4] } else { [pad(0, f as u32), pad(1, f as u32), pad(2, f as u32), IDLE] };
                assert_eq!(*ran, want, "frame {f}");
            }

            // the match is over: b is back first, then the host (reopens for all), then a; then the next match
            assert!(b.back());
            let bs = b.snapshot();
            assert_eq!(bs.state, Phase::Open);
            assert!(!bs.room.as_ref().unwrap().started);
            assert!(host.back());
            assert!(!host.back(), "back once");
            let hs = host.snapshot();
            let open = hs.room.unwrap();
            assert_eq!(hs.state, Phase::Open);
            assert!(!open.started && open.seed == 0 && open.id != room.id && open.ver == room.ver + 1);
            assert_eq!(open.seats, room.seats);
            assert!(!host.lockstep_started());
            until(&b, "b hears the reopened room", |s| s.room.as_ref().is_some_and(|r| r.id == open.id)).await;
            // a still plays: it keeps the started room until it is back
            tokio::time::sleep(Duration::from_millis(100)).await;
            assert_eq!(a.snapshot().state, Phase::Started);
            assert_eq!(a.snapshot().room.unwrap().id, room.id);
            assert!(a.back());
            assert_eq!(a.snapshot().room.unwrap().id, open.id, "the host's room heard while playing");
            assert!(host.start(8).is_err(), "nobody ready after the match");
            for g in [&a, &b] {
                assert!(g.ready(true));
            }
            until(&host, "all ready again", |s| s.room.as_ref().unwrap().all_ready()).await;
            host.start(8).unwrap();
            until(&a, "a started again", |s| s.state == Phase::Started).await;
            until(&b, "b started again", |s| s.state == Phase::Started).await;
            for rx in [&hrx, &arx, &brx] {
                assert_eq!(got(rx).0.peers, 3, "a second lockstep");
            }
            assert!(host.lockstep_started());
            host.leave();
            a.leave();
            b.leave();
        });
    }

    #[test]
    fn delay_from_the_slowest_two_guests() {
        assert_eq!(delay_of(&[]), delay_for(0));
        assert_eq!(delay_of(&[150]), delay_for(150));
        // guest to guest is through the host: the two slowest legs
        assert_eq!(delay_of(&[40, 150, 100]), delay_for(250));
    }

    #[test]
    fn listing_fits_the_mdns_record() {
        let l = Listing { id: "m1".into(), name: "Ryan".into(), open: 3 };
        assert_eq!(l.clone().fit().unwrap(), r#"{"id":"m1","name":"Ryan","open":3}"#);
        let long = Listing { name: "é".repeat(300), ..l.clone() }.fit().unwrap();
        assert!(long.len() <= LISTING_MAX && long.contains("éé"), "{long}");
        assert_eq!(Listing { id: "x".repeat(300), ..l.clone() }.fit(), None);
        // heard back: only real listings, by their endpoint
        let id = iroh::SecretKey::generate().public();
        let games = lan_games(vec![LanHost { id, listing: l.clone().fit().unwrap() }, LanHost { id, listing: "not json".into() }]);
        assert_eq!(games, [LanGame { code: id.to_string(), listing: l }]);
        assert_eq!(serde_json::to_string(&games[0]).unwrap(), format!(r#"{{"code":"{id}","id":"m1","name":"Ryan","open":3}}"#));
    }

    #[test]
    fn browse_states() {
        let rt = Runtime::new().unwrap();
        let binding = Browse::new(rt.handle(), Relay::Off, std::future::pending());
        let failed = Browse::new(rt.handle(), Relay::Off, async { Err("no network".to_string()) });
        rt.block_on(async { tokio::time::sleep(Duration::from_millis(50)).await });
        assert_eq!(binding.list(), LanList { state: "binding", lan: false, error: String::new(), games: vec![] });
        let f = failed.list();
        assert_eq!((f.state, f.error.as_str()), ("closed", "Couldn't go online: no network"));
        assert!(rt.block_on(failed.take()).is_none());
        assert_eq!(serde_json::to_string(&binding.list()).unwrap(), r#"{"state":"binding","lan":false,"error":"","games":[]}"#);
    }

    /// A join takes the browse's endpoint over: the guest is seated as that endpoint.
    #[test]
    fn join_dials_from_the_browse_endpoint() {
        let rt = Runtime::new().unwrap();
        rt.block_on(async {
            let net = Testnet::new(3).await.unwrap();
            let h = Handle::current();
            let host = Lobby::host(&h, bind(&net), setup("h", "GMPE01-00"), catch().0);
            let s = until(&host, "host open", |s| s.state == Phase::Open && !s.code.is_empty()).await;
            let b = Browse::new(&h, Relay::Off, bind(&net));
            for _ in 0..500 {
                if b.list().state == "open" {
                    break;
                }
                tokio::time::sleep(Duration::from_millis(10)).await;
            }
            assert_eq!(b.list().state, "open");
            let id = b.slot.lock().await.as_ref().unwrap().as_ref().unwrap().id().to_string();
            let guest = Lobby::join(&h, Browse::or_bind(Some(b), Relay::Off), setup("g", "GMPE01-00"), &s.code, catch().0);
            let g = until(&guest, "guest seated", |s| s.state == Phase::Open).await;
            assert_eq!(g.me, id);
            host.leave();
            guest.leave();
        });
    }

    /// On the LAN by mDNS: an open host is heard with its listing, joined by the code heard, its open seats follow
    /// the room and it is gone at the start. Needs multicast (as `transport::lan_listing_heard_by_mdns`): run with
    /// `cargo test -- --ignored`.
    #[test]
    #[ignore]
    fn lan_lobby_heard_joined_and_unlisted() {
        let rt = Runtime::new().unwrap();
        rt.block_on(async {
            let net = Testnet::new(3).await.unwrap();
            let h = Handle::current();
            let host = Lobby::host(&h, bind(&net), setup("h", "GMPE01-00"), catch().0);
            until(&host, "host open", |s| s.state == Phase::Open).await;
            let heard = |b: &Browse, f: &dyn Fn(&[LanGame]) -> bool| {
                let games = b.list().games;
                f(&games).then_some(games)
            };
            let wait = async |b: &Browse, what: &str, f: &dyn Fn(&[LanGame]) -> bool| {
                for _ in 0..300 {
                    if let Some(g) = heard(b, f) {
                        return g;
                    }
                    tokio::time::sleep(Duration::from_millis(50)).await;
                }
                panic!("{what}: never; last {:?}", b.list());
            };
            let b = Browse::new(&h, Relay::Off, bind(&net));
            let games = wait(&b, "host heard", &|g| g.iter().any(|g| g.listing.id == "m1")).await;
            let game = games.into_iter().find(|g| g.listing.id == "m1").unwrap();
            assert_eq!((game.listing.name.as_str(), game.listing.open), ("h", 3));
            assert!(b.list().lan);

            let guest = Lobby::join(&h, Browse::or_bind(Some(b), Relay::Off), setup("g", "GMPE01-00"), &game.code, catch().0);
            until(&guest, "guest seated", |s| s.state == Phase::Open).await;
            let b2 = Browse::new(&h, Relay::Off, bind(&net));
            wait(&b2, "one seat fewer", &|g| g.iter().any(|g| g.listing.id == "m1" && g.listing.open == 2)).await;
            assert!(host.cpu(3, Some(1)));
            wait(&b2, "cpu seat taken", &|g| g.iter().any(|g| g.listing.id == "m1" && g.listing.open == 1)).await;

            assert!(guest.ready(true));
            until(&host, "guest ready", |s| s.room.as_ref().unwrap().all_ready()).await;
            host.start(3).unwrap();
            wait(&b2, "unlisted at the start", &|g| !g.iter().any(|g| g.listing.id == "m1")).await;
            host.leave();
            guest.leave();
        });
    }

    /// The C ABI with no lobby, and bad setups refused (the one test touching the global lobby).
    #[test]
    fn c_abi_without_lobby() {
        pbnet_lobby_leave();
        assert_eq!(pbnet_lobby_state(), PBNET_LOBBY_NONE);
        assert_eq!(pbnet_lobby_gen(), 0);
        let mut b = [0 as c_char; 8];
        let n = unsafe { pbnet_lobby_json(b.as_mut_ptr(), b.len() as i32) };
        assert_eq!(n as usize, r#"{"state":"none"}"#.len());
        assert_eq!(unsafe { CStr::from_ptr(b.as_ptr()) }.to_str().unwrap(), r#"{"state"#);
        assert_eq!(unsafe { pbnet_lobby_json(std::ptr::null_mut(), 0) }, n);
        assert_eq!(pbnet_lobby_ready(1), 0);
        assert_eq!(pbnet_lobby_cpu(9, 1), 0);
        assert_eq!(unsafe { pbnet_lobby_picks([1u8].as_ptr(), [0u8].as_ptr(), 1) }, 0);
        assert_eq!(unsafe { pbnet_lobby_settings(0, 20, 1, 0, std::ptr::null()) }, 0);
        let mut why = [0 as c_char; 64];
        assert_eq!(unsafe { pbnet_lobby_start(1, why.as_mut_ptr(), 64) }, 0);
        assert_eq!(unsafe { CStr::from_ptr(why.as_ptr()) }.to_str().unwrap(), "no lobby");
        assert_eq!(unsafe { pbnet_lobby_host(std::ptr::null()) }, 0);
        assert_eq!(unsafe { pbnet_lobby_host(c"{\"build\":\"b\",\"disc\":\"d\",\"players\":[]}".as_ptr()) }, 0);
        assert_eq!(unsafe { pbnet_lobby_join(c"{}".as_ptr(), c"ABCD".as_ptr()) }, 0);
        assert_eq!(pbnet_lobby_state(), PBNET_LOBBY_NONE);
        assert!(started_room().is_none());
        assert_eq!(unsafe { pbnet_lobby_checkpoint(std::ptr::null_mut(), 0) }, 0);
        // a bad browse setup binds nothing
        assert_eq!(unsafe { pbnet_lan_browse(c"not json".as_ptr(), std::ptr::null_mut(), 0) }, -1);
        assert!(browse().is_none());
        pbnet_lan_stop();
    }

    #[test]
    fn setup_json_defaults() {
        let (s, r) = super::setup(r#"{"build":"b","disc":"d","players":[{"name":"n","key":"k"}]}"#).unwrap();
        assert!(s.short && s.id.is_none());
        assert_eq!(r, Relay::N0);
        let (_, r) = super::setup(r#"{"build":"b","disc":"d","players":[{"name":"n","key":"k"}],"relay":"off"}"#).unwrap();
        assert_eq!(r, Relay::Off);
    }

    /// The started lobby's JSON as `pbnet_lobby_json` gives it: the C reader (netplay_match_from_lobby,
    /// src/port/netplay.c) is tested against this same file by tools/netplay/tick_gate_test.sh.
    #[test]
    fn started_json_fixture() {
        let s = Setup { players: vec![Player { name: "A \"quoted\" name".into(), key: "k".into() }], ..setup("h", "d") };
        let mut h = Host::new("m1".into(), s.ident(), &s.players);
        h.settings(3, 25, false, true, [0, 2, 9, 1]);
        h.cpu(2, Some(3));
        h.start_with(0xFEDC_BA98_7654_3210, 4).unwrap();
        let snap = Snapshot {
            state: Phase::Started,
            host: true,
            me: HOST.into(),
            code: "ticket".into(),
            short: String::new(),
            error: String::new(),
            seats: h.room().seats_of(HOST),
            room: Some(h.room().clone()),
            gen: 9,
        };
        let json = serde_json::to_string(&snap).unwrap();
        assert_eq!(json, include_str!("../../tools/netplay/lobby_started.json").trim_end(), "regenerate the fixture: {json}");
    }
}
