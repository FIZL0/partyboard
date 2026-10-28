//! N92: the browser build's transport, WebRTC data channels in place of iroh (`transport.rs`), as opentennis's
//! `hst-base/src/net/rtc.rs`. A browser can't send UDP and iroh/tokio's networking doesn't build for
//! `wasm32-unknown-emscripten`, so the peer connections live in the page's JavaScript (`src/port/web/pbnet_rtc.js`,
//! linked with `--js-library`) and this module drives them through a handful of polled imports ([`js`]); no callbacks
//! into wasm. The lobby, lockstep, saves and recovery above it are `net/src`'s own, on a current-thread tokio runtime
//! that the game's frames pump (`lobby_c::pump`).
//!
//! - **Codes, both ways** (no server: no signalling service, no STUN with the relay Off). The host's lobby code is
//!   an *offer* ([`OFFER`] + its essentials, base64url); a guest joins with it and its lobby shows an *answer*
//!   ([`ANSWER`]) to send back; the host pastes it (`pbnet_lobby_answer`) and the guest is in. The host then shows a
//!   fresh offer for the next guest. With the relay setting on, both sides also gather STUN (server-reflexive)
//!   candidates, so two browsers on different networks may connect when their NATs allow it; there is no TURN.
//! - A code is a description's essentials only ([`Sig`]: ICE ufrag and password, the DTLS fingerprint, the
//!   candidates, the peer name; an answer also the ufrag of the offer it answers): ~110 bytes instead of a ~600-byte
//!   SDP. [`Sig::sdp`] builds a minimal description back.
//! - On a connection ([`Peer`]): the lobby's JSON messages on a reliable, ordered data channel (id 0), the lockstep's
//!   datagrams on an unordered one with no retransmits (id 1), both negotiated up front, so a code carries nothing
//!   but ICE and DTLS. Same message and datagram contents as iroh's.
//! - No LAN discovery (mDNS isn't the page's to use).
//! - N98, mid-match redial: the offer a guest joined with is spent once it connected, so the host sends every guest a
//!   *spare* offer of its own over the connection ([`REDIAL`] + the code, not JSON, so the lobby never sees it). A
//!   guest whose connection dropped redials that one instead of its code; its answer goes back to the host as a new
//!   guest's would (the hold screen's Copy Answer / Paste Answer), and the host sends a new spare once it is back.

use std::marker::PhantomData;
use std::sync::{Arc, Mutex};
use std::time::Duration;

use serde::de::DeserializeOwned;
use serde::Serialize;
use tokio::sync::mpsc;

/// The longest message on the reliable channel.
pub const MSG_MAX: usize = 1 << 20;
/// The biggest datagram sent, as iroh's (a data channel message of up to 1100 bytes is one SCTP chunk).
pub const DATAGRAM_MAX: usize = 1100;
/// A host's code starts with this.
pub const OFFER: &str = "rtco";
/// A guest's answer starts with this.
pub const ANSWER: &str = "rtca";
/// How long a guest waits for the host to paste its answer.
const ANSWER_WAIT: Duration = Duration::from_secs(600);
/// How long a pasted answer has to connect.
const OPEN_WAIT: Duration = Duration::from_secs(30);
/// How often the connections' JavaScript queues are read (each frame's pump runs the timers that are due).
const POLL: Duration = Duration::from_millis(2);
/// N98: a control message on the reliable channel, from the host: the guest's spare offer for a redial.
const REDIAL: &str = "!redial ";
/// N98: the most spare offers the host keeps (one per guest; the oldest goes first).
const SPARES_MAX: usize = 8;
/// The public STUN server the relay setting `n0` (the default) uses.
const STUN: &str = "stun:stun.l.google.com:19302";

// ---------------------------------------------------------------- settings

/// The relay setting, as `transport.rs`'s; on the web it picks the ICE servers: [`STUN`], an own STUN/TURN URL, or
/// none (the local network only).
#[derive(Clone, Debug, PartialEq)]
pub enum Relay {
    N0,
    Own(String),
    Off,
}

impl Relay {
    /// The setting as written (`relay = n0 | own | off`, with `relay_url` for own); anything else is n0.
    pub fn parse(relay: &str, relay_url: &str) -> Relay {
        match relay.trim() {
            "off" => Relay::Off,
            "own" => Relay::Own(relay_url.trim().to_string()),
            _ => Relay::N0,
        }
    }

    pub fn name(&self) -> &'static str {
        match self {
            Relay::N0 => "n0",
            Relay::Own(_) => "own",
            Relay::Off => "off",
        }
    }

    /// `RTCConfiguration.iceServers` as JSON.
    fn ice_servers(&self) -> String {
        let urls: Vec<&str> = match self {
            Relay::N0 => vec![STUN],
            Relay::Own(u) if !u.is_empty() => vec![u.as_str()],
            _ => vec![],
        };
        serde_json::to_string(&urls.iter().map(|u| serde_json::json!({ "urls": u })).collect::<Vec<_>>()).unwrap_or_default()
    }
}

// ---------------------------------------------------------------- wire (as transport.rs)

/// Whether `data` fits in a datagram with a peer name in front.
pub fn fits(data: &[u8]) -> bool {
    data.len() + 65 <= DATAGRAM_MAX
}

/// A datagram on the wire: the other end's peer name (the sender's from the host, the receiver's to it), then the data.
pub fn datagram(peer: &str, data: &[u8]) -> Vec<u8> {
    let mut d = vec![peer.len() as u8];
    d.extend_from_slice(peer.as_bytes());
    d.extend_from_slice(data);
    d
}

/// A [`datagram`] read back: peer name and data.
pub fn undatagram(d: &[u8]) -> Option<(String, &[u8])> {
    let n = *d.first()? as usize;
    Some((String::from_utf8(d.get(1..1 + n)?.to_vec()).ok()?, d.get(1 + n..)?))
}

// ---------------------------------------------------------------- codes

const B64: &[u8; 64] = b"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";

