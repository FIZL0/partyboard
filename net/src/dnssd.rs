//! N119: `_pbn._udp` as standard DNS-SD on our own multicast socket (every iroh build but iOS's): answers browses
//! and resolves for this endpoint, and listens for other endpoints' records, so an iPhone's Bonjour browse
//! (`bonjour.rs`) finds a desktop host and the desktop hears the iPhone. See `lan.rs`.
//!
//! IPv4 mDNS only (224.0.0.251:5353), on every IPv4 interface. Beside iroh's own mDNS (which keeps finding older
//! builds); both fill the same LAN list.
//!
//! - **Answers**: a PTR query for `_pbn._udp.local` gets the PTR to `<id>._pbn._udp.local` with SRV, TXT and the A/AAAA
//!   records in the additional section (a resolve then needs no more queries); queries for the SRV, TXT or the SRV's
//!   target `<id>-<port>.local` are answered too. Changes are announced twice, and a goodbye (TTL 0) is sent when
//!   it stops.
//! - **Listens**: every response's PTR/SRV/TXT/A/AAAA records under `_pbn._udp.local` and their targets (mDNSResponder
//!   puts the SRV and TXT in the additional section). A PTR query every [`QUERY_EVERY`] keeps them fresh; one not
//!   heard for [`FORGET`] is forgotten, one whose goodbye is heard at once.

use std::collections::{BTreeSet, HashMap};
use std::net::{IpAddr, Ipv4Addr, Ipv6Addr, SocketAddr, SocketAddrV4};
use std::sync::Arc;
use std::time::{Duration, Instant};

use iroh::address_lookup::EndpointData;
use iroh::EndpointId;
use tokio::net::UdpSocket;
use tokio::sync::watch;

use crate::lan::{self, Book, Heard};

const GROUP: Ipv4Addr = Ipv4Addr::new(224, 0, 0, 251);
const PORT: u16 = 5353;
/// How often we ask who is there (after a quicker start).
const QUERY_EVERY: Duration = Duration::from_secs(10);
/// An endpoint not heard for this long is gone (three queries missed).
const FORGET: Duration = Duration::from_secs(35);
/// The TTL of our records (RFC 6762's 120 s for host records; short, so a crashed host's listing goes soon).
const TTL: u32 = 120;

pub const A: u16 = 1;
pub const PTR: u16 = 12;
pub const TXT: u16 = 16;
pub const AAAA: u16 = 28;
pub const SRV: u16 = 33;
pub const ANY: u16 = 255;

// ---------------------------------------------------------------- the wire

/// A resource record's data, the kinds DNS-SD uses.
#[derive(Clone, Debug, PartialEq)]
pub enum Data {
    A(Ipv4Addr),
    Aaaa(Ipv6Addr),
    Ptr(String),
    Srv { port: u16, target: String },
    Txt(Vec<u8>),
    Other(u16),
}

impl Data {
    fn rtype(&self) -> u16 {
        match self {
            Data::A(_) => A,
            Data::Aaaa(_) => AAAA,
            Data::Ptr(_) => PTR,
            Data::Srv { .. } => SRV,
            Data::Txt(_) => TXT,
            Data::Other(t) => *t,
        }
    }
}

/// A resource record: name (lowercase, no final dot), TTL, cache-flush bit, data.
#[derive(Clone, Debug, PartialEq)]
pub struct Rr {
    pub name: String,
    pub ttl: u32,
    pub flush: bool,
    pub data: Data,
}

/// A question: name, type and the unicast-response bit.
#[derive(Clone, Debug, PartialEq)]
pub struct Question {
    pub name: String,
    pub qtype: u16,
    pub unicast: bool,
}

/// A DNS message as mDNS uses it: every record section in one list (answers first).
#[derive(Clone, Debug, Default, PartialEq)]
pub struct Packet {
    pub id: u16,
    pub response: bool,
    pub questions: Vec<Question>,
    pub answers: Vec<Rr>,
    pub additionals: Vec<Rr>,
}

