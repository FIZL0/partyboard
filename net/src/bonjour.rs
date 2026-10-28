//! N119: LAN lobbies on iOS: `_pbn._udp` registered and browsed through the system's mDNSResponder (dns_sd), which
//! an app may use with NSBonjourServices and the Local Network permission alone; raw multicast (iroh's mDNS) needs
//! Apple's restricted multicast entitlement. See `lan.rs`.
//!
//! One thread owns every DNSServiceRef: it polls their sockets and runs the callbacks, which fill the [`Book`].
//! - **Register**: instance `<id>` (base32 endpoint ID), port iroh's IPv4 port, TXT as iroh's mDNS writes it; a new
//!   TXT (the listing) updates the record, a new port registers again.
//! - **Browse**: each instance found is resolved (SRV target, port, TXT), and its host's A/AAAA and its TXT are kept
//!   under query, so a host's address and listing changes are heard; a browse remove forgets it.
//! - A broken connection (mDNSResponder restarted, the app suspended) starts everything again after
//!   [`RESTART_AFTER`].
//!
//! Built on iOS, and with the `bonjour` feature elsewhere against Avahi's dns_sd compatibility library (libdns_sd),
//! so a Linux test can stand in for the iPhone.

use std::collections::{BTreeSet, HashMap};
use std::ffi::{c_char, c_int, c_void, CStr, CString};
use std::net::{IpAddr, Ipv4Addr, Ipv6Addr, SocketAddr};
use std::sync::atomic::{AtomicBool, Ordering};
use std::sync::Arc;
use std::time::Duration;

use iroh::address_lookup::EndpointData;
use tokio::sync::watch;

use crate::lan::{self, Book, Heard};

/// How long a broken connection waits before everything starts again.
const RESTART_AFTER: Duration = Duration::from_secs(2);
/// How long a poll waits for the daemon (and so how soon a new listing or a stop is seen).
const POLL_MS: c_int = 200;

// ---------------------------------------------------------------- dns_sd.h

type Ref = *mut c_void;
type Flags = u32;
type Err = i32;

const ADD: Flags = 0x2;
const POLICY_DENIED: Err = -65570;
const CLASS_IN: u16 = 1;
const A: u16 = 1;
const TXT: u16 = 16;
const AAAA: u16 = 28;

type RegisterReply = extern "C" fn(Ref, Flags, Err, *const c_char, *const c_char, *const c_char, *mut c_void);
type BrowseReply = extern "C" fn(Ref, Flags, u32, Err, *const c_char, *const c_char, *const c_char, *mut c_void);
type ResolveReply = extern "C" fn(Ref, Flags, u32, Err, *const c_char, *const c_char, u16, u16, *const u8, *mut c_void);
type QueryReply = extern "C" fn(Ref, Flags, u32, Err, *const c_char, u16, u16, u16, *const c_void, u32, *mut c_void);

// iOS: in libSystem. Elsewhere: Avahi's libdns_sd (the `bonjour` feature, for tests).
#[cfg_attr(not(target_vendor = "apple"), link(name = "dns_sd"))]
extern "C" {
    fn DNSServiceRegister(
        r: *mut Ref,
        flags: Flags,
        ifindex: u32,
        name: *const c_char,
        regtype: *const c_char,
        domain: *const c_char,
        host: *const c_char,
        port: u16,
        txt_len: u16,
        txt: *const c_void,
        cb: Option<RegisterReply>,
        ctx: *mut c_void,
    ) -> Err;
    fn DNSServiceUpdateRecord(r: Ref, record: *mut c_void, flags: Flags, len: u16, data: *const c_void, ttl: u32) -> Err;
    fn DNSServiceBrowse(
        r: *mut Ref,
        flags: Flags,
        ifindex: u32,
        regtype: *const c_char,
        domain: *const c_char,
        cb: Option<BrowseReply>,
        ctx: *mut c_void,
    ) -> Err;
    fn DNSServiceResolve(
        r: *mut Ref,
        flags: Flags,
        ifindex: u32,
        name: *const c_char,
        regtype: *const c_char,
        domain: *const c_char,
        cb: Option<ResolveReply>,
        ctx: *mut c_void,
    ) -> Err;
    fn DNSServiceQueryRecord(
        r: *mut Ref,
        flags: Flags,
        ifindex: u32,
        fullname: *const c_char,
        rrtype: u16,
        rrclass: u16,
        cb: Option<QueryReply>,
        ctx: *mut c_void,
    ) -> Err;
    fn DNSServiceRefSockFD(r: Ref) -> c_int;
    fn DNSServiceProcessResult(r: Ref) -> Err;
    fn DNSServiceRefDeallocate(r: Ref);
}