fn b64(b: &[u8]) -> String {
    let mut s = String::new();
    for c in b.chunks(3) {
        let n = (c[0] as u32) << 16 | (*c.get(1).unwrap_or(&0) as u32) << 8 | *c.get(2).unwrap_or(&0) as u32;
        for i in 0..=c.len() {
            s.push(B64[(n >> (18 - 6 * i)) as usize & 63] as char);
        }
    }
    s
}

fn unb64(s: &str) -> Option<Vec<u8>> {
    let v: Vec<u32> = s.bytes().map(|c| B64.iter().position(|&x| x == c).map(|p| p as u32)).collect::<Option<_>>()?;
    if v.len() % 4 == 1 {
        return None;
    }
    let mut b = Vec::new();
    for c in v.chunks(4) {
        let n = c.iter().enumerate().fold(0, |n, (i, &x)| n | x << (18 - 6 * i));
        for i in 0..c.len() - 1 {
            b.push((n >> (16 - 8 * i)) as u8);
        }
    }
    Some(b)
}

/// A session description's essentials.
#[derive(Clone, Debug, Default, PartialEq)]
pub struct Sig {
    pub ufrag: String,
    pub pwd: String,
    /// The DTLS certificate's SHA-256.
    pub fingerprint: [u8; 32],
    /// UDP candidates (host, or server-reflexive with a STUN server): an IP address or an mDNS name (`<uuid>.local`,
    /// how browsers hide local addresses), port.
    pub cands: Vec<(String, u16)>,
    /// The peer name of its side ([`PeerId`]).
    pub id: [u8; 8],
    /// An answer: the ufrag of the offer it answers (empty in an offer).
    pub to: String,
}

impl Sig {
    /// The essentials of a browser's description (UDP candidates of component 1).
    pub fn from_sdp(sdp: &str, id: [u8; 8]) -> Option<Sig> {
        let mut s = Sig { id, ..Sig::default() };
        let mut fp = None;
        for l in sdp.lines().map(str::trim) {
            if let Some(v) = l.strip_prefix("a=ice-ufrag:") {
                s.ufrag = v.into();
            } else if let Some(v) = l.strip_prefix("a=ice-pwd:") {
                s.pwd = v.into();
            } else if let Some(v) = l.strip_prefix("a=fingerprint:sha-256 ") {
                let b: Vec<u8> = v.split(':').filter_map(|h| u8::from_str_radix(h, 16).ok()).collect();
                fp = b.try_into().ok();
            } else if let Some(v) = l.strip_prefix("a=candidate:") {
                let f: Vec<&str> = v.split_whitespace().collect();
                if f.len() >= 8 && f[1] == "1" && f[2].eq_ignore_ascii_case("udp") && matches!(f[7], "host" | "srflx" | "prflx") {
                    if let Ok(port) = f[5].parse() {
                        if !s.cands.iter().any(|(a, p)| a == f[4] && *p == port) {
                            s.cands.push((f[4].into(), port));
                        }
                    }
                }
            }
        }
        s.fingerprint = fp?;
        (!s.ufrag.is_empty() && !s.pwd.is_empty()).then_some(s)
    }

    /// A minimal data-channel-only description from the essentials (`offer`: setup actpass, else active). Every
    /// candidate is given as a host candidate: the type only orders the checks.
    pub fn sdp(&self, offer: bool) -> String {
        let fp: Vec<String> = self.fingerprint.iter().map(|b| format!("{b:02X}")).collect();
        let mut s = format!(
            "v=0\r\no=- 1 2 IN IP4 127.0.0.1\r\ns=-\r\nt=0 0\r\na=group:BUNDLE 0\r\nm=application 9 UDP/DTLS/SCTP webrtc-datachannel\r\n\
             c=IN IP4 0.0.0.0\r\na=mid:0\r\na=ice-ufrag:{}\r\na=ice-pwd:{}\r\na=fingerprint:sha-256 {}\r\na=setup:{}\r\n\
             a=sctp-port:5000\r\na=max-message-size:262144\r\n",
            self.ufrag,
            self.pwd,
            fp.join(":"),
            if offer { "actpass" } else { "active" }
        );
        for (i, (addr, port)) in self.cands.iter().enumerate() {
            s += &format!("a=candidate:{} 1 udp {} {addr} {port} typ host\r\n", i + 1, 2122260223 - i as u64);
        }
        s + "a=end-of-candidates\r\n"
    }

    /// The code: `kind` ([`OFFER`]/[`ANSWER`]) then the essentials, base64url.
    pub fn code(&self, kind: &str) -> String {
        let mut b = Vec::new();
        for t in [&self.ufrag, &self.pwd, &self.to] {
            b.push(t.len() as u8);
            b.extend_from_slice(t.as_bytes());
        }
        b.extend_from_slice(&self.id);
        b.extend_from_slice(&self.fingerprint);
        for (addr, port) in &self.cands {
            match (addr.parse::<std::net::IpAddr>(), uuid(addr)) {
                (Ok(std::net::IpAddr::V4(ip)), _) => b.extend([4].iter().chain(&ip.octets())),
                (Ok(std::net::IpAddr::V6(ip)), _) => b.extend([6].iter().chain(&ip.octets())),
                (_, Some(u)) => b.extend(b"m".iter().chain(&u)),
                _ => b.extend([0, addr.len() as u8].iter().chain(addr.as_bytes())),
            }
            b.extend_from_slice(&port.to_be_bytes());
        }
        format!("{kind}{}", b64(&b))
    }