fn read_name(buf: &[u8], mut pos: usize) -> Option<(String, usize)> {
    let mut labels: Vec<String> = Vec::new();
    let mut end = None;
    for _ in 0..128 {
        let n = *buf.get(pos)? as usize;
        if n & 0xc0 == 0xc0 {
            let p = ((n & 0x3f) << 8) | *buf.get(pos + 1)? as usize;
            end.get_or_insert(pos + 2);
            if p >= pos {
                return None;
            }
            pos = p;
            continue;
        }
        if n == 0 {
            let name = labels.join(".").to_ascii_lowercase();
            return Some((name, end.unwrap_or(pos + 1)));
        }
        let l = buf.get(pos + 1..pos + 1 + n)?;
        labels.push(String::from_utf8_lossy(l).into_owned());
        pos += 1 + n;
    }
    None
}

fn u16_at(buf: &[u8], pos: usize) -> Option<u16> {
    Some(u16::from_be_bytes(buf.get(pos..pos + 2)?.try_into().ok()?))
}

fn read_rr(buf: &[u8], pos: usize) -> Option<(Rr, usize)> {
    let (name, pos) = read_name(buf, pos)?;
    let rtype = u16_at(buf, pos)?;
    let class = u16_at(buf, pos + 2)?;
    let ttl = u32::from_be_bytes(buf.get(pos + 4..pos + 8)?.try_into().ok()?);
    let len = u16_at(buf, pos + 8)? as usize;
    let at = pos + 10;
    let rdata = buf.get(at..at + len)?;
    let data = match rtype {
        A if len == 4 => Data::A(Ipv4Addr::from(<[u8; 4]>::try_from(rdata).ok()?)),
        AAAA if len == 16 => Data::Aaaa(Ipv6Addr::from(<[u8; 16]>::try_from(rdata).ok()?)),
        PTR => Data::Ptr(read_name(buf, at)?.0),
        SRV if len >= 7 => Data::Srv { port: u16_at(buf, at + 4)?, target: read_name(buf, at + 6)?.0 },
        TXT => Data::Txt(rdata.to_vec()),
        t => Data::Other(t),
    };
    Some((Rr { name, ttl, flush: class & 0x8000 != 0, data }, at + len))
}

/// Reads a message; None if it is malformed.
pub fn parse(buf: &[u8]) -> Option<Packet> {
    let id = u16_at(buf, 0)?;
    let flags = u16_at(buf, 2)?;
    let counts: Vec<usize> = (0..4).map(|i| u16_at(buf, 4 + 2 * i).map(usize::from)).collect::<Option<_>>()?;
    let mut p = Packet { id, response: flags & 0x8000 != 0, ..Packet::default() };
    let mut pos = 12;
    for _ in 0..counts[0] {
        let (name, at) = read_name(buf, pos)?;
        let qtype = u16_at(buf, at)?;
        let class = u16_at(buf, at + 2)?;
        p.questions.push(Question { name, qtype, unicast: class & 0x8000 != 0 });
        pos = at + 4;
    }
    for i in 0..counts[1] + counts[2] + counts[3] {
        let (rr, at) = read_rr(buf, pos)?;
        if i < counts[1] { p.answers.push(rr) } else { p.additionals.push(rr) }
        pos = at;
    }
    Some(p)
}

fn put_name(out: &mut Vec<u8>, name: &str) {
    for l in name.split('.').filter(|l| !l.is_empty()) {
        let l = &l.as_bytes()[..l.len().min(63)];
        out.push(l.len() as u8);
        out.extend_from_slice(l);
    }
    out.push(0);
}

fn put_rr(out: &mut Vec<u8>, rr: &Rr) {
    put_name(out, &rr.name);
    out.extend_from_slice(&rr.data.rtype().to_be_bytes());
    out.extend_from_slice(&(1u16 | if rr.flush { 0x8000 } else { 0 }).to_be_bytes());
    out.extend_from_slice(&rr.ttl.to_be_bytes());
    let mut d = Vec::new();
    match &rr.data {
        Data::A(a) => d.extend_from_slice(&a.octets()),
        Data::Aaaa(a) => d.extend_from_slice(&a.octets()),
        Data::Ptr(n) => put_name(&mut d, n),
        Data::Srv { port, target } => {
            d.extend_from_slice(&[0, 0, 0, 0]);
            d.extend_from_slice(&port.to_be_bytes());
            put_name(&mut d, target);
        }
        Data::Txt(t) => d.extend_from_slice(t),
        Data::Other(_) => {}
    }
    out.extend_from_slice(&(d.len() as u16).to_be_bytes());
    out.extend_from_slice(&d);
}

