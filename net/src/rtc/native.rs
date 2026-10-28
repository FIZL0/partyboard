//! N96: the page's peer connections (`super::js`) on a native build, so a desktop or phone lobby meets browsers:
//! str0m (a sans-IO WebRTC stack) on this machine's UDP sockets, after opentennis's `hst-base/src/net/rtc/native.rs`.
//! Same calls and meaning as `src/port/web/pbnet_rtc.js`: a connection by number, its description polled once
//! gathered, its two negotiated channels (0 reliable and ordered, 1 unordered with no retransmits) queued for
//! polling; nothing calls back.
//!
//! - Each connection: a socket per IPv4 address of this machine (its host candidates), a reader thread per socket
//!   and a driver thread for its `Rtc` (timers, packets in). Plain threads, so it runs under any runtime (the
//!   desktop's tokio threads, a test's current-thread runtime).
//! - With STUN servers in the ICE JSON (the relay setting `n0` or an own `stun:` URL), each socket asks them for its
//!   public address first (server-reflexive candidates, up to [`GATHER`]), as a browser gathers before its
//!   description is final. No TURN.
//! - A browser's own candidates are mDNS names we can't resolve: they are left out, and its ICE checks reach our host
//!   candidates anyway (str0m learns its address from them, peer-reflexive).
//! - DTLS roles as `Sig::sdp` has them: an answer says `active`, so we answer as the DTLS client (str0m picks that for
//!   a `passive` remote), and an answer to our offer makes us the server.
//! - The round trip: our ICE checks' STUN transactions, timed to their responses.

use std::collections::{BTreeMap, VecDeque};
use std::ffi::{c_char, CStr};
use std::hash::{BuildHasher, Hasher};
use std::net::{IpAddr, SocketAddr, ToSocketAddrs, UdpSocket};
use std::sync::atomic::{AtomicBool, Ordering};
use std::sync::mpsc;
use std::sync::{Arc, Mutex};
use std::time::{Duration, Instant};

use str0m::change::{SdpAnswer, SdpOffer, SdpPendingOffer};
use str0m::channel::{ChannelConfig, ChannelId, Reliability};
use str0m::net::{Protocol, Receive};
use str0m::{Candidate, Event, IceConnectionState, Input, Output, Rtc};

/// How long STUN servers have to tell a socket its public address.
const GATHER: Duration = Duration::from_millis(2000);
/// The driver wakes at least this often (closing, gathering's deadline).
const TICK: Duration = Duration::from_millis(50);
/// The channels, as the page's: (id, label, reliable).
const CHANNELS: [(u16, &str, bool); 2] = [(0, "lobby", true), (1, "lockstep", false)];

/// What a driver thread is woken with.
enum Wake {
    /// A packet in: its bytes, where from, which of our sockets it came to.
    Packet(Vec<u8>, SocketAddr, SocketAddr),
    Poke,
}

/// What a connection was asked for, made once gathering is over.
enum Want {
    Nothing,
    Offer,
    Answer(String),
}

struct Pc {
    rtc: Rtc,
    socks: Vec<Arc<UdpSocket>>,
    /// STUN transactions asking for our public address: id, the socket asking, and the gathering's deadline.
    gathering: Vec<([u8; 12], SocketAddr)>,
    gather_until: Instant,
    want: Want,
    pending: Option<SdpPendingOffer>,
    /// Our offer's media id (a code always says 0, a browser's own).
    mid: String,
    chans: Option<[ChannelId; 2]>,
    open: [bool; 2],
    local: Option<String>,
    failed: bool,
    closed: bool,
    queues: [VecDeque<Vec<u8>>; 2],
    /// Reliable messages the send buffer had no room for yet, oldest first.
    backlog: VecDeque<Vec<u8>>,
    /// The next time `rtc` wants a timeout.
    next: Instant,
    /// Our recent ICE checks (STUN binding requests): transaction id, when sent.
    checks: VecDeque<([u8; 12], Instant)>,
    rtt: i32,
    stop: Arc<AtomicBool>,
    wake: mpsc::Sender<Wake>,
}

type Shared = Arc<Mutex<Pc>>;