    /// A code of `kind`, bare or in a link (`…?code=…`).
    pub fn parse(text: &str, kind: &str) -> Option<Sig> {
        let code = text.trim().rsplit("code=").next()?.split(['&', '#']).next()?.trim();
        let b = unb64(code.strip_prefix(kind)?)?;
        let mut r = b.as_slice();
        let mut take = |n: usize| -> Option<&[u8]> {
            if r.len() < n {
                return None;
            }
            let (a, rest) = r.split_at(n);
            r = rest;
            Some(a)
        };
        let mut text = || -> Option<String> {
            let n = take(1)?[0] as usize;
            String::from_utf8(take(n)?.to_vec()).ok()
        };
        let (ufrag, pwd, to) = (text()?, text()?, text()?);
        let id = take(8)?.try_into().ok()?;
        let fingerprint = take(32)?.try_into().ok()?;
        let mut cands = Vec::new();
        while let Some(&[tag]) = take(1) {
            let addr = match tag {
                4 => std::net::Ipv4Addr::from(<[u8; 4]>::try_from(take(4)?).ok()?).to_string(),
                6 => std::net::Ipv6Addr::from(<[u8; 16]>::try_from(take(16)?).ok()?).to_string(),
                b'm' => {
                    let h: String = take(16)?.iter().map(|b| format!("{b:02x}")).collect();
                    format!("{}-{}-{}-{}-{}.local", &h[..8], &h[8..12], &h[12..16], &h[16..20], &h[20..])
                }
                0 => {
                    let n = take(1)?[0] as usize;
                    String::from_utf8(take(n)?.to_vec()).ok()?
                }
                _ => return None,
            };
            cands.push((addr, u16::from_be_bytes(take(2)?.try_into().ok()?)));
        }
        Some(Sig { ufrag, pwd, fingerprint, cands, id, to })
    }
}

/// `<uuid>.local` as the uuid's 16 bytes.
fn uuid(name: &str) -> Option<[u8; 16]> {
    let h: String = name.strip_suffix(".local")?.split('-').collect();
    if h.len() != 32 || name.len() != 42 || h.chars().any(|c| !c.is_ascii_hexdigit() || c.is_ascii_uppercase()) {
        return None;
    }
    (0..16).map(|i| u8::from_str_radix(&h[2 * i..2 * i + 2], 16).ok()).collect::<Option<Vec<_>>>()?.try_into().ok()
}

/// The codes as `lobby_c` uses `codes.rs`'s.
pub mod codes {
    use super::{Sig, ANSWER, OFFER};
    use std::time::Duration;

    /// How often the host's lobby asks for its code ([`super::Transport::publish_code`]): a spent offer is
    /// replaced by a fresh one, so the code shown is the next guest's.
    pub const REPUBLISH: Duration = Duration::from_millis(500);

    /// `code` is a host's offer.
    pub fn is_code(code: &str) -> bool {
        Sig::parse(code, OFFER).is_some()
    }

    /// `code` is a guest's answer.
    pub fn is_answer(code: &str) -> bool {
        Sig::parse(code, ANSWER).is_some()
    }

    /// The join code in pasted or typed text: the code itself, or a link's `?code=`.
    pub fn code_in(text: &str) -> Option<String> {
        let t = text.trim();
        let t = t.split_once("code=").map_or(t, |(_, c)| c.split(['&', '#']).next().unwrap_or("")).trim();
        is_code(t).then(|| t.to_string())
    }
}

// ---------------------------------------------------------------- the page's peer connections

/// The page's `RTCPeerConnection`s (`src/port/web/pbnet_rtc.js`), by number. Every call returns at once: offers,
/// answers and arrivals are polled.
mod js {
    #[cfg(target_os = "emscripten")]
    mod imp {
        use std::ffi::c_char;
        extern "C" {
            pub fn pbnet_js_rtc_open(ice_json: *const c_char) -> i32;
            pub fn pbnet_js_rtc_offer(id: i32);
            pub fn pbnet_js_rtc_answer(id: i32, offer_sdp: *const c_char);
            pub fn pbnet_js_rtc_accept(id: i32, answer_sdp: *const c_char);
            pub fn pbnet_js_rtc_local(id: i32, buf: *mut c_char, cap: i32) -> i32;
            pub fn pbnet_js_rtc_state(id: i32) -> i32;
            pub fn pbnet_js_rtc_send(id: i32, chan: i32, data: *const u8, len: i32) -> i32;
            pub fn pbnet_js_rtc_recv(id: i32, chan: i32, buf: *mut u8, cap: i32) -> i32;
            pub fn pbnet_js_rtc_rtt(id: i32) -> i32;
            pub fn pbnet_js_rtc_close(id: i32);
        }
    }
    #[cfg(not(target_os = "emscripten"))]
    use super::fake as imp;
    use std::ffi::{c_char, CString};

    /// A connection's state.
    #[derive(Clone, Copy, Debug, PartialEq, Eq)]
    pub enum State {
        Connecting,
        Open,
        Closed,
    }

    // SAFETY (every call below): the imports take only what is passed here: NUL-terminated strings and buffers
    // that live for the call, with their true lengths; a connection number they don't know is ignored (state
    // Closed, nothing received). They never call back into wasm.

    /// A new peer connection with both channels, or None (the page has no WebRTC).
    pub fn open(ice_json: &str) -> Option<i32> {
        let c = CString::new(ice_json).ok()?;
        let id = unsafe { imp::pbnet_js_rtc_open(c.as_ptr()) };
        (id > 0).then_some(id)
    }

    /// Makes an offer and gathers its candidates ([`local`] has it).
    pub fn offer(id: i32) {
        unsafe { imp::pbnet_js_rtc_offer(id) }
    }

    /// Takes `offer`, makes the answer and gathers its candidates ([`local`] has it).
    pub fn answer(id: i32, offer: &str) {
        let c = CString::new(offer).unwrap_or_default();
        unsafe { imp::pbnet_js_rtc_answer(id, c.as_ptr()) }
    }

    /// Takes the guest's `answer`: the connection opens.
    pub fn accept(id: i32, answer: &str) {
        let c = CString::new(answer).unwrap_or_default();
        unsafe { imp::pbnet_js_rtc_accept(id, c.as_ptr()) }
    }

    /// This side's description once gathered (Ok(None) before), Err(why) if it couldn't be made.
    pub fn local(id: i32) -> Result<Option<String>, String> {
        let mut buf = vec![0u8; 4096];
        let n = unsafe { imp::pbnet_js_rtc_local(id, buf.as_mut_ptr() as *mut c_char, buf.len() as i32) };
        if n == 0 {
            return Ok(None);
        }
        if n < 0 || n as usize >= buf.len() {
            return Err("this browser couldn't make a WebRTC description".into());
        }
        buf.truncate(n as usize);
        Ok(Some(String::from_utf8_lossy(&buf).into_owned()))
    }