/// Writes a message (no name compression).
pub fn encode(p: &Packet) -> Vec<u8> {
    let mut out = Vec::new();
    out.extend_from_slice(&p.id.to_be_bytes());
    // a response is authoritative (RFC 6762 18.4)
    out.extend_from_slice(&(if p.response { 0x8400u16 } else { 0 }).to_be_bytes());
    for n in [p.questions.len(), p.answers.len(), 0, p.additionals.len()] {
        out.extend_from_slice(&(n as u16).to_be_bytes());
    }
    for q in &p.questions {
        put_name(&mut out, &q.name);
        out.extend_from_slice(&q.qtype.to_be_bytes());
        out.extend_from_slice(&(1u16 | if q.unicast { 0x8000 } else { 0 }).to_be_bytes());
    }
    for rr in p.answers.iter().chain(&p.additionals) {
        put_rr(&mut out, rr);
    }
    out
}

// ---------------------------------------------------------------- our records

fn service_name() -> String {
    format!("{}.local", lan::service())
}

/// This endpoint's records: PTR, SRV, TXT and the SRV target's addresses. The SRV is iroh's IPv4 port (its IPv6
/// socket may have another: those addresses are left out, one SRV is all a resolve takes).
fn records(id: &EndpointId, data: &EndpointData, ttl: u32, seq: u64) -> Option<(Rr, Rr, Rr, Vec<Rr>)> {
    let addrs: Vec<SocketAddr> = data.ip_addrs().copied().collect();
    let port = addrs.iter().find(|a| a.is_ipv4()).or(addrs.first())?.port();
    let label = lan::label(id);
    let instance = format!("{label}.{}", service_name());
    // not iroh's mDNS's `<id>-<port>.local` (lan.rs: its TTL 0 records would be our goodbyes)
    let target = format!("{label}.local");
    let ptr = Rr { name: service_name(), ttl, flush: false, data: Data::Ptr(instance.clone()) };
    let srv = Rr { name: instance.clone(), ttl, flush: true, data: Data::Srv { port, target: target.clone() } };
    let txt = Rr { name: instance, ttl, flush: true, data: Data::Txt(lan::txt(data, seq)) };
    let ips: BTreeSet<IpAddr> = addrs.iter().filter(|a| a.port() == port).map(|a| a.ip()).collect();
    let ips = ips
        .into_iter()
        .map(|ip| Rr {
            name: target.clone(),
            ttl,
            flush: true,
            data: match ip {
                IpAddr::V4(a) => Data::A(a),
                IpAddr::V6(a) => Data::Aaaa(a),
            },
        })
        .collect();
    Some((ptr, srv, txt, ips))
}

/// Our answer to `q` (answers, additionals), if it asks for one of our records.
fn answer(q: &[Question], me: &(Rr, Rr, Rr, Vec<Rr>)) -> Option<(Vec<Rr>, Vec<Rr>)> {
    let (ptr, srv, txt, ips) = me;
    let target = match &srv.data {
        Data::Srv { target, .. } => target.clone(),
        _ => return None,
    };
    let (mut an, mut ad) = (Vec::new(), Vec::new());
    let add = |v: &mut Vec<Rr>, r: &Rr| {
        if !v.contains(r) {
            v.push(r.clone())
        }
    };
    for q in q {
        let t = q.qtype;
        if q.name == ptr.name && (t == PTR || t == ANY) {
            add(&mut an, ptr);
            for r in [srv, txt].into_iter().chain(ips) {
                add(&mut ad, r);
            }
        }
        if q.name == srv.name {
            if t == SRV || t == ANY {
                add(&mut an, srv);
                ips.iter().for_each(|r| add(&mut ad, r));
            }
            if t == TXT || t == ANY {
                add(&mut an, txt);
            }
        }
        if q.name == target {
            ips.iter().filter(|r| t == ANY || r.data.rtype() == t).for_each(|r| add(&mut an, r));
        }
    }
    ad.retain(|r| !an.contains(r));
    (!an.is_empty()).then_some((an, ad))
}