// ---------------------------------------------------------------- the state the callbacks fill

/// What a per-instance callback gets as its context: the state and the instance it is about.
struct Ctx {
    st: *mut State,
    inst: String,
}

/// An instance being resolved and watched.
struct Peer {
    refs: Vec<Ref>,
    host: String,
    port: u16,
    ips: BTreeSet<IpAddr>,
    /// The TXT records heard and not removed, per interface, the last heard last (the newest is the highest sequence
    /// number: `lan.rs`). Only from the TXT query: a resolve's TXT can be older than what the query has said since.
    txts: Vec<(Vec<u8>, u32)>,
    /// Boxed: its address is the callbacks' context.
    ctx: Box<Ctx>,
}

enum Todo {
    Resolve(String, String),
    Forget(String),
    Host(String, String, u16),
}

struct State {
    book: Book,
    /// Our own instance name (browse hears it too).
    me: String,
    browse: Ref,
    reg: Ref,
    reg_port: u16,
    reg_txt: Vec<u8>,
    /// The TXT's sequence number (`lan.rs`).
    seq: u64,
    /// Interfaces each instance is seen on (browse adds and removes come per interface).
    seen: HashMap<String, BTreeSet<u32>>,
    peers: HashMap<String, Peer>,
    todo: Vec<Todo>,
    broken: bool,
}

fn text(p: *const c_char) -> String {
    if p.is_null() {
        return String::new();
    }
    // SAFETY: dns_sd passes NUL-terminated strings valid for the callback's duration
    unsafe { CStr::from_ptr(p) }.to_string_lossy().into_owned()
}

/// Browse results.
///
/// # Safety
/// Called by DNSServiceProcessResult on the bonjour thread with the `*mut State` given to DNSServiceBrowse, while no
/// other reference to the state is live; the strings are NUL-terminated (or null).
extern "C" fn browsed(_r: Ref, flags: Flags, ifindex: u32, err: Err, name: *const c_char, _t: *const c_char, domain: *const c_char, ctx: *mut c_void) {
    // SAFETY: see the function's contract
    let st = unsafe { &mut *(ctx as *mut State) };
    if err != 0 {
        if err == POLICY_DENIED {
            eprintln!("pbnet: Bonjour: Local Network access is off for Party Board (Settings > Privacy): no LAN lobbies");
        } else {
            eprintln!("pbnet: Bonjour: browse error {err}");
        }
        st.broken = err != POLICY_DENIED;
        return;
    }
    let name = text(name);
    if name.eq_ignore_ascii_case(&st.me) {
        return;
    }
    let ifs = st.seen.entry(name.clone()).or_default();
    if flags & ADD != 0 {
        if ifs.is_empty() {
            st.todo.push(Todo::Resolve(name, text(domain)));
        }
        ifs.insert(ifindex);
    } else {
        ifs.remove(&ifindex);
        if ifs.is_empty() {
            st.seen.remove(&name);
            st.todo.push(Todo::Forget(name));
        }
    }
}

/// Resolve results: the instance's host and port (its TXT comes from the TXT query).
///
/// # Safety
/// As [`browsed`], with the `*const Ctx` of the instance's [`Peer`] (alive while its refs are); `txt` points at
/// `txt_len` bytes.
extern "C" fn resolved(
    _r: Ref,
    _flags: Flags,
    _ifindex: u32,
    err: Err,
    _full: *const c_char,
    host: *const c_char,
    port: u16,
    _txt_len: u16,
    _txt: *const u8,
    ctx: *mut c_void,
) {
    // SAFETY: see the function's contract
    let c = unsafe { &*(ctx as *const Ctx) };
    let st = unsafe { &mut *c.st };
    if err != 0 {
        return;
    }
    st.todo.push(Todo::Host(c.inst.clone(), text(host), u16::from_be(port)));
}