    pub fn state(id: i32) -> State {
        match unsafe { imp::pbnet_js_rtc_state(id) } {
            0 => State::Connecting,
            1 => State::Open,
            _ => State::Closed,
        }
    }

    /// Sends `data` on channel `chan` (0 reliable, 1 datagrams); false if it isn't open.
    pub fn send(id: i32, chan: i32, data: &[u8]) -> bool {
        unsafe { imp::pbnet_js_rtc_send(id, chan, data.as_ptr(), data.len() as i32) == 1 }
    }

    /// The next message that came on channel `chan`.
    pub fn recv(id: i32, chan: i32) -> Option<Vec<u8>> {
        let mut buf = vec![0u8; 2048];
        loop {
            let n = unsafe { imp::pbnet_js_rtc_recv(id, chan, buf.as_mut_ptr(), buf.len() as i32) };
            if n < 0 {
                return None;
            }
            if n as usize <= buf.len() {
                buf.truncate(n as usize);
                return Some(buf);
            }
            buf.resize(n as usize, 0);
        }
    }

    /// The round trip (ms) of the selected candidate pair, once measured.
    pub fn rtt(id: i32) -> Option<u32> {
        let r = unsafe { imp::pbnet_js_rtc_rtt(id) };
        (r >= 0).then_some(r as u32)
    }

    pub fn close(id: i32) {
        unsafe { imp::pbnet_js_rtc_close(id) }
    }
}

/// The page's peer connections faked in-process for the host's `cargo test` (two "browsers" in one process): an
/// answer accepted by an offer's side connects the two at once.
#[cfg(not(target_os = "emscripten"))]
#[allow(clippy::missing_safety_doc)]
mod fake {
    use std::collections::{BTreeMap, VecDeque};
    use std::ffi::{c_char, CStr};
    use std::sync::Mutex;

    #[derive(Default)]
    struct Pc {
        ufrag: String,
        local: Option<String>,
        /// The connection on the other side.
        other: Option<i32>,
        open: bool,
        closed: bool,
        queues: [VecDeque<Vec<u8>>; 2],
    }

    static PCS: Mutex<(i32, BTreeMap<i32, Pc>)> = Mutex::new((0, BTreeMap::new()));

    fn desc(ufrag: &str) -> String {
        let fp: Vec<String> = (0..32).map(|i| format!("{i:02X}")).collect();
        format!("v=0\r\na=ice-ufrag:{ufrag}\r\na=ice-pwd:pw{ufrag}pw{ufrag}pw\r\na=fingerprint:sha-256 {}\r\n\
                 a=candidate:1 1 udp 2122260223 192.168.1.2 50000 typ host\r\n", fp.join(":"))
    }

    unsafe fn s(p: *const c_char) -> String {
        unsafe { CStr::from_ptr(p) }.to_string_lossy().into_owned()
    }

    fn ufrag_of(sdp: &str) -> String {
        sdp.lines().find_map(|l| l.trim().strip_prefix("a=ice-ufrag:")).unwrap_or("").to_string()
    }

    pub unsafe fn pbnet_js_rtc_open(_: *const c_char) -> i32 {
        let mut g = PCS.lock().unwrap();
        g.0 += 1;
        let id = g.0;
        g.1.insert(id, Pc { ufrag: format!("u{id:03}"), ..Pc::default() });
        id
    }

    pub unsafe fn pbnet_js_rtc_offer(id: i32) {
        if let Some(p) = PCS.lock().unwrap().1.get_mut(&id) {
            p.local = Some(desc(&p.ufrag));
        }
    }

    pub unsafe fn pbnet_js_rtc_answer(id: i32, offer: *const c_char) {
        let _ = unsafe { s(offer) };
        if let Some(p) = PCS.lock().unwrap().1.get_mut(&id) {
            p.local = Some(desc(&p.ufrag));
        }
    }

    pub unsafe fn pbnet_js_rtc_accept(id: i32, answer: *const c_char) {
        let theirs = ufrag_of(&unsafe { s(answer) });
        let mut g = PCS.lock().unwrap();
        let Some((&other, _)) = g.1.iter().find(|(_, p)| p.ufrag == theirs && !p.closed) else { return };
        for (a, b) in [(id, other), (other, id)] {
            if let Some(p) = g.1.get_mut(&a) {
                p.other = Some(b);
                p.open = true;
            }
        }
    }

    pub unsafe fn pbnet_js_rtc_local(id: i32, buf: *mut c_char, cap: i32) -> i32 {
        let g = PCS.lock().unwrap();
        let Some(l) = g.1.get(&id).and_then(|p| p.local.clone()) else { return 0 };
        if l.len() < cap as usize {
            // SAFETY: the caller's buffer holds `cap` bytes.
            unsafe {
                std::ptr::copy_nonoverlapping(l.as_ptr(), buf as *mut u8, l.len());
                *buf.add(l.len()) = 0;
            }
        }
        l.len() as i32
    }

    pub unsafe fn pbnet_js_rtc_state(id: i32) -> i32 {
        match PCS.lock().unwrap().1.get(&id) {
            Some(p) if p.closed => 2,
            Some(p) if p.open => 1,
            Some(_) => 0,
            None => 2,
        }
    }

    pub unsafe fn pbnet_js_rtc_send(id: i32, chan: i32, data: *const u8, len: i32) -> i32 {
        // SAFETY: the caller's `data` holds `len` bytes.
        let d = unsafe { std::slice::from_raw_parts(data, len as usize) }.to_vec();
        let mut g = PCS.lock().unwrap();
        let Some(other) = g.1.get(&id).filter(|p| p.open && !p.closed).and_then(|p| p.other) else { return 0 };
        match g.1.get_mut(&other) {
            Some(p) if !p.closed => {
                p.queues[chan as usize & 1].push_back(d);
                1
            }
            _ => 0,
        }
    }