// ---------------------------------------------------------------- what we hear

/// What the listener knows: SRV and TXT by instance, addresses by host name, each with when it was heard.
#[derive(Default)]
struct Cache {
    srv: HashMap<String, (u16, String, Instant)>,
    txt: HashMap<String, (Vec<u8>, Instant)>,
    hosts: HashMap<String, (BTreeSet<IpAddr>, Instant)>,
}

/// Takes in a response's records: updates the book for every endpoint they touch, and returns the host names
/// whose addresses we still need.
fn hear(p: &Packet, me: &EndpointId, h: &mut Cache, book: &Book) -> Vec<String> {
    let now = Instant::now();
    let suffix = format!(".{}", service_name());
    let id_of = |name: &str| name.strip_suffix(&suffix).filter(|l| !l.contains('.')).and_then(lan::id_of);
    let mut touched = BTreeSet::new();
    let mut gone = BTreeSet::new();
    // addresses first: a fresh flush set replaces the old one
    let mut flushed = BTreeSet::new();
    for rr in p.answers.iter().chain(&p.additionals) {
        let ip: IpAddr = match rr.data {
            Data::A(a) => a.into(),
            Data::Aaaa(a) => a.into(),
            _ => continue,
        };
        let e = h.hosts.entry(rr.name.clone()).or_insert_with(|| (BTreeSet::new(), now));
        if rr.flush && flushed.insert(rr.name.clone()) {
            e.0.clear();
        }
        if rr.ttl == 0 {
            e.0.remove(&ip);
        } else {
            e.0.insert(ip);
        }
        e.1 = now;
        touched.extend(h.srv.iter().filter(|(_, s)| s.1 == rr.name).map(|(i, _)| i.clone()));
    }
    for rr in p.answers.iter().chain(&p.additionals) {
        match &rr.data {
            Data::Ptr(inst) if rr.name == service_name() && id_of(inst).is_some() => {
                if rr.ttl == 0 {
                    gone.insert(inst.clone());
                } else {
                    touched.insert(inst.clone());
                }
            }
            Data::Srv { port, target } if id_of(&rr.name).is_some() => {
                if rr.ttl == 0 {
                    // a goodbye is for that record only (iroh's mDNS sends its own SRV with TTL 0 all the time)
                    if h.srv.get(&rr.name).is_some_and(|s| (s.0, &s.1) == (*port, target)) {
                        gone.insert(rr.name.clone());
                    }
                } else {
                    h.srv.insert(rr.name.clone(), (*port, target.clone(), now));
                    touched.insert(rr.name.clone());
                }
            }
            Data::Txt(t) if id_of(&rr.name).is_some() => {
                // never back to an older TXT of ours (`lan.rs`)
                let older = |old: &Vec<u8>| lan::untxt(t).seq.unwrap_or(u64::MAX) < lan::untxt(old).seq.unwrap_or(0);
                if rr.ttl > 0 && h.txt.get(&rr.name).is_some_and(|x| older(&x.0)) {
                    h.txt.get_mut(&rr.name).unwrap().1 = now;
                } else if rr.ttl > 0 {
                    h.txt.insert(rr.name.clone(), (t.clone(), now));
                } else if h.txt.get(&rr.name).is_some_and(|x| &x.0 == t) {
                    h.txt.remove(&rr.name);
                }
                touched.insert(rr.name.clone());
            }
            _ => {}
        }
    }
    let mut need = Vec::new();
    for inst in gone {
        h.srv.remove(&inst);
        h.txt.remove(&inst);
        touched.remove(&inst);
        if let Some(id) = id_of(&inst) {
            book.remove(&id);
        }
    }
    for inst in touched {
        let Some(id) = id_of(&inst).filter(|id| id != me) else { continue };
        // a PTR alone (a browse answer without its additionals) only refreshes
        let Some((port, target, _)) = h.srv.get(&inst).cloned() else {
            need.push(inst);
            continue;
        };
        if let Some(s) = h.srv.get_mut(&inst) {
            s.2 = now;
        }
        let ips = h.hosts.get(&target).map(|x| x.0.clone()).unwrap_or_default();
        if ips.is_empty() {
            need.push(target.clone());
        }
        let lan::Txt { user_data, relay, .. } = h.txt.get(&inst).map(|t| lan::untxt(&t.0)).unwrap_or_default();
        let addrs = ips.into_iter().map(|ip| SocketAddr::new(ip, port)).collect();
        book.set(id, Heard { addrs, user_data, relay, until: Some(now + FORGET) });
    }
    need
}