static PCS: Mutex<(i32, BTreeMap<i32, Shared>)> = Mutex::new((0, BTreeMap::new()));

fn locked<T>(m: &Mutex<T>) -> std::sync::MutexGuard<'_, T> {
    m.lock().unwrap_or_else(|e| e.into_inner())
}

fn get(id: i32) -> Option<Shared> {
    locked(&PCS).1.get(&id).cloned()
}

/// Tests: candidates on loopback only (a sandbox may have no network).
#[cfg(test)]
pub static LOOPBACK: AtomicBool = AtomicBool::new(true);

/// This machine's IPv4 addresses on interfaces that are up, not loopback; loopback alone when there are none (a
/// browser on this machine can still reach it).
fn addresses() -> Vec<IpAddr> {
    let lo = vec![IpAddr::from([127, 0, 0, 1])];
    #[cfg(test)]
    if LOOPBACK.load(Ordering::Relaxed) {
        return lo;
    }
    let mut ips: Vec<IpAddr> = netdev::get_interfaces()
        .into_iter()
        .filter(|i| i.is_up() && !i.is_loopback())
        .flat_map(|i| i.ipv4.into_iter().map(|n| IpAddr::V4(n.addr())))
        .filter(|ip| !ip.is_loopback() && !ip.is_unspecified() && !ip.is_multicast())
        .collect();
    ips.sort();
    ips.dedup();
    if ips.is_empty() {
        lo
    } else {
        ips
    }
}

fn random12() -> [u8; 12] {
    let mut b = [0u8; 12];
    for c in b.chunks_mut(8) {
        let mut h = std::collections::hash_map::RandomState::new().build_hasher();
        h.write_u128(std::time::SystemTime::now().duration_since(std::time::UNIX_EPOCH).unwrap_or_default().as_nanos());
        c.copy_from_slice(&h.finish().to_le_bytes()[..c.len()]);
    }
    b
}

// ---------------------------------------------------------------- STUN (gathering and timing)

const MAGIC: [u8; 4] = [0x21, 0x12, 0xa4, 0x42];

/// A STUN message's type and transaction id (None: not STUN).
fn stun(p: &[u8]) -> Option<(u16, [u8; 12])> {
    if p.len() < 20 || p[0] & 0xc0 != 0 || p[4..8] != MAGIC {
        return None;
    }
    Some((u16::from_be_bytes([p[0], p[1]]), p[8..20].try_into().ok()?))
}

/// A bare binding request with transaction id `tx`.
fn binding_request(tx: &[u8; 12]) -> Vec<u8> {
    let mut p = vec![0, 1, 0, 0];
    p.extend(MAGIC);
    p.extend(tx);
    p
}

/// The (XOR-)MAPPED-ADDRESS of a binding response, if IPv4.
fn mapped(p: &[u8]) -> Option<SocketAddr> {
    let end = (20 + u16::from_be_bytes([*p.get(2)?, *p.get(3)?]) as usize).min(p.len());
    let mut i = 20;
    let mut plain = None;
    while i + 4 <= end {
        let kind = u16::from_be_bytes([p[i], p[i + 1]]);
        let len = u16::from_be_bytes([p[i + 2], p[i + 3]]) as usize;
        let v = p.get(i + 4..i + 4 + len)?;
        if (kind == 0x0020 || kind == 0x0001) && len == 8 && v[1] == 1 {
            let x = if kind == 0x0020 { [MAGIC[0], MAGIC[1], MAGIC[0], MAGIC[1], MAGIC[2], MAGIC[3]] } else { [0; 6] };
            let port = u16::from_be_bytes([v[2] ^ x[0], v[3] ^ x[1]]);
            let ip = [v[4] ^ x[2], v[5] ^ x[3], v[6] ^ x[4], v[7] ^ x[5]];
            let a = SocketAddr::from((ip, port));
            if kind == 0x0020 {
                return Some(a);
            }
            plain = Some(a);
        }
        i += 4 + len.div_ceil(4) * 4;
    }
    plain
}