    pub unsafe fn pbnet_js_rtc_recv(id: i32, chan: i32, buf: *mut u8, cap: i32) -> i32 {
        let mut g = PCS.lock().unwrap();
        let Some(p) = g.1.get_mut(&id) else { return -1 };
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

    pub unsafe fn pbnet_js_rtc_rtt(id: i32) -> i32 {
        if PCS.lock().unwrap().1.get(&id).is_some_and(|p| p.open) {
            20
        } else {
            -1
        }
    }

    /// N98: every open connection drops, as on a network change (both ends see it closed).
    pub fn cut_open() {
        for p in PCS.lock().unwrap().1.values_mut().filter(|p| p.open) {
            p.closed = true;
        }
    }

    pub unsafe fn pbnet_js_rtc_close(id: i32) {
        let mut g = PCS.lock().unwrap();
        let other = g.1.get(&id).and_then(|p| p.other);
        g.1.remove(&id);
        if let Some(p) = other.and_then(|o| g.1.get_mut(&o)) {
            p.closed = true;
        }
    }
}

// ---------------------------------------------------------------- a connection

/// A peer connection, closed when the last handle goes.
struct Conn(i32);

impl Drop for Conn {
    fn drop(&mut self) {
        js::close(self.0);
    }
}

impl Conn {
    fn new(relay: &Relay) -> Result<Conn, String> {
        js::open(&relay.ice_servers()).map(Conn).ok_or_else(|| "This browser has no WebRTC, so it can't play online.".into())
    }

    /// This side's essentials once its description is gathered.
    async fn local(&self, id: [u8; 8]) -> Result<Sig, String> {
        loop {
            if let Some(sdp) = js::local(self.0)? {
                let sig = Sig::from_sdp(&sdp, id).ok_or("this browser's description has no ICE/DTLS details")?;
                if sig.cands.is_empty() {
                    return Err("No network address: join a network first.".into());
                }
                return Ok(sig);
            }
            tokio::time::sleep(Duration::from_millis(20)).await;
        }
    }

    /// Waits for both channels to open (Err: the connection failed, or `wait` passed).
    async fn open(&self, wait: Duration) -> Result<(), String> {
        let until = tokio::time::Instant::now() + wait;
        loop {
            match js::state(self.0) {
                js::State::Open => return Ok(()),
                js::State::Closed => return Err("Couldn't connect to the other player.".into()),
                js::State::Connecting if tokio::time::Instant::now() >= until => {
                    return Err("Couldn't connect to the other player (timed out).".into())
                }
                js::State::Connecting => tokio::time::sleep(Duration::from_millis(20)).await,
            }
        }
    }
}

/// A peer name: 8 random bytes, in hex.
#[derive(Clone, Copy, Debug, PartialEq, Eq, PartialOrd, Ord)]
pub struct PeerId([u8; 8]);

impl std::fmt::Display for PeerId {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        self.0.iter().try_for_each(|b| write!(f, "{b:02x}"))
    }
}

impl PeerId {
    fn random() -> PeerId {
        let t = crate::recover::token();
        let mut b = [0u8; 8];
        for (i, x) in b.iter_mut().enumerate() {
            *x = u8::from_str_radix(&t[2 * i..2 * i + 2], 16).unwrap_or(0);
        }
        PeerId(b)
    }
}

/// What [`Peer::recv_any`] got.
pub enum Incoming<T> {
    Msg(Result<T, String>),
    Datagram(Vec<u8>),
}

/// One peer connection: messages of type `T` on the reliable channel, datagrams on the other. A task reads the
/// page's queues into the receivers below.
pub struct Peer<T> {
    conn: Arc<Conn>,
    remote: PeerId,
    msgs: mpsc::UnboundedReceiver<Result<T, String>>,
    dgrams: mpsc::UnboundedReceiver<Vec<u8>>,
    _t: PhantomData<fn() -> T>,
}

impl<T: Serialize + DeserializeOwned + Send + 'static> Peer<T> {
    /// `spare` (a guest's): where the host's spare offer goes ([`REDIAL`]).
    fn new(conn: Conn, remote: PeerId, spare: Option<Arc<Mutex<Option<Sig>>>>) -> Peer<T> {
        let conn = Arc::new(conn);
        let (mt, msgs) = mpsc::unbounded_channel();
        let (dt, dgrams) = mpsc::unbounded_channel();
        let c = conn.clone();
        tokio::spawn(async move {
            loop {
                while let Some(d) = js::recv(c.0, 1) {
                    let _ = dt.send(d);
                }
                while let Some(m) = js::recv(c.0, 0) {
                    // N98: the host's spare offer, for this guest's redial
                    if let Some(code) = m.strip_prefix(REDIAL.as_bytes()) {
                        let sig = std::str::from_utf8(code).ok().and_then(|c| Sig::parse(c, OFFER)).filter(|s| s.id == remote.0);
                        if let (Some(sig), Some(spare)) = (sig, &spare) {
                            *locked(spare) = Some(sig);
                        }
                        continue;
                    }
                    let m = if m.len() > MSG_MAX { Err("message too long".into()) } else { serde_json::from_slice(&m).map_err(|e| e.to_string()) };
                    if mt.send(m).is_err() {
                        return;
                    }
                }
                if js::state(c.0) == js::State::Closed {
                    let _ = mt.send(Err("connection closed".into()));
                    return;
                }
                if mt.is_closed() {
                    return;
                }
                tokio::time::sleep(POLL).await;
            }
        });
        Peer { conn, remote, msgs, dgrams, _t: PhantomData }
    }

    /// The other end's peer name.
    pub fn id(&self) -> PeerId {
        self.remote
    }

    /// Sends `m` on the reliable channel (lost only if the connection is gone, or `m` can't be JSON).
    pub fn send(&self, m: T) {
        match serde_json::to_vec(&m) {
            Ok(b) => drop(js::send(self.conn.0, 0, &b)),
            Err(e) => eprintln!("pbnet: message not sent: {e}"),
        }
    }

    /// The next message; Err once the connection ended (why).
    pub async fn recv(&mut self) -> Result<T, String> {
        self.msgs.recv().await.unwrap_or_else(|| Err("connection closed".into()))
    }

    /// Sends a datagram as is; one that doesn't fit ([`DATAGRAM_MAX`]) or finds the connection gone is lost.
    pub fn send_datagram(&self, d: Vec<u8>) {
        if d.len() <= DATAGRAM_MAX {
            js::send(self.conn.0, 1, &d);
        }
    }

    /// The next datagram; None once the connection is gone.
    pub async fn recv_datagram(&mut self) -> Option<Vec<u8>> {
        self.dgrams.recv().await
    }

    /// The next message or datagram, whichever comes first (both cancel-safe).
    pub async fn recv_any(&mut self) -> Incoming<T> {
        tokio::select! {
            m = self.msgs.recv() => Incoming::Msg(m.unwrap_or_else(|| Err("connection closed".into()))),
            Some(d) = self.dgrams.recv() => Incoming::Datagram(d),
        }
    }

    /// The current round-trip time estimate.
    pub fn rtt(&self) -> Option<Duration> {
        js::rtt(self.conn.0).map(|ms| Duration::from_millis(ms as u64))
    }

    pub fn close(&self, _why: &str) {
        js::close(self.conn.0);
    }
}