// ---------------------------------------------------------------- the socket

fn socket() -> std::io::Result<std::net::UdpSocket> {
    use socket2::{Domain, Protocol, Socket, Type};
    let s = Socket::new(Domain::IPV4, Type::DGRAM, Some(Protocol::UDP))?;
    s.set_reuse_address(true)?;
    // others answer on 5353 too (the system's responder, iroh's mDNS): share the port
    #[cfg(unix)]
    let _ = s.set_reuse_port(true);
    s.bind(&SocketAddr::from((Ipv4Addr::UNSPECIFIED, PORT)).into())?;
    s.set_multicast_loop_v4(true)?;
    s.set_multicast_ttl_v4(255)?;
    s.set_nonblocking(true)?;
    Ok(s.into())
}

/// This machine's IPv4 interface addresses (up, multicast-capable or loopback).
fn interfaces() -> Vec<Ipv4Addr> {
    let mut v: Vec<Ipv4Addr> =
        netdev::get_interfaces().into_iter().filter(|i| i.is_up()).flat_map(|i| i.ipv4.into_iter().map(|n| n.addr())).collect();
    v.sort();
    v.dedup();
    v
}

struct Responder {
    sock: UdpSocket,
    joined: Vec<Ipv4Addr>,
}

impl Responder {
    /// Joins the group on interfaces not joined yet.
    fn join(&mut self) {
        let now = interfaces();
        for a in &now {
            if !self.joined.contains(a) {
                let _ = self.sock.join_multicast_v4(GROUP, *a);
            }
        }
        self.joined = now;
    }

    /// Sends to the group out of every interface.
    async fn multicast(&self, p: &Packet) {
        let b = encode(p);
        let s2 = socket2::SockRef::from(&self.sock);
        for a in &self.joined {
            if s2.set_multicast_if_v4(a).is_ok() {
                let _ = self.sock.send_to(&b, SocketAddrV4::new(GROUP, PORT)).await;
            }
        }
    }
}

/// Answers and listens for `_pbn._udp` until dropped (a goodbye then). `publish` has this endpoint's data from iroh.
pub struct Dnssd {
    task: tokio::task::JoinHandle<()>,
    goodbye: Arc<std::sync::Mutex<Option<Vec<u8>>>>,
}

impl Drop for Dnssd {
    fn drop(&mut self) {
        self.task.abort();
        if let Some(b) = self.goodbye.lock().unwrap().take() {
            if let Ok(s) = socket() {
                let s2 = socket2::SockRef::from(&s);
                for a in interfaces() {
                    if s2.set_multicast_if_v4(&a).is_ok() {
                        let _ = s.send_to(&b, SocketAddrV4::new(GROUP, PORT));
                    }
                }
            }
        }
    }
}