/// The STUN servers' IPv4 addresses in `RTCConfiguration.iceServers` JSON (`stun:host:port` URLs; TURN is left out).
fn stun_servers(ice_json: &str) -> Vec<SocketAddr> {
    let Ok(serde_json::Value::Array(servers)) = serde_json::from_str(ice_json) else { return vec![] };
    let mut urls = Vec::new();
    for s in &servers {
        match s.get("urls") {
            Some(serde_json::Value::String(u)) => urls.push(u.clone()),
            Some(serde_json::Value::Array(us)) => urls.extend(us.iter().filter_map(|u| u.as_str().map(String::from))),
            _ => {}
        }
    }
    let mut out = Vec::new();
    for u in urls {
        let Some(hp) = u.strip_prefix("stun:") else { continue };
        let hp = hp.split('?').next().unwrap_or(hp);
        let hp = if hp.rsplit_once(':').is_some_and(|(_, p)| p.parse::<u16>().is_ok()) { hp.to_string() } else { format!("{hp}:3478") };
        if let Ok(addrs) = hp.to_socket_addrs() {
            out.extend(addrs.filter(SocketAddr::is_ipv4).take(1));
        }
    }
    out
}

// ---------------------------------------------------------------- a connection

fn config((id, label, reliable): (u16, &str, bool)) -> ChannelConfig {
    ChannelConfig {
        label: label.into(),
        ordered: reliable,
        reliability: if reliable { Reliability::Reliable } else { Reliability::MaxRetransmits { retransmits: 0 } },
        negotiated: Some(id),
        protocol: String::new(),
    }
}

/// A remote description as str0m reads it: IP candidates only (a browser's mDNS names can't be resolved here).
fn ip_candidates_only(sdp: &str) -> String {
    sdp.split("\r\n")
        .filter(|l| {
            l.strip_prefix("a=candidate:").is_none_or(|c| c.split_whitespace().nth(4).is_some_and(|a| a.parse::<IpAddr>().is_ok()))
        })
        .collect::<Vec<_>>()
        .join("\r\n")
}

impl Pc {
    /// Sends what `rtc` has to send and handles its events; notes its next timeout.
    fn drain(&mut self) {
        loop {
            match self.rtc.poll_output() {
                Ok(Output::Timeout(t)) => {
                    self.next = t;
                    return;
                }
                Ok(Output::Transmit(t)) => {
                    if let Some((1, tx)) = stun(&t.contents) {
                        if self.checks.len() >= 32 {
                            self.checks.pop_front();
                        }
                        self.checks.push_back((tx, Instant::now()));
                    }
                    if let Some(s) = self.socks.iter().find(|s| s.local_addr().is_ok_and(|a| a == t.source)) {
                        let _ = s.send_to(&t.contents, t.destination);
                    }
                }
                Ok(Output::Event(e)) => self.event(e),
                Err(e) => {
                    eprintln!("pbnet: WebRTC: {e}");
                    self.closed = true;
                    return;
                }
            }
        }
    }

    fn event(&mut self, e: Event) {
        let Some([msgs, dgrams]) = self.chans else { return };
        match e {
            Event::ChannelOpen(id, _) => {
                self.open[0] |= id == msgs;
                self.open[1] |= id == dgrams;
            }
            Event::ChannelData(d) if d.id == msgs => self.queues[0].push_back(d.data),
            Event::ChannelData(d) if d.id == dgrams => self.queues[1].push_back(d.data),
            Event::ChannelClose(id) if id == msgs || id == dgrams => self.closed = true,
            Event::IceConnectionStateChange(IceConnectionState::Disconnected) => self.closed = true,
            _ => {}
        }
    }

    /// A packet to one of our sockets.
    fn input(&mut self, data: Vec<u8>, from: SocketAddr, at: SocketAddr) {
        if let Some((0x0101, tx)) = stun(&data) {
            if let Some(i) = self.gathering.iter().position(|(t, _)| *t == tx) {
                let (_, base) = self.gathering.remove(i);
                if let Some(c) = mapped(&data).filter(|a| *a != base).and_then(|a| Candidate::server_reflexive(a, base, "udp").ok()) {
                    self.rtc.add_local_candidate(c);
                }
                return;
            }
            if let Some(i) = self.checks.iter().position(|(t, _)| *t == tx) {
                self.rtt = self.checks[i].1.elapsed().as_millis().min(i32::MAX as u128) as i32;
                self.checks.remove(i);
            }
        }
        let Ok(contents) = data.as_slice().try_into() else { return };
        let r = Receive { proto: Protocol::Udp, source: from, destination: at, contents };
        if self.rtc.handle_input(Input::Receive(Instant::now(), r)).is_err() {
            return;
        }
        self.drain();
    }