// ---------------------------------------------------------------- the endpoint

/// A LAN host (never heard on the web: no mDNS).
#[derive(Clone, Debug, PartialEq)]
pub struct LanHost {
    pub id: PeerId,
    pub listing: String,
}

/// The host's offer for the next guest.
struct Pending {
    conn: Conn,
    sig: Sig,
}

#[derive(Default)]
struct Hosting {
    /// The offer shown now (None once a guest's answer took it, until the next one is made).
    offer: Option<Pending>,
    /// N98: each connected guest's spare offer (sent to it), for its redial if the connection drops.
    spares: Vec<(PeerId, Pending)>,
    /// Guests whose answer was pasted, connecting: their connection, peer name and the time it was pasted.
    connecting: Vec<(Conn, PeerId, tokio::time::Instant)>,
    /// Why the last pasted answer didn't connect, or "" once one did; taken by the lobby ([`Transport::take_error`]).
    error: Option<String>,
    closed: bool,
}

/// The hosting transport, for `pbnet_lobby_answer`.
static HOSTING: Mutex<Option<Arc<Mutex<Hosting>>>> = Mutex::new(None);
/// Joining: this guest's answer, for the host (the lobby's snapshot `answer`), once made; empty when connected.
static ANSWERING: Mutex<String> = Mutex::new(String::new());

/// This browser on the network: its peer name and relay setting, and (hosting) its offers.
pub struct Transport {
    id: PeerId,
    relay: Relay,
    hosting: Arc<Mutex<Hosting>>,
    /// N98, joining: the spare offer the host sent on the last connection, which a redial answers.
    spare: Arc<Mutex<Option<Sig>>>,
}

fn locked<T>(m: &Mutex<T>) -> std::sync::MutexGuard<'_, T> {
    m.lock().unwrap_or_else(|e| e.into_inner())
}

impl Transport {
    pub async fn bind(relay: &Relay) -> Result<Transport, String> {
        Ok(Transport { id: PeerId::random(), relay: relay.clone(), hosting: Arc::default(), spare: Arc::default() })
    }

    /// This browser's peer name.
    pub fn id(&self) -> PeerId {
        self.id
    }

    pub fn has_lan(&self) -> bool {
        false
    }

    pub fn lan(&self) -> Vec<LanHost> {
        Vec::new()
    }

    pub fn set_lan_listing(&self, _listing: Option<&str>) -> Result<(), String> {
        Ok(())
    }

    /// The host's code now: its offer for the next guest (empty while it is being made).
    pub fn ticket(&self) -> String {
        locked(&self.hosting).offer.as_ref().map(|p| p.sig.code(OFFER)).unwrap_or_default()
    }

    /// Hosting: makes the first offer, and takes the guests' answers from now on.
    pub async fn ready(&self) {
        *locked(&HOSTING) = Some(self.hosting.clone());
        if let Err(e) = self.fresh_offer().await {
            locked(&self.hosting).error = Some(e);
        }
    }

    /// An offer for the next guest, unless one is shown.
    async fn fresh_offer(&self) -> Result<String, String> {
        if let Some(p) = &locked(&self.hosting).offer {
            return Ok(p.sig.code(OFFER));
        }
        let conn = Conn::new(&self.relay)?;
        js::offer(conn.0);
        let sig = conn.local(self.id.0).await?;
        let code = sig.code(OFFER);
        let mut h = locked(&self.hosting);
        if h.closed {
            return Err("closed".into());
        }
        h.offer = Some(Pending { conn, sig });
        Ok(code)
    }

    /// The host's code now (as `transport.rs`'s short code, asked for every [`codes::REPUBLISH`]): the shown offer,
    /// or a fresh one once a guest took it. Err: no offer could be made.
    pub async fn publish_code(&self, _first: Option<String>) -> Result<String, String> {
        self.fresh_offer().await
    }

    /// Hosting: why the last pasted answer didn't connect (Some("") once one did), if that changed since last asked.
    pub fn take_error(&self) -> Option<String> {
        locked(&self.hosting).error.take()
    }