/// Record query results: the host's addresses or the instance's TXT, added or removed.
///
/// # Safety
/// As [`resolved`]; `rdata` points at `len` bytes.
extern "C" fn queried(
    _r: Ref,
    flags: Flags,
    ifindex: u32,
    err: Err,
    _full: *const c_char,
    rrtype: u16,
    _class: u16,
    len: u16,
    rdata: *const c_void,
    _ttl: u32,
    ctx: *mut c_void,
) {
    // SAFETY: see the function's contract
    let c = unsafe { &*(ctx as *const Ctx) };
    let st = unsafe { &mut *c.st };
    if err != 0 || rdata.is_null() {
        return;
    }
    let d = unsafe { std::slice::from_raw_parts(rdata as *const u8, len as usize) };
    let Some(p) = st.peers.get_mut(&c.inst) else { return };
    let add = flags & ADD != 0;
    let ip: Option<IpAddr> = match (rrtype, d.len()) {
        (A, 4) => Some(Ipv4Addr::from(<[u8; 4]>::try_from(d).unwrap()).into()),
        (AAAA, 16) => Some(Ipv6Addr::from(<[u8; 16]>::try_from(d).unwrap()).into()),
        _ => None,
    };
    match (ip, rrtype) {
        (Some(ip), _) if add => drop(p.ips.insert(ip)),
        (Some(ip), _) => drop(p.ips.remove(&ip)),
        (None, TXT) if add => p.add_txt(d, ifindex),
        (None, TXT) => p.txts.retain(|t| (t.0.as_slice(), t.1) != (d, ifindex)),
        _ => return,
    }
    st.publish_peer(&c.inst.clone());
}

/// Registration results (a conflict or error is only logged: browsing still works).
///
/// # Safety
/// As [`browsed`] (the context isn't used).
extern "C" fn registered(_r: Ref, _flags: Flags, err: Err, name: *const c_char, _t: *const c_char, _d: *const c_char, _ctx: *mut c_void) {
    if err != 0 {
        eprintln!("pbnet: Bonjour: register {} failed: {err}", text(name));
    }
}

impl Peer {
    fn add_txt(&mut self, t: &[u8], ifindex: u32) {
        self.txts.retain(|x| (x.0.as_slice(), x.1) != (t, ifindex));
        self.txts.push((t.to_vec(), ifindex));
    }
}

fn cstr(s: &str) -> CString {
    CString::new(s.replace('\0', "")).unwrap()
}

impl State {
    /// The book entry for an instance from what is known of it.
    fn publish_peer(&mut self, inst: &str) {
        let (Some(p), Some(id)) = (self.peers.get(inst), lan::id_of(inst)) else { return };
        let lan::Txt { user_data, relay, .. } = lan::newest(p.txts.iter().map(|t| t.0.as_slice()));
        let addrs = p.ips.iter().map(|ip| SocketAddr::new(*ip, p.port)).collect();
        self.book.set(id, Heard { addrs, user_data, relay, until: None });
    }

    fn start_browse(&mut self) {
        let st = self as *mut State as *mut c_void;
        let t = cstr(&lan::service());
        let d = cstr("local.");
        // SAFETY: valid out-pointer and strings; the state outlives the ref (dropped in teardown first)
        let e = unsafe { DNSServiceBrowse(&mut self.browse, 0, 0, t.as_ptr(), d.as_ptr(), Some(browsed), st) };
        if e != 0 {
            eprintln!("pbnet: Bonjour: browse failed: {e}: no LAN lobbies on this device");
            self.browse = std::ptr::null_mut();
            self.broken = true;
        }
    }