    /// Timers, the description once gathered, the backlog.
    fn tick(&mut self) {
        let now = Instant::now();
        if now >= self.next {
            let _ = self.rtc.handle_input(Input::Timeout(now));
            self.drain();
        }
        if !self.gathering.is_empty() && now >= self.gather_until {
            self.gathering.clear();
        }
        if self.gathering.is_empty() && self.local.is_none() && !self.failed {
            match std::mem::replace(&mut self.want, Want::Nothing) {
                Want::Nothing => {}
                Want::Offer => self.make_offer(),
                Want::Answer(sdp) => self.make_answer(&sdp),
            }
        }
        while let (Some(m), Some([msgs, _])) = (self.backlog.front(), self.chans) {
            match self.rtc.channel(msgs).map(|mut c| c.write(true, m)) {
                Some(Ok(true)) => drop(self.backlog.pop_front()),
                _ => break,
            }
            self.drain();
        }
    }

    fn make_offer(&mut self) {
        let mut change = self.rtc.sdp_api();
        let ids = CHANNELS.map(|c| change.add_channel_with_config(config(c)));
        let Some((offer, pending)) = change.apply() else {
            self.failed = true;
            return;
        };
        let sdp = offer.to_sdp_string();
        self.mid = sdp.lines().find_map(|l| l.trim().strip_prefix("a=mid:")).unwrap_or("0").to_string();
        self.chans = Some(ids);
        self.pending = Some(pending);
        self.local = Some(sdp);
        self.drain();
    }

    fn make_answer(&mut self, offer: &str) {
        // the answer we send says active (`Sig::sdp`): we must be the DTLS client, which str0m is for a passive remote
        let offer = ip_candidates_only(offer).replace("a=setup:actpass", "a=setup:passive");
        let answer = SdpOffer::from_sdp_string(&offer).map_err(|e| e.to_string()).and_then(|o| self.rtc.sdp_api().accept_offer(o).map_err(|e| e.to_string()));
        let answer = match answer {
            Ok(a) => a,
            Err(e) => {
                eprintln!("pbnet: WebRTC offer: {e}");
                self.failed = true;
                return;
            }
        };
        self.drain();
        // negotiated channels, made one at a time (str0m drains every change before the next)
        let ids = CHANNELS.map(|c| {
            let id = self.rtc.direct_api().create_data_channel(config(c));
            self.drain();
            id
        });
        self.chans = Some(ids);
        self.local = Some(answer.to_sdp_string());
    }

    fn accept(&mut self, answer: &str) {
        let Some(pending) = self.pending.take() else { return };
        let mid = &self.mid;
        let answer = ip_candidates_only(answer).replace("a=mid:0\r\n", &format!("a=mid:{mid}\r\n")).replace("BUNDLE 0\r\n", &format!("BUNDLE {mid}\r\n"));
        let r = SdpAnswer::from_sdp_string(&answer).map_err(|e| e.to_string()).and_then(|a| self.rtc.sdp_api().accept_answer(pending, a).map_err(|e| e.to_string()));
        if let Err(e) = r {
            eprintln!("pbnet: WebRTC answer: {e}");
            self.failed = true;
        }
        self.drain();
    }

    fn state(&self) -> i32 {
        if self.failed || self.closed {
            2
        } else if self.open == [true; 2] {
            1
        } else {
            0
        }
    }

    /// Closes the channels (the other end hears it at once) and the connection; the threads end.
    fn shut(&mut self) {
        if let Some([msgs, _]) = self.chans.filter(|_| !self.closed) {
            self.rtc.direct_api().close_data_channel(msgs);
            self.drain();
        }
        self.rtc.disconnect();
        self.drain();
        self.closed = true;
        self.stop.store(true, Ordering::SeqCst);
        let _ = self.wake.send(Wake::Poke);
    }
}