    /// The next guest whose answer connected; None once the transport is closed.
    pub async fn accept<T: Serialize + DeserializeOwned + Send + 'static>(&self) -> Option<Peer<T>> {
        loop {
            {
                let mut h = locked(&self.hosting);
                if h.closed {
                    return None;
                }
                let now = tokio::time::Instant::now();
                let mut i = 0;
                while i < h.connecting.len() {
                    let (c, _, at) = &h.connecting[i];
                    match js::state(c.0) {
                        js::State::Open => {
                            let (c, id, _) = h.connecting.remove(i);
                            h.error = Some(String::new());
                            let p = Peer::new(c, id, None);
                            self.send_spare(id, p.conn.clone());
                            return Some(p);
                        }
                        js::State::Closed => {
                            h.connecting.remove(i);
                            h.error = Some("A player's answer didn't connect: send them the new code.".into());
                        }
                        js::State::Connecting if now.duration_since(*at) > OPEN_WAIT => {
                            h.connecting.remove(i);
                            h.error = Some("A player's connection timed out: send them the new code.".into());
                        }
                        js::State::Connecting => i += 1,
                    }
                }
            }
            tokio::time::sleep(Duration::from_millis(20)).await;
        }
    }

    /// N98: makes a spare offer for the guest `to` and sends it on `conn` (its new connection), replacing the one it
    /// had: if that connection drops, the guest's redial answers it.
    fn send_spare(&self, to: PeerId, conn: Arc<Conn>) {
        let (relay, me, hosting) = (self.relay.clone(), self.id, self.hosting.clone());
        tokio::spawn(async move {
            let Ok(spare) = Conn::new(&relay) else { return };
            js::offer(spare.0);
            let Ok(sig) = spare.local(me.0).await else { return };
            let code = sig.code(OFFER);
            {
                let mut h = locked(&hosting);
                if h.closed {
                    return;
                }
                h.spares.retain(|(g, _)| *g != to);
                if h.spares.len() >= SPARES_MAX {
                    h.spares.remove(0);
                }
                h.spares.push((to, Pending { conn: spare, sig }));
            }
            js::send(conn.0, 0, format!("{REDIAL}{code}").as_bytes());
        });
    }

    /// Joins the host whose offer is `code`: makes the answer (the lobby shows it, [`answer_code`]) and waits for
    /// the host to take it.
    pub async fn dial<T: Serialize + DeserializeOwned + Send + 'static>(&self, code: &str) -> Result<Peer<T>, String> {
        let mut offer = Sig::parse(code, OFFER).ok_or("That code isn't a lobby code.")?;
        // N98: a redial (the code's offer is spent) answers the spare offer this host sent
        if let Some(spare) = locked(&self.spare).as_ref().filter(|s| s.id == offer.id) {
            offer = spare.clone();
        }
        let conn = Conn::new(&self.relay)?;
        js::answer(conn.0, &offer.sdp(true));
        let mut sig = conn.local(self.id.0).await?;
        sig.to = offer.ufrag.clone();
        // shown until connected, or until this dial ends some other way (failed, or dropped by a leave)
        struct Shown;
        impl Drop for Shown {
            fn drop(&mut self) {
                locked(&ANSWERING).clear();
            }
        }
        *locked(&ANSWERING) = sig.code(ANSWER);
        let shown = Shown;
        let r = conn.open(ANSWER_WAIT).await;
        drop(shown);
        r.map_err(|e| if e.contains("timed out") { "The host didn't take your answer in time.".into() } else { e })?;
        Ok(Peer::new(conn, PeerId(offer.id), Some(self.spare.clone())))
    }

    /// Closes the offers and the connections still connecting.
    pub async fn close(&self) {
        let mut h = locked(&self.hosting);
        h.closed = true;
        h.offer = None;
        h.spares.clear();
        h.connecting.clear();
        drop(h);
        let mut g = locked(&HOSTING);
        if g.as_ref().is_some_and(|x| Arc::ptr_eq(x, &self.hosting)) {
            *g = None;
        }
    }
}

/// This guest's answer for the host while it waits for it (empty otherwise).
pub fn answer_code() -> String {
    locked(&ANSWERING).clone()
}

/// The host takes a guest's `answer` for the offer it shows. Err: why not (no lobby hosting, not an answer, or for
/// an older offer).
pub fn take_answer(answer: &str) -> Result<(), String> {
    let a = Sig::parse(answer, ANSWER).ok_or("That isn't a player's answer.")?;
    let h = locked(&HOSTING).clone().ok_or("Not hosting a lobby.")?;
    let mut h = locked(&h);
    if h.closed {
        return Err("Not hosting a lobby.".into());
    }
    let p = if let Some(p) = h.offer.take_if(|p| p.sig.ufrag == a.to) {
        p
    } else if let Some(i) = h.spares.iter().position(|(_, p)| p.sig.ufrag == a.to) {
        // N98: a guest's redial, from the same peer only (its seats are its rejoin token's anyway)
        if h.spares[i].0 != PeerId(a.id) {
            return Err("That answer is for another player's reconnect.".into());
        }
        h.spares.remove(i).1
    } else {
        return Err("That answer is for an older code: send the player the code shown now.".into());
    };
    js::accept(p.conn.0, &a.sdp(false));
    h.connecting.push((p.conn, PeerId(a.id), tokio::time::Instant::now()));
    Ok(())
}

#[cfg(test)]
mod tests {
    use super::*;

    fn sig() -> Sig {
        Sig {
            ufrag: "abCD".into(),
            pwd: "0123456789abcdefghijklmn".into(),
            fingerprint: [7; 32],
            cands: vec![("192.168.1.20".into(), 50000), ("fe80::1".into(), 5), ("0b6b7b3c-1a2b-4c5d-8e9f-001122334455.local".into(), 61000), ("odd".into(), 1)],
            id: [1, 2, 3, 4, 5, 6, 7, 8],
            to: String::new(),
        }
    }

    #[test]
    fn codes_round_trip() {
        let s = sig();
        let c = s.code(OFFER);
        assert!(c.starts_with(OFFER));
        assert_eq!(Sig::parse(&c, OFFER), Some(s.clone()));
        assert_eq!(Sig::parse(&format!("https://x.example/?code={c}&y=1"), OFFER), Some(s.clone()));
        assert_eq!(Sig::parse(&c, ANSWER), None);
        let mut a = s.clone();
        a.to = "xyZ9".into();
        assert_eq!(Sig::parse(&a.code(ANSWER), ANSWER), Some(a));
        assert!(codes::is_code(&c) && !codes::is_answer(&c));
        assert_eq!(codes::code_in(&format!("  {c}\n")), Some(c.clone()));
        assert_eq!(codes::code_in("ABCD"), None);
        for n in 0..10 {
            let b: Vec<u8> = (0..n).map(|i| (i * 37 + 5) as u8).collect();
            assert_eq!(unb64(&b64(&b)), Some(b));
        }
    }