    /// Registers this endpoint, or updates its TXT.
    fn register(&mut self, data: &EndpointData) {
        let addrs: Vec<SocketAddr> = data.ip_addrs().copied().collect();
        let Some(port) = addrs.iter().find(|a| a.is_ipv4()).or(addrs.first()).map(|a| a.port()) else { return };
        let mut txt = lan::txt(data, self.seq);
        if txt != self.reg_txt {
            self.seq += 1;
            txt = lan::txt(data, self.seq);
        }
        if !self.reg.is_null() && port == self.reg_port {
            if txt != self.reg_txt {
                // SAFETY: a live registration; null record = its TXT; the bytes are valid for the call
                let e = unsafe { DNSServiceUpdateRecord(self.reg, std::ptr::null_mut(), 0, txt.len() as u16, txt.as_ptr() as _, 0) };
                if e != 0 {
                    eprintln!("pbnet: Bonjour: TXT update failed: {e}");
                }
                self.reg_txt = txt;
            }
            return;
        }
        self.unregister();
        let (n, t) = (cstr(&self.me), cstr(&lan::service()));
        // SAFETY: valid out-pointer, strings and TXT bytes for the call; no context
        let e = unsafe {
            DNSServiceRegister(
                &mut self.reg,
                0,
                0,
                n.as_ptr(),
                t.as_ptr(),
                std::ptr::null(),
                std::ptr::null(),
                port.to_be(),
                txt.len() as u16,
                txt.as_ptr() as _,
                Some(registered),
                std::ptr::null_mut(),
            )
        };
        if e != 0 {
            eprintln!("pbnet: Bonjour: register failed: {e}");
            self.reg = std::ptr::null_mut();
            return;
        }
        (self.reg_port, self.reg_txt) = (port, txt);
    }

    fn unregister(&mut self) {
        if !self.reg.is_null() {
            // SAFETY: a live ref, deallocated once
            unsafe { DNSServiceRefDeallocate(self.reg) };
            self.reg = std::ptr::null_mut();
        }
    }

    fn forget(&mut self, inst: &str) {
        if let Some(p) = self.peers.remove(inst) {
            // SAFETY: live refs, deallocated once; their context (p.ctx) is dropped after
            p.refs.into_iter().for_each(|r| unsafe { DNSServiceRefDeallocate(r) });
        }
        if let Some(id) = lan::id_of(inst) {
            self.book.remove(&id);
        }
    }

    /// Runs what the callbacks asked for (they may not start or stop refs themselves).
    fn run_todo(&mut self) {
        let st = self as *mut State;
        for t in std::mem::take(&mut self.todo) {
            match t {
                Todo::Resolve(inst, domain) => {
                    if lan::id_of(&inst).is_none() || self.peers.contains_key(&inst) {
                        continue;
                    }
                    let ctx = Box::new(Ctx { st, inst: inst.clone() });
                    let mut p = Peer { refs: Vec::new(), host: String::new(), port: 0, ips: BTreeSet::new(), txts: Vec::new(), ctx };
                    let c = &*p.ctx as *const Ctx as *mut c_void;
                    let (n, t, d) = (cstr(&inst), cstr(&lan::service()), cstr(&domain));
                    let full = format!("{inst}.{}.{}", lan::service(), if domain.is_empty() { "local." } else { &domain });
                    let mut r = std::ptr::null_mut();
                    // SAFETY: valid out-pointer and strings; the context lives in the Peer as long as its refs
                    if unsafe { DNSServiceResolve(&mut r, 0, 0, n.as_ptr(), t.as_ptr(), d.as_ptr(), Some(resolved), c) } == 0 {
                        p.refs.push(r);
                    }
                    let mut r = std::ptr::null_mut();
                    let f = cstr(&full);
                    // SAFETY: as above
                    if unsafe { DNSServiceQueryRecord(&mut r, 0, 0, f.as_ptr(), TXT, CLASS_IN, Some(queried), c) } == 0 {
                        p.refs.push(r);
                    }
                    self.peers.insert(inst, p);
                }
                Todo::Forget(inst) => self.forget(&inst),
                Todo::Host(inst, host, port) => {
                    let Some(p) = self.peers.get_mut(&inst) else { continue };
                    p.port = port;
                    if p.host != host {
                        p.host = host.clone();
                        p.ips.clear();
                        let c = &*p.ctx as *const Ctx as *mut c_void;
                        let h = cstr(&host);
                        for ty in [A, AAAA] {
                            let mut r = std::ptr::null_mut();
                            // SAFETY: as in Resolve
                            if unsafe { DNSServiceQueryRecord(&mut r, 0, 0, h.as_ptr(), ty, CLASS_IN, Some(queried), c) } == 0 {
                                p.refs.push(r);
                            }
                        }
                    }
                    self.publish_peer(&inst);
                }
            }
        }
    }