impl Dnssd {
    /// Starts on the current tokio runtime; Err if the mDNS port can't be had.
    pub fn start(me: EndpointId, book: Book, mut publish: watch::Receiver<Option<EndpointData>>) -> Result<Dnssd, String> {
        let sock = UdpSocket::from_std(socket().map_err(|e| format!("mDNS socket: {e}"))?).map_err(|e| e.to_string())?;
        let mut r = Responder { sock, joined: Vec::new() };
        r.join();
        let goodbye = Arc::new(std::sync::Mutex::new(None));
        let bye = goodbye.clone();
        let task = tokio::spawn(async move {
            let mut heard = Cache::default();
            let mut ours: Option<(Rr, Rr, Rr, Vec<Rr>)> = None;
            let mut buf = vec![0u8; 9000];
            let mut tick = 0u32;
            // the TXT's sequence number (`lan.rs`)
            let mut seq = 0u64;
            let mut next_query = tokio::time::Instant::now();
            let mut announce_again: Option<tokio::time::Instant> = None;
            let mut asked: Option<Instant> = None;
            let query = |names: &[String]| Packet {
                questions: names
                    .iter()
                    .flat_map(|n| {
                        let t: &[u16] = if n == &service_name() { &[PTR] } else if n.ends_with(&format!(".{}", service_name())) { &[SRV, TXT] } else { &[A, AAAA] };
                        t.iter().map(|t| Question { name: n.clone(), qtype: *t, unicast: false })
                    })
                    .collect(),
                ..Packet::default()
            };
            loop {
                tokio::select! {
                    _ = tokio::time::sleep_until(next_query) => {
                        r.multicast(&query(&[service_name()])).await;
                        tick += 1;
                        next_query += if tick < 3 { Duration::from_secs(1 << tick) } else { QUERY_EVERY };
                        if tick.is_multiple_of(3) {
                            r.join();
                        }
                    }
                    _ = async { tokio::time::sleep_until(announce_again.unwrap()).await }, if announce_again.is_some() => {
                        announce_again = None;
                        if let Some(o) = &ours {
                            r.multicast(&announcement(o)).await;
                        }
                    }
                    ch = publish.changed() => {
                        if ch.is_err() {
                            break;
                        }
                        let data = publish.borrow_and_update().clone();
                        let mut new = data.as_ref().and_then(|d| records(&me, d, TTL, seq));
                        if let (Some(d), Some(n)) = (&data, &new) {
                            if ours.as_ref().is_none_or(|o| o.2 != n.2) {
                                seq += 1;
                                new = records(&me, d, TTL, seq);
                            }
                        }
                        if new == ours {
                            continue;
                        }
                        // goodbyes for the old SRV, TXT and addresses that changed (a cache flush keeps what it heard
                        // in the last second: a listing set right after the bind would stay beside the empty one)
                        if let (Some(o), Some(n)) = (&ours, &new) {
                            let old = [&o.1, &o.2].into_iter().chain(&o.3);
                            let bye: Vec<Rr> = old.filter(|r| ![&n.1, &n.2].into_iter().chain(&n.3).any(|x| x == *r)).map(|r| Rr { ttl: 0, ..r.clone() }).collect();
                            if !bye.is_empty() {
                                r.multicast(&Packet { response: true, answers: bye, ..Packet::default() }).await;
                            }
                        }
                        ours = new;
                        *bye.lock().unwrap() = ours.as_ref().map(|o| encode(&Packet { response: true, answers: vec![Rr { ttl: 0, ..o.0.clone() }, Rr { ttl: 0, ..o.1.clone() }], ..Packet::default() }));
                        if let Some(o) = &ours {
                            r.multicast(&announcement(o)).await;
                            announce_again = Some(tokio::time::Instant::now() + Duration::from_secs(1));
                        }
                    }
                    got = r.sock.recv_from(&mut buf) => {
                        let Ok((n, from)) = got else { continue };
                        let Some(p) = parse(&buf[..n]) else { continue };
                        if p.response {
                            let need = hear(&p, &me, &mut heard, &book);
                            // what a response left out (an SRV/TXT, a target's addresses), asked at most once a second
                            if !need.is_empty() && asked.is_none_or(|t: Instant| t.elapsed() >= Duration::from_secs(1)) {
                                asked = Some(Instant::now());
                                r.multicast(&query(&need)).await;
                            }
                        } else if let Some((answers, additionals)) = ours.as_ref().and_then(|o| answer(&p.questions, o)) {
                            if from.port() != PORT {
                                // a legacy unicast query (dig, RFC 6762 6.7): its ID and questions back, to it alone
                                let reply = Packet { id: p.id, response: true, questions: p.questions.clone(), answers, additionals };
                                let _ = r.sock.send_to(&encode(&reply), from).await;
                                continue;
                            }
                            let reply = Packet { response: true, answers, additionals, ..Packet::default() };
                            if p.questions.iter().any(|q| q.unicast) {
                                let _ = r.sock.send_to(&encode(&reply), from).await;
                            }
                            r.multicast(&reply).await;
                        }
                    }
                }
            }
        });
        Ok(Dnssd { task, goodbye })
    }
}