/// One socket's packets to its connection's driver until it stops.
fn read(s: Arc<UdpSocket>, at: SocketAddr, wake: mpsc::Sender<Wake>, stop: Arc<AtomicBool>) {
    let mut buf = vec![0u8; 2048];
    while !stop.load(Ordering::SeqCst) {
        match s.recv_from(&mut buf) {
            Ok((n, from)) => {
                if wake.send(Wake::Packet(buf[..n].to_vec(), from, at)).is_err() {
                    return;
                }
            }
            Err(e) if matches!(e.kind(), std::io::ErrorKind::WouldBlock | std::io::ErrorKind::TimedOut | std::io::ErrorKind::Interrupted) => {}
            // e.g. ICMP port unreachable from a candidate that isn't there
            Err(_) => std::thread::sleep(Duration::from_millis(5)),
        }
    }
}

/// The connection's driver: packets in, timers, until it is closed.
fn drive(pc: Shared, wake: mpsc::Receiver<Wake>, stop: Arc<AtomicBool>) {
    loop {
        let wait = {
            let mut p = locked(&pc);
            p.tick();
            p.next.saturating_duration_since(Instant::now()).min(TICK)
        };
        if stop.load(Ordering::SeqCst) {
            return;
        }
        match wake.recv_timeout(wait) {
            Ok(Wake::Packet(d, from, at)) => locked(&pc).input(d, from, at),
            Ok(Wake::Poke) | Err(mpsc::RecvTimeoutError::Timeout) => {}
            Err(mpsc::RecvTimeoutError::Disconnected) => return,
        }
    }
}

/// A new connection (its sockets, threads and STUN requests), or -1 if no socket could be bound.
fn open(ice_json: &str) -> i32 {
    let mut rtc = Rtc::builder().build(Instant::now());
    let (wt, wr) = mpsc::channel();
    let stop = Arc::new(AtomicBool::new(false));
    let mut socks = Vec::new();
    for ip in addresses() {
        let Ok(s) = UdpSocket::bind(SocketAddr::new(ip, 0)) else { continue };
        let Ok(at) = s.local_addr() else { continue };
        let Ok(c) = Candidate::host(at, "udp") else { continue };
        if s.set_read_timeout(Some(Duration::from_millis(100))).is_err() {
            continue;
        }
        rtc.add_local_candidate(c);
        socks.push(Arc::new(s));
    }
    if socks.is_empty() {
        return -1;
    }
    let mut gathering = Vec::new();
    for server in stun_servers(ice_json) {
        for s in &socks {
            let (Ok(at), tx) = (s.local_addr(), random12()) else { continue };
            if s.send_to(&binding_request(&tx), server).is_ok() {
                gathering.push((tx, at));
            }
        }
    }
    let now = Instant::now();
    let pc = Arc::new(Mutex::new(Pc {
        rtc,
        socks: socks.clone(),
        gathering,
        gather_until: now + GATHER,
        want: Want::Nothing,
        pending: None,
        mid: "0".into(),
        chans: None,
        open: [false; 2],
        local: None,
        failed: false,
        closed: false,
        queues: Default::default(),
        backlog: VecDeque::new(),
        next: now,
        checks: VecDeque::new(),
        rtt: -1,
        stop: stop.clone(),
        wake: wt.clone(),
    }));
    for s in socks {
        let (wt, stop) = (wt.clone(), stop.clone());
        let Ok(at) = s.local_addr() else { continue };
        let _ = std::thread::Builder::new().name("pbnet-rtc-rx".into()).spawn(move || read(s, at, wt, stop));
    }
    let (p, st) = (pc.clone(), stop);
    if std::thread::Builder::new().name("pbnet-rtc".into()).spawn(move || drive(p, wr, st)).is_err() {
        locked(&pc).shut();
        return -1;
    }
    let mut g = locked(&PCS);
    g.0 += 1;
    let id = g.0;
    g.1.insert(id, pc);
    id
}

// ---------------------------------------------------------------- `super::js`'s imports

