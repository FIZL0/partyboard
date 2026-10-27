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
//! After the start the connections stay open (N8 runs the lockstep on them; a drop then is N10's).

use std::collections::BTreeMap;
use std::ffi::{c_char, CStr};
use std::future::Future;
use std::sync::{Arc, Mutex, OnceLock};
use std::time::{Duration, SystemTime};

use serde::{Deserialize, Serialize};
use tokio::runtime::{Handle, Runtime};
use tokio::sync::{mpsc, oneshot};

use crate::codes;
use crate::lobby::{Event, Guest, Host, Ident, Msg, Pick, Player, Room, To, HOST, SEATS};
use crate::transport::{Peer, Relay, Transport};

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
    gen: u32,
}

impl Shared {
    fn new(host: bool) -> Shared {
        Shared {
            phase: Phase::Connecting,
            host,
            role: None,
            me: String::new(),
            ticket: String::new(),
            short: String::new(),
            error: String::new(),
            peers: BTreeMap::new(),
            gen: 0,
        }
    }

    fn bump(&mut self) {
        self.gen = self.gen.wrapping_add(1);
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
    /// Hosts a lobby on the endpoint `bind` makes, its tasks on `rt`.
    pub fn host<B>(rt: &Handle, bind: B, setup: Setup) -> Lobby
    where
        B: Future<Output = Result<Transport, String>> + Send + 'static,
    {
        let shared: Sh = Arc::new(Mutex::new(Shared::new(true)));
        let (quit, q) = oneshot::channel();
        rt.spawn(run_host(bind, shared.clone(), setup, q));
        Lobby { shared, quit: Some(quit) }
    }

    /// Joins the lobby at `code` (a short code, ticket, bare endpoint ID or a link with one) from the endpoint
    /// `bind` makes.
    pub fn join<B>(rt: &Handle, bind: B, setup: Setup, code: &str) -> Lobby
    where
        B: Future<Output = Result<Transport, String>> + Send + 'static,
    {
        let shared: Sh = Arc::new(Mutex::new(Shared::new(false)));
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

    /// Host: starts the match with `seed` (0: one from the clock). Err: why not.
    pub fn start(&self, seed: u64) -> Result<(), String> {
        let seed = if seed == 0 { clock_seed() } else { seed };
        let mut s = lock(&self.shared);
        if s.phase != Phase::Open {
            return Err("the lobby isn't open".into());
        }
        let out = s.host().ok_or("only the host starts")?.start(seed)?;
        s.dispatch(out);
        Ok(())
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
    {
        let mut s = lock(&sh);
        let id = setup.id.clone().unwrap_or_else(|| {
            let secs = SystemTime::now().duration_since(SystemTime::UNIX_EPOCH).unwrap_or_default().as_secs();
            format!("{}-{secs}", &t.id().to_string()[..8])
        });
        s.role = Some(Role::Host(Host::new(id, setup.ident(), &setup.players)));
        s.me = HOST.into();
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
    {
        let mut s = lock(&sh);
        if s.phase == Phase::Closed {
            p.close("closed");
            return;
        }
        s.peers.insert(peer.clone(), tx);
    }
    let mut refused = None;
    loop {
        tokio::select! {
            m = p.recv() => match m {
                Ok(m) => {
                    let mut s = lock(&sh);
                    let Some(h) = s.host() else { break };
                    let out = h.on(&peer, m);
                    s.dispatch(out);
                }
                Err(_) => break,
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
    lock(&sh).peers.insert(HOST.into(), tx);
    p.send(Guest::hello(setup.ident(), setup.players.clone()));
    loop {
        tokio::select! {
            m = p.recv() => {
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
    lock(&sh).peers.clear();
    p.close("bye");
    t.close().await;
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

fn global() -> std::sync::MutexGuard<'static, Option<Lobby>> {
    LOBBY.lock().unwrap_or_else(|e| e.into_inner())
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
    let l = Lobby::host(rt().handle(), async move { Transport::bind(&relay).await }, s);
    *global() = Some(l);
    1
}

/// Joins the lobby at `code` (replacing one that runs): 1 if it is dialling, 0 if `setup` isn't valid or `code`
/// is empty.
///
/// # Safety
///
/// `setup` and `code` are null or valid NUL-terminated UTF-8 strings, read during the call only. Thread-safe.
#[no_mangle]
pub unsafe extern "C" fn pbnet_lobby_join(setup_json: *const c_char, code: *const c_char) -> i32 {
    // SAFETY: the caller's contract on both strings.
    let (s, code) = unsafe { (arg(setup_json), arg(code)) };
    let (Some((s, relay)), Some(code)) = (s.and_then(setup), code.filter(|c| !c.trim().is_empty())) else { return 0 };
    let l = Lobby::join(rt().handle(), async move { Transport::bind(&relay).await }, s, code);
    *global() = Some(l);
    1
}

/// Leaves the lobby, if one runs (the host's closes for everyone). Safe to call any time, from any thread.
#[no_mangle]
pub extern "C" fn pbnet_lobby_leave() {
    if let Some(l) = global().take() {
        l.leave();
    }
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
        }
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
            let host = Lobby::host(&h, bind(&net), setup("h", "GMPE01-00"));
            let s = until(&host, "host open", |s| s.state == Phase::Open && !s.code.is_empty()).await;
            assert!(s.host && s.me == HOST);
            assert_eq!(s.seats, [0]);
            assert!(!host.ready(true), "the host is always ready");

            let guest = Lobby::join(&h, bind(&net), setup("g", "GMPE01-00"), &s.code);
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
            let pal = Lobby::join(&h, bind(&net), setup("p", "GMPP01-00"), &s.code);
            let p = until(&pal, "pal refused", |s| s.state == Phase::Closed).await;
            assert!(p.error.contains("different disc"), "{}", p.error);

            // a fourth joins (ready cleared), then leaves: its seat is open again
            let late = Lobby::join(&h, bind(&net), setup("l", "GMPE01-00"), &s.code);
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

    /// The host leaving closes the guest's lobby with words; a bad code closes the joiner's.
    #[test]
    fn host_leaving_closes_guest() {
        let rt = Runtime::new().unwrap();
        rt.block_on(async {
            let net = Testnet::new(3).await.unwrap();
            let h = Handle::current();
            let host = Lobby::host(&h, bind(&net), setup("h", "GMPE01-00"));
            let s = until(&host, "host open", |s| s.state == Phase::Open).await;
            let guest = Lobby::join(&h, bind(&net), setup("g", "GMPE01-00"), &s.code);
            until(&guest, "guest seated", |s| s.state == Phase::Open).await;
            host.leave();
            let g = until(&guest, "guest closed", |s| s.state == Phase::Closed).await;
            assert_eq!(g.error, "The host closed the lobby.");
            assert!(!guest.ready(true));

            let bad = Lobby::join(&h, bind(&net), setup("b", "GMPE01-00"), "not a code");
            let b = until(&bad, "bad code", |s| s.state == Phase::Closed).await;
            assert!(b.error.contains("isn't a lobby code"), "{}", b.error);
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
    }

    #[test]
    fn setup_json_defaults() {
        let (s, r) = super::setup(r#"{"build":"b","disc":"d","players":[{"name":"n","key":"k"}]}"#).unwrap();
        assert!(s.short && s.id.is_none());
        assert_eq!(r, Relay::N0);
        let (_, r) = super::setup(r#"{"build":"b","disc":"d","players":[{"name":"n","key":"k"}],"relay":"off"}"#).unwrap();
        assert_eq!(r, Relay::Off);
    }
}