/// An unsolicited announcement: every record of ours as an answer.
fn announcement(o: &(Rr, Rr, Rr, Vec<Rr>)) -> Packet {
    Packet { response: true, answers: [o.0.clone(), o.1.clone(), o.2.clone()].into_iter().chain(o.3.clone()).collect(), ..Packet::default() }
}

#[cfg(test)]
mod tests {
    use super::*;
    use iroh::SecretKey;

    fn data(addrs: &[&str], user: Option<&str>) -> EndpointData {
        let mut d = EndpointData::from(addrs.iter().map(|a| a.parse().unwrap()).collect::<BTreeSet<SocketAddr>>());
        d.set_user_data(user.map(|u| u.parse().unwrap()));
        d
    }

    #[test]
    fn wire_round_trip() {
        let id = SecretKey::generate().public();
        let o = records(&id, &data(&["192.168.1.4:4000", "[fe80::1]:4000", "[2001:db8::2]:4001"], Some("pb1{}")), TTL, 1).unwrap();
        let p = announcement(&o);
        let q = parse(&encode(&p)).unwrap();
        assert_eq!(q, p);
        // the IPv6 address on the other port is left out; the SRV is the IPv4 port's
        assert_eq!(o.3.len(), 2);
        assert_eq!(o.1.data, Data::Srv { port: 4000, target: format!("{}.local", lan::label(&id)) });
    }

    /// A browse answer as mDNSResponder sends one: PTR answer, SRV/TXT/A additionals, compressed names.
    #[test]
    fn hears_a_compressed_browse_answer() {
        let id = SecretKey::generate().public();
        let l = lan::label(&id);
        let mut b = vec![0, 0, 0x84, 0, 0, 0, 0, 1, 0, 0, 0, 3];
        // answer: _pbn._udp.local PTR <l>._pbn._udp.local
        let svc = b.len();
        b.extend_from_slice(b"\x04_pbn\x04_udp\x05local\x00");
        b.extend_from_slice(&[0, 12, 0, 1, 0, 0, 0x11, 0x94]);
        let inst = b.len() + 2;
        let mut rd = vec![l.len() as u8];
        rd.extend_from_slice(l.as_bytes());
        rd.extend_from_slice(&[0xc0, svc as u8]);
        b.extend_from_slice(&(rd.len() as u16).to_be_bytes());
        b.extend_from_slice(&rd);
        // SRV <inst> -> iPhone.local:4123 (flush)
        b.extend_from_slice(&[0xc0, inst as u8, 0, 33, 0x80, 1, 0, 0, 0, 120]);
        let host_at = b.len() + 2 + 6;
        let mut rd = vec![0, 0, 0, 0, 0x10, 0x1b, 6];
        rd.extend_from_slice(b"iPhone");
        rd.extend_from_slice(&[0xc0, (svc + 10) as u8]);
        b.extend_from_slice(&(rd.len() as u16).to_be_bytes());
        b.extend_from_slice(&rd);
        // TXT <inst> user-data=pb1x
        b.extend_from_slice(&[0xc0, inst as u8, 0, 16, 0x80, 1, 0, 0, 0x11, 0x94]);
        let t = b"\x0euser-data=pb1x";
        b.extend_from_slice(&(t.len() as u16).to_be_bytes());
        b.extend_from_slice(t);
        // A iPhone.local 192.168.1.9
        b.extend_from_slice(&[0xc0, host_at as u8, 0, 1, 0x80, 1, 0, 0, 0, 120, 0, 4, 192, 168, 1, 9]);
        let p = parse(&b).unwrap();
        assert_eq!(p.answers[0].data, Data::Ptr(format!("{l}._pbn._udp.local")));
        assert_eq!(p.additionals[0].data, Data::Srv { port: 4123, target: "iphone.local".into() });

        let book = Book::default();
        let mut h = Cache::default();
        let me = SecretKey::generate().public();
        assert!(hear(&p, &me, &mut h, &book).is_empty());
        let got = book.get(&id).unwrap();
        assert_eq!(got.addrs, BTreeSet::from(["192.168.1.9:4123".parse().unwrap()]));
        assert_eq!(got.user_data.as_deref(), Some("pb1x"));
        // iroh's mDNS's records (TTL 0 always, its own SRV target) change nothing
        let swarm = |data| Rr { name: format!("{l}._pbn._udp.local"), ttl: 0, flush: false, data };
        let p2 = Packet {
            response: true,
            answers: vec![swarm(Data::Srv { port: 4123, target: format!("{l}-4123.local") }), swarm(Data::Txt(b"\x0euser-data=pb1y".to_vec()))],
            ..Packet::default()
        };
        hear(&p2, &me, &mut h, &book);
        assert_eq!(book.get(&id).map(|h| Heard { until: None, ..h }), Some(Heard { until: None, ..got }));
        // an older TXT of ours (a cache's leftover) never replaces a newer one
        let ours = |seq| Rr { name: format!("{l}._pbn._udp.local"), ttl: 120, flush: true, data: Data::Txt(lan::txt(&data(&[], Some(&format!("pb1s{seq}"))), seq)) };
        for (seq, want) in [(5, 5), (4, 5), (6, 6)] {
            hear(&Packet { response: true, answers: vec![ours(seq)], ..Packet::default() }, &me, &mut h, &book);
            assert_eq!(book.get(&id).unwrap().user_data, Some(format!("pb1s{want}")));
        }
        // a goodbye takes it off
        let bye = Packet { response: true, answers: vec![Rr { ttl: 0, ..p.answers[0].clone() }], ..Packet::default() };
        hear(&bye, &me, &mut h, &book);
        assert!(book.get(&id).is_none());
    }