/// # Safety
/// The string arguments of the functions below are null or valid NUL-terminated strings for the call.
unsafe fn s(p: *const c_char) -> String {
    if p.is_null() {
        return String::new();
    }
    // SAFETY: the caller's contract.
    unsafe { CStr::from_ptr(p) }.to_string_lossy().into_owned()
}

/// # Safety
/// `ice_json` is null or a valid NUL-terminated string for the call.
pub unsafe fn pbnet_js_rtc_open(ice_json: *const c_char) -> i32 {
    // SAFETY: the caller's contract.
    open(&unsafe { s(ice_json) })
}

/// # Safety
/// None: any `id`.
pub unsafe fn pbnet_js_rtc_offer(id: i32) {
    if let Some(pc) = get(id) {
        let mut p = locked(&pc);
        p.want = Want::Offer;
        let _ = p.wake.send(Wake::Poke);
    }
}

/// # Safety
/// `offer` is null or a valid NUL-terminated string for the call.
pub unsafe fn pbnet_js_rtc_answer(id: i32, offer: *const c_char) {
    // SAFETY: the caller's contract.
    let offer = unsafe { s(offer) };
    if let Some(pc) = get(id) {
        let mut p = locked(&pc);
        p.want = Want::Answer(offer);
        let _ = p.wake.send(Wake::Poke);
    }
}

/// # Safety
/// `answer` is null or a valid NUL-terminated string for the call.
pub unsafe fn pbnet_js_rtc_accept(id: i32, answer: *const c_char) {
    // SAFETY: the caller's contract.
    let answer = unsafe { s(answer) };
    if let Some(pc) = get(id) {
        locked(&pc).accept(&answer);
    }
}

/// # Safety
/// `buf` holds `cap` writable bytes.
pub unsafe fn pbnet_js_rtc_local(id: i32, buf: *mut c_char, cap: i32) -> i32 {
    let Some(pc) = get(id) else { return -1 };
    let p = locked(&pc);
    if p.failed {
        return -1;
    }
    let Some(l) = &p.local else { return 0 };
    if l.len() < cap as usize {
        // SAFETY: the caller's buffer holds `cap` bytes; `l.len() + 1` of them are written.
        unsafe {
            std::ptr::copy_nonoverlapping(l.as_ptr(), buf as *mut u8, l.len());
            *buf.add(l.len()) = 0;
        }
    }
    l.len() as i32
}

/// # Safety
/// None: any `id`.
pub unsafe fn pbnet_js_rtc_state(id: i32) -> i32 {
    get(id).map_or(2, |pc| locked(&pc).state())
}

/// # Safety
/// `data` holds `len` readable bytes.
pub unsafe fn pbnet_js_rtc_send(id: i32, chan: i32, data: *const u8, len: i32) -> i32 {
    let Some(pc) = get(id) else { return 0 };
    let mut p = locked(&pc);
    let chan = chan as usize & 1;
    let Some(ids) = p.chans.filter(|_| p.state() == 1 && p.open[chan]) else { return 0 };
    // SAFETY: the caller's `data` holds `len` bytes.
    let d = unsafe { std::slice::from_raw_parts(data, len.max(0) as usize) };
    if chan == 0 {
        if !p.backlog.is_empty() || !matches!(p.rtc.channel(ids[0]).map(|mut c| c.write(true, d)), Some(Ok(true))) {
            p.backlog.push_back(d.to_vec());
        }
    } else if let Some(mut c) = p.rtc.channel(ids[1]) {
        let _ = c.write(true, d);
    }
    p.drain();
    let _ = p.wake.send(Wake::Poke);
    1
}

/// # Safety
/// `buf` holds `cap` writable bytes.
pub unsafe fn pbnet_js_rtc_recv(id: i32, chan: i32, buf: *mut u8, cap: i32) -> i32 {
    let Some(pc) = get(id) else { return -1 };
    let mut p = locked(&pc);
    let q = &mut p.queues[chan as usize & 1];
    let Some(m) = q.front() else { return -1 };
    let n = m.len() as i32;
    if n <= cap {
        // SAFETY: the caller's buffer holds `cap` bytes.
        unsafe { std::ptr::copy_nonoverlapping(m.as_ptr(), buf, m.len()) };
        q.pop_front();
    }
    n
}