    /// Every ref, with the instance it belongs to (None: the browse or the registration).
    fn refs(&self) -> Vec<(Ref, Option<String>)> {
        let main = [self.browse, self.reg].into_iter().filter(|r| !r.is_null()).map(|r| (r, None));
        main.chain(self.peers.iter().flat_map(|(i, p)| p.refs.iter().map(|r| (*r, Some(i.clone()))))).collect()
    }

    fn teardown(&mut self) {
        for i in self.peers.keys().cloned().collect::<Vec<_>>() {
            self.forget(&i);
        }
        self.seen.clear();
        self.todo.clear();
        if !self.browse.is_null() {
            // SAFETY: a live ref, deallocated once
            unsafe { DNSServiceRefDeallocate(self.browse) };
            self.browse = std::ptr::null_mut();
        }
        self.unregister();
    }
}

/// Registers and browses until dropped (deregistered then). `publish` has this endpoint's data from iroh.
pub struct Bonjour {
    stop: Arc<AtomicBool>,
}

impl Drop for Bonjour {
    fn drop(&mut self) {
        self.stop.store(true, Ordering::Relaxed);
    }
}

impl Bonjour {
    pub fn start(me: iroh::EndpointId, book: Book, mut publish: watch::Receiver<Option<EndpointData>>) -> Result<Bonjour, String> {
        let stop = Arc::new(AtomicBool::new(false));
        let s = stop.clone();
        let st = State {
            book,
            me: lan::label(&me),
            browse: std::ptr::null_mut(),
            reg: std::ptr::null_mut(),
            reg_port: 0,
            reg_txt: Vec::new(),
            seq: 0,
            seen: HashMap::new(),
            peers: HashMap::new(),
            todo: Vec::new(),
            broken: false,
        };
        let mut st = Box::new(st);
        st.start_browse();
        if st.browse.is_null() {
            return Err("browse failed".into());
        }
        // the refs and the state go to the thread together (as an address: a raw pointer isn't Send)
        let st = Box::into_raw(st) as usize;
        std::thread::Builder::new()
            .name("pbnet-bonjour".into())
            .spawn(move || {
                let st = st as *mut State;
                let mut last: Option<EndpointData> = None;
                while !s.load(Ordering::Relaxed) {
                    match publish.has_changed() {
                        Ok(true) => {
                            last = publish.borrow_and_update().clone();
                            // SAFETY: only this thread touches the state, and no callback runs now
                            if let Some(d) = &last {
                                unsafe { &mut *st }.register(d);
                            }
                        }
                        Ok(false) => {}
                        Err(_) => break,
                    }
                    // SAFETY: as above
                    let refs = unsafe { &*st }.refs();
                    // SAFETY: a live ref's socket
                    let mut fds: Vec<libc::pollfd> =
                        refs.iter().map(|(r, _)| libc::pollfd { fd: unsafe { DNSServiceRefSockFD(*r) }, events: libc::POLLIN, revents: 0 }).collect();
                    // SAFETY: fds is a valid array of fds.len() pollfds
                    unsafe { libc::poll(fds.as_mut_ptr(), fds.len() as _, POLL_MS) };
                    for ((r, inst), fd) in refs.iter().zip(&fds) {
                        if fd.revents == 0 {
                            continue;
                        }
                        // SAFETY: a live ref (refs are only deallocated in run_todo/teardown below); its callbacks
                        // take the state while nothing else holds it
                        let e = unsafe { DNSServiceProcessResult(*r) };
                        if e != 0 {
                            let st = unsafe { &mut *st };
                            match inst {
                                Some(i) => st.todo.push(Todo::Forget(i.clone())),
                                None => st.broken = true,
                            }
                        }
                    }
                    // SAFETY: as above
                    let state = unsafe { &mut *st };
                    state.run_todo();
                    if state.broken {
                        eprintln!("pbnet: Bonjour: connection broken, starting again");
                        state.teardown();
                        state.broken = false;
                        std::thread::sleep(RESTART_AFTER);
                        state.start_browse();
                        if let Some(d) = &last {
                            state.register(d);
                        }
                    }
                }
                // SAFETY: the thread owns the state (from Box::into_raw above); nothing uses it after
                let mut state = unsafe { Box::from_raw(st) };
                state.teardown();
            })
            .map_err(|e| e.to_string())?;
        Ok(Bonjour { stop })
    }
}