    #[test]
    fn answers_browse_and_resolve() {
        let id = SecretKey::generate().public();
        let o = records(&id, &data(&["10.0.0.7:5000"], None), TTL, 1).unwrap();
        let q = |name: &str, qtype| vec![Question { name: name.into(), qtype, unicast: false }];
        let (an, ad) = answer(&q("_pbn._udp.local", PTR), &o).unwrap();
        assert_eq!((an.len(), ad.len()), (1, 3));
        let (an, ad) = answer(&q(&o.1.name, ANY), &o).unwrap();
        assert_eq!((an.len(), ad.len()), (2, 1));
        let Data::Srv { target, .. } = &o.1.data else { panic!() };
        assert_eq!(answer(&q(target, A), &o).unwrap().0, o.3);
        assert!(answer(&q(target, AAAA), &o).is_none());
        assert!(answer(&q("_other._udp.local", PTR), &o).is_none());
    }

    /// Truncated or looping names don't parse.
    #[test]
    fn bad_packets() {
        assert!(parse(&[0; 5]).is_none());
        let mut b = vec![0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0];
        b.extend_from_slice(&[0xc0, 12, 0, 12, 0, 1]);
        assert!(parse(&b).is_none());
    }

    /// Two of our sockets on one machine: one hears the other's records with real multicast, follows a TXT change and
    /// forgets it at its goodbye. Needs multicast (`cargo test -- --ignored dnssd`).
    #[test]
    #[ignore]
    fn dnssd_hears_dnssd() {
        let rt = tokio::runtime::Runtime::new().unwrap();
        rt.block_on(async {
            let (a, b) = (SecretKey::generate().public(), SecretKey::generate().public());
            let (pa, ra) = watch::channel(None);
            let (_pb, rb) = watch::channel(None);
            let book_b = Book::default();
            let da = Dnssd::start(a, Book::default(), ra).unwrap();
            let _db = Dnssd::start(b, book_b.clone(), rb).unwrap();
            let until = async |what: &str, f: &dyn Fn(Option<Heard>) -> bool| {
                for _ in 0..100 {
                    if f(book_b.get(&a)) {
                        return;
                    }
                    tokio::time::sleep(Duration::from_millis(100)).await;
                }
                panic!("{what}: not in 10 s: {:?}", book_b.get(&a));
            };
            pa.send_replace(Some(data(&["192.168.77.1:4444"], Some("pb1one"))));
            until("heard", &|h| h.is_some_and(|h| h.user_data.as_deref() == Some("pb1one") && h.addrs.len() == 1)).await;
            pa.send_replace(Some(data(&["192.168.77.1:4444"], Some("pb1two"))));
            until("TXT change", &|h| h.is_some_and(|h| h.user_data.as_deref() == Some("pb1two"))).await;
            drop(da);
            until("goodbye", &|h| h.is_none()).await;
        });
    }
}