/// # Safety
/// None: any `id`.
pub unsafe fn pbnet_js_rtc_rtt(id: i32) -> i32 {
    get(id).map_or(-1, |pc| locked(&pc).rtt)
}

/// # Safety
/// None: any `id`.
pub unsafe fn pbnet_js_rtc_close(id: i32) {
    let pc = locked(&PCS).1.remove(&id);
    if let Some(pc) = pc {
        locked(&pc).shut();
    }
}

/// Tests (N98's redial): every open connection drops, as on a network change; both ends see it closed.
#[cfg(test)]
pub fn cut_open() {
    let pcs: Vec<Shared> = locked(&PCS).1.values().cloned().collect();
    for pc in pcs {
        let mut p = locked(&pc);
        if p.state() == 1 {
            p.shut();
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn stun_addresses() {
        let tx = [9u8; 12];
        let req = binding_request(&tx);
        assert_eq!(stun(&req), Some((1, tx)));
        // a response with XOR-MAPPED-ADDRESS 203.0.113.9:40000
        let mut p = vec![0x01, 0x01, 0, 12];
        p.extend(MAGIC);
        p.extend(tx);
        let port = 40000u16 ^ 0x2112;
        let ip = [203 ^ 0x21, 0 ^ 0x12, 113 ^ 0xa4, 9 ^ 0x42];
        p.extend([0x00, 0x20, 0, 8, 0, 1]);
        p.extend(port.to_be_bytes());
        p.extend(ip);
        assert_eq!(stun(&p), Some((0x0101, tx)));
        assert_eq!(mapped(&p), Some(SocketAddr::from(([203, 0, 113, 9], 40000))));
        assert_eq!(stun(&[22, 254, 253, 0, 0]), None);
        assert_eq!(stun_servers(r#"[{"urls":"turn:x.example"}]"#), vec![]);
        assert_eq!(stun_servers(r#"[{"urls":["stun:127.0.0.1:19302"]}]"#), vec![SocketAddr::from(([127, 0, 0, 1], 19302))]);
        assert_eq!(stun_servers(r#"[{"urls":"stun:127.0.0.1"}]"#), vec![SocketAddr::from(([127, 0, 0, 1], 3478))]);
    }

    /// The relay setting n0: an offer gathers a server-reflexive candidate from Google's public STUN server. Needs
    /// the internet: `cargo test -- --ignored stun_gathers`.
    #[test]
    #[ignore]
    fn stun_gathers_a_public_address() {
        LOOPBACK.store(false, Ordering::Relaxed);
        // SAFETY: NUL-terminated strings and a buffer of the size passed, for each call.
        unsafe {
            let ice = std::ffi::CString::new(super::super::Relay::N0.ice_servers()).unwrap();
            let id = pbnet_js_rtc_open(ice.as_ptr());
            assert!(id > 0);
            pbnet_js_rtc_offer(id);
            let mut buf = vec![0u8; 8192];
            let mut n = 0;
            for _ in 0..200 {
                n = pbnet_js_rtc_local(id, buf.as_mut_ptr() as *mut c_char, buf.len() as i32);
                if n != 0 {
                    break;
                }
                std::thread::sleep(Duration::from_millis(20));
            }
            assert!(n > 0, "no description");
            let sdp = String::from_utf8_lossy(&buf[..n as usize]).into_owned();
            pbnet_js_rtc_close(id);
            assert!(sdp.contains("typ srflx"), "{sdp}");
            let sig = super::super::Sig::from_sdp(&sdp, [0; 8]).unwrap();
            assert!(sig.cands.len() >= 2, "{sig:?}");
        }
    }

    #[test]
    fn mdns_candidates_left_out() {
        let sdp = "v=0\r\na=candidate:1 1 udp 1 3f0c.local 5 typ host\r\na=candidate:2 1 udp 1 10.0.0.2 6 typ host\r\na=mid:0\r\n";
        assert_eq!(ip_candidates_only(sdp), "v=0\r\na=candidate:2 1 udp 1 10.0.0.2 6 typ host\r\na=mid:0\r\n");
    }
}