    #[test]
    fn sdp_essentials_round_trip() {
        let s = sig();
        let back = Sig::from_sdp(&s.sdp(true), s.id).unwrap();
        assert_eq!(back, s);
        let chrome = "v=0\r\na=ice-ufrag:Qx1a\r\na=ice-pwd:abcdefghijklmnopqrstuvwx\r\na=fingerprint:sha-256 \
                      AA:BB:CC:DD:EE:FF:00:11:22:33:44:55:66:77:88:99:AA:BB:CC:DD:EE:FF:00:11:22:33:44:55:66:77:88:99\r\n\
                      a=candidate:1 1 udp 2113937151 3f0c.local 54321 typ host generation 0\r\n\
                      a=candidate:2 1 udp 1677729535 203.0.113.9 40000 typ srflx raddr 0.0.0.0 rport 0\r\n\
                      a=candidate:3 1 tcp 1518280447 10.0.0.2 9 typ host tcptype active\r\n";
        let c = Sig::from_sdp(chrome, [0; 8]).unwrap();
        assert_eq!(c.cands, vec![("3f0c.local".to_string(), 54321), ("203.0.113.9".to_string(), 40000)]);
    }

    #[test]
    fn ice_servers_by_relay_setting() {
        assert_eq!(Relay::Off.ice_servers(), "[]");
        assert!(Relay::N0.ice_servers().contains(STUN));
        assert!(Relay::parse("own", "turn:me.example:3478").ice_servers().contains("turn:me.example"));
    }

    /// Two lobbies in one process on the fake page: the host's code, the guest's answer pasted back, seated, ready,
    /// started; then the lockstep's datagrams go both ways on the started lobbies' wires. N98: the connection drops
    /// mid-match; the guest redials the spare offer the host sent it, the host takes that answer and the wires carry
    /// datagrams again.
    #[test]
    fn lobby_over_rtc_reaches_start() {
        use crate::lobby::Player;
        use crate::lobby_c::{Lobby, Phase, Setup, Starter, Wire};
        use crate::lockstep::{Config, Transport as _};

        fn setup(name: &str) -> Setup {
            serde_json::from_value(serde_json::json!({
                "build": "0.1.0 test", "disc": "GMPE01-00", "relay": "off", "id": "m1",
                "players": [Player { name: name.into(), key: format!("key-{name}") }],
            }))
            .unwrap()
        }
        fn catch() -> (Starter, std::sync::mpsc::Receiver<(Config, Wire)>) {
            let (tx, rx) = std::sync::mpsc::channel();
            let tx = Mutex::new(tx);
            (Arc::new(move |c, w| drop(tx.lock().unwrap().send((c, w)))), rx)
        }
        async fn until(what: &str, mut f: impl FnMut() -> bool) {
            for _ in 0..1000 {
                if f() {
                    return;
                }
                tokio::time::sleep(Duration::from_millis(10)).await;
            }
            panic!("timed out: {what}");
        }

        let rt = tokio::runtime::Builder::new_current_thread().enable_time().build().unwrap();
        rt.block_on(async {
            let h = tokio::runtime::Handle::current();
            let (hs, hr) = catch();
            let (gs, gr) = catch();
            let host = Lobby::host(&h, Transport::bind(&Relay::Off), setup("h"), hs);
            until("the host's code", || !host.snapshot().short.is_empty()).await;
            let code = host.snapshot().short;
            assert!(code.starts_with(OFFER));
            let guest = Lobby::join(&h, Transport::bind(&Relay::Off), setup("g"), &code, gs);
            until("the guest's answer", || !guest.snapshot().answer.is_empty()).await;
            let answer = guest.snapshot().answer;
            assert!(take_answer("rtcaAAAA").is_err());
            take_answer(&answer).unwrap();
            assert!(take_answer(&answer).is_err(), "an offer takes one answer");
            until("seated", || guest.snapshot().room.is_some_and(|r| r.seats.iter().flatten().count() == 2)).await;
            assert!(guest.snapshot().answer.is_empty());
            until("a fresh code", || host.snapshot().short != code && !host.snapshot().short.is_empty()).await;
            assert_eq!(host.snapshot().error, "");
            // a second guest that leaves while it waits for the host takes its answer down
            let (ls, _lr) = catch();
            let late = Lobby::join(&h, Transport::bind(&Relay::Off), setup("l"), &host.snapshot().short, ls);
            until("the second answer", || !late.snapshot().answer.is_empty()).await;
            late.leave();
            until("the answer gone", || answer_code().is_empty()).await;
            assert!(guest.ready(true));
            until("ready", || host.start(7).is_ok()).await;
            until("started", || guest.snapshot().state == Phase::Started).await;
            let (hc, mut hw) = hr.recv_timeout(Duration::from_secs(5)).unwrap();
            let (gc, mut gw) = gr.recv_timeout(Duration::from_secs(5)).unwrap();
            assert_eq!((hc.me, gc.me, hc.peers, gc.peers), (0, 1, 2, 2));
            hw.send(1, b"to guest");
            gw.send(0, b"to host");
            let mut got = (None, None);
            until("datagrams", || {
                got.0 = got.0.take().or_else(|| gw.recv());
                got.1 = got.1.take().or_else(|| hw.recv());
                got.0.is_some() && got.1.is_some()
            })
            .await;
            assert_eq!(got, (Some((0, b"to guest".to_vec())), Some((1, b"to host".to_vec()))));

            // N98: the connection drops mid-match (the only open one in this process: `late` left, spares never open)
            fake::cut_open();
            until("reconnecting", || guest.status().reconnecting).await;
            until("the redial's answer", || !guest.snapshot().answer.is_empty()).await;
            let redial = guest.snapshot().answer;
            let to = Sig::parse(&redial, ANSWER).unwrap().to;
            assert_ne!(to, Sig::parse(&code, OFFER).unwrap().ufrag, "a redial answers the spare offer, not the spent code");
            take_answer(&redial).unwrap();
            until("reconnected", || guest.snapshot().answer.is_empty() && !guest.status().reconnecting).await;
            let mut got = (None, None);
            until("datagrams after the redial", || {
                hw.send(1, b"again");
                gw.send(0, b"back");
                got.0 = got.0.take().or_else(|| gw.recv());
                got.1 = got.1.take().or_else(|| hw.recv());
                got.0.is_some() && got.1.is_some()
            })
            .await;
            assert_eq!(got, (Some((0, b"again".to_vec())), Some((1, b"back".to_vec()))));
        });
    }
}
