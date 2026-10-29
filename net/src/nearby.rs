//! N139a: lobbies on the same network for the browser's LAN tab. A page has no mDNS, so "the same network" is "the
//! same public IPv4 address": each side learns its own from STUN (the server-reflexive candidate of a WebRTC
//! description, `rtc.rs`), and the records go through the pkarr relay as N139's short codes do (`signal.rs`).
//!
//! - A WebRTC host (a browser, or a desktop's Host for Browsers) with a short code puts a record (its short code and
//!   the LAN listing `lobby_c.rs` gives it: name, match id, open seats) under one of [`SLOTS`] keys named after its
//!   address ([`name`]), every [`REPUBLISH`], and empties it when the lobby starts or closes.
//! - A browser's LAN tab reads those keys (one every [`BROWSE_EVERY`]: every tab and game on the address share the
//!   relay's ~0.5 requests a second) and lists the records written less than [`STALE`] ago; a tap joins the code.
//!
//! Strangers behind the same address (a carrier's NAT) show up too: the tab says "on your network", not "yours".

use std::net::Ipv4Addr;
use std::sync::{Arc, Mutex, Weak};
use std::time::Duration;

use crate::signal;

/// Hosts one address can list at once.
pub const SLOTS: u8 = 3;
/// A record older than this is left out (its host closed, or its page went away).
pub const STALE: Duration = Duration::from_secs(60);
/// How often a host puts its record again (well inside [`STALE`], a PUT can take 25 s).
pub const REPUBLISH: Duration = Duration::from_secs(20);
/// A browse reads one slot this often (after reading them all once at its start).
pub const BROWSE_EVERY: Duration = Duration::from_millis(if cfg!(test) { 50 } else { 6000 });
/// A failed put is tried again after this.
const RETRY: Duration = Duration::from_secs(15);

/// The record name of `slot` for the address `ip` (in `signal`'s key space: the short codes are 4 symbols, and
/// their answers `<code>/a`).
pub fn name(ip: Ipv4Addr, slot: u8) -> String {
    format!("n/{:08x}/{slot}", u32::from(ip))
}

/// The public IPv4 address among a description's candidates (`rtc::Sig::cands`): the first that is neither
/// private, loopback, link-local nor a carrier's shared range (browsers hide local addresses as `<uuid>.local`;
/// what is left is the STUN server's view of us, or a host address that is public).
pub fn public_v4(cands: &[(String, u16)]) -> Option<Ipv4Addr> {
    cands.iter().filter_map(|(a, _)| a.parse::<Ipv4Addr>().ok()).find(|ip| {
        let o = ip.octets();
        !(ip.is_private() || ip.is_loopback() || ip.is_link_local() || ip.is_unspecified() || ip.is_broadcast() || ip.is_multicast())
            && !(o[0] == 100 && (64..128).contains(&o[1]))
            && o[0] != 0
    })
}

/// A host's record: its LAN listing (a JSON object) with its short code as `code`. None if the listing isn't an
/// object.
pub fn record(code: &str, listing: &str) -> Option<String> {
    let mut m: serde_json::Map<String, serde_json::Value> = serde_json::from_str(listing).ok()?;
    m.insert("code".into(), code.into());
    serde_json::to_string(&m).ok()
}

/// A record read back: its short code, and the record (the listing with `code` in it). None for an empty or
/// foreign one.
pub fn parse(text: &str) -> Option<(String, String)> {
    let m: serde_json::Map<String, serde_json::Value> = serde_json::from_str(text).ok()?;
    let code = m.get("code")?.as_str()?;
    signal::is_short(code).then(|| (code.to_string(), text.to_string()))
}

/// A record's seq (unix µs) was written less than [`STALE`] ago.
fn fresh(seq: i64) -> bool {
    signal::unix_micros().saturating_sub(seq) <= STALE.as_micros() as i64
}

// ---------------------------------------------------------------- host

/// What a host has to list now (asked by [`host`] each round).
pub struct HostNow {
    /// Its short code (None while the relay hasn't taken one).
    pub code: Option<String>,
    /// The lobby's LAN listing (None once started, or while it isn't open).
    pub listing: Option<String>,
    /// Its public address, once a description had it.
    pub ip: Option<Ipv4Addr>,
}

/// A host's listing on its address: `now` says what to list, and None once the hosting is closed (the record is then
/// emptied and this ends).
pub async fn host(mut now: impl FnMut() -> Option<HostNow>) {
    use tokio::time::Instant;
    // the slot written, the text and when, its seq
    let mut at: Option<(Ipv4Addr, u8)> = None;
    let (mut text, mut put_at, mut seq) = (String::new(), None::<Instant>, 0i64);
    let mut next = Instant::now();
    loop {
        let Some(n) = now() else { break };
        let want = match (&n.code, &n.listing, n.ip) {
            (Some(c), Some(l), Some(ip)) => record(c, l).map(|r| (ip, c.clone(), r)),
            _ => None,
        };
        let t = Instant::now();
        match want {
            Some((ip, code, r)) if t >= next && (r != text || at.is_none_or(|(i, _)| i != ip) || put_at.is_none_or(|p| t.duration_since(p) >= REPUBLISH)) => {
                // the slot is still ours (two hosts may have taken it at once), else the first free one
                let mut slot = None;
                if let Some((i, s)) = at.filter(|(i, _)| *i == ip) {
                    if free(&signal::get(&name(i, s)).await, &code) {
                        slot = Some(s);
                    }
                }
                if slot.is_none() {
                    if let Some((i, s)) = at.take() {
                        // moved address: the old record isn't ours to keep up (a slot taken from us is left alone)
                        if i != ip {
                            let _ = signal::put(&name(i, s), "", seq).await;
                        }
                    }
                    for s in 0..SLOTS {
                        if free(&signal::get(&name(ip, s)).await, &code) {
                            slot = Some(s);
                            break;
                        }
                    }
                }
                let Some(s) = slot else {
                    eprintln!("pbnet: nearby: all {SLOTS} slots of this network are taken; not listed");
                    next = Instant::now() + REPUBLISH;
                    continue;
                };
                match signal::put(&name(ip, s), &r, seq).await {
                    Ok(q) => {
                        if at != Some((ip, s)) {
                            eprintln!("pbnet: nearby: lobby {code} listed for this network ({ip}, slot {s})");
                        }
                        (at, text, put_at, seq) = (Some((ip, s)), r, Some(t), q);
                    }
                    Err(e) => {
                        eprintln!("pbnet: nearby: lobby {code} not listed: {e}");
                        next = Instant::now() + RETRY;
                    }
                }
            }
            None if at.is_some() => unlist(&mut at, &mut text, &mut put_at, seq).await,
            _ => {}
        }
        tokio::time::sleep(Duration::from_millis(500)).await;
    }
    unlist(&mut at, &mut text, &mut put_at, seq).await;
}

/// Empties the host's record (it would go stale in [`STALE`] anyway).
async fn unlist(at: &mut Option<(Ipv4Addr, u8)>, text: &mut String, put_at: &mut Option<tokio::time::Instant>, seq: i64) {
    if let Some((ip, s)) = at.take() {
        match signal::put(&name(ip, s), "", seq).await {
            Ok(_) => eprintln!("pbnet: nearby: unlisted (slot {s})"),
            Err(e) => eprintln!("pbnet: nearby: couldn't unlist ({e}); it goes stale in {} s", STALE.as_secs()),
        }
    }
    text.clear();
    *put_at = None;
}

/// A slot read as `got` may be written by the host of `code`: empty, stale, foreign, or its own.
fn free(got: &Result<Option<(String, i64)>, String>, code: &str) -> bool {
    match got {
        Ok(None) => true,
        Ok(Some((t, seq))) => !fresh(*seq) || parse(t).is_none_or(|(c, _)| c == code),
        // the relay refuses for now: don't write over what may be someone's
        Err(_) => false,
    }
}

// ---------------------------------------------------------------- browse

/// What a browse found: read by the LAN tab ([`Found::games`]), written by [`browse`].
#[derive(Default)]
pub struct Found {
    inner: Mutex<FoundIn>,
}

#[derive(Default)]
struct FoundIn {
    /// The address couldn't be learnt: why.
    error: Option<String>,
    /// The address, once learnt.
    ip: Option<Ipv4Addr>,
    /// Each slot's record: code, record, seq.
    slots: [Option<(String, String, i64)>; SLOTS as usize],
    /// A join took the browse's endpoint: stop asking.
    stopped: bool,
}

impl Found {
    fn lock(&self) -> std::sync::MutexGuard<'_, FoundIn> {
        self.inner.lock().unwrap_or_else(|e| e.into_inner())
    }

    /// The lobbies listed for this address now (code, record), in slot order.
    pub fn games(&self) -> Vec<(String, String)> {
        self.lock().slots.iter().flatten().filter(|(_, _, seq)| fresh(*seq)).map(|(c, r, _)| (c.clone(), r.clone())).collect()
    }

    /// The browse can list lobbies (false once the address couldn't be learnt).
    pub fn works(&self) -> bool {
        self.lock().error.is_none()
    }

    /// Why it can't, if so.
    pub fn error(&self) -> Option<String> {
        self.lock().error.clone()
    }

    /// The address it lists for, once learnt.
    pub fn ip(&self) -> Option<Ipv4Addr> {
        self.lock().ip
    }

    /// Stops the browse (a join took its endpoint).
    pub fn stop(&self) {
        self.lock().stopped = true;
    }
}

/// Reads this address's slots into `found` until it is dropped or stopped. `ip` learns the address (Err: why not).
pub async fn browse(found: Weak<Found>, ip: impl std::future::Future<Output = Result<Ipv4Addr, String>>) {
    let ip = match ip.await {
        Ok(ip) => ip,
        Err(e) => {
            eprintln!("pbnet: nearby: no public address, so no lobbies on this network: {e}");
            if let Some(f) = found.upgrade() {
                f.lock().error = Some(e);
            }
            return;
        }
    };
    eprintln!("pbnet: nearby: looking for lobbies on this network ({ip})");
    match found.upgrade() {
        Some(f) => f.lock().ip = Some(ip),
        None => return,
    }
    for round in 0u64.. {
        let s = (round % SLOTS as u64) as u8;
        let got = signal::get(&name(ip, s)).await;
        {
            let Some(f) = found.upgrade() else { return };
            let mut g = f.lock();
            if g.stopped {
                return;
            }
            match got {
                Ok(Some((t, seq))) => g.slots[s as usize] = parse(&t).filter(|_| fresh(seq)).map(|(c, r)| (c, r, seq)),
                Ok(None) => g.slots[s as usize] = None,
                // keep what was read: it drops out by its age
                Err(e) => eprintln!("pbnet: nearby: {e}"),
            }
        }
        // the first round reads every slot at once (inside the relay's burst)
        if round + 1 >= SLOTS as u64 {
            tokio::time::sleep(BROWSE_EVERY).await;
        }
    }
}

/// A browse's state shared by its endpoint and its task.
pub fn new_found() -> (Arc<Found>, Weak<Found>) {
    let f = Arc::new(Found::default());
    let w = Arc::downgrade(&f);
    (f, w)
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn names_and_addresses() {
        assert_eq!(name(Ipv4Addr::new(203, 0, 113, 9), 2), "n/cb007109/2");
        // inside the key's 16 name bytes
        assert!(name(Ipv4Addr::new(255, 255, 255, 255), SLOTS - 1).len() <= 16);
        let c = |v: &[&str]| v.iter().map(|a| (a.to_string(), 1u16)).collect::<Vec<_>>();
        assert_eq!(public_v4(&c(&["1f2e3d4c-0000-4000-8000-000000000000.local", "192.168.1.5", "100.70.1.1", "203.0.113.9"])), Some(Ipv4Addr::new(203, 0, 113, 9)));
        assert_eq!(public_v4(&c(&["10.0.0.2", "fe80::1", "2001:db8::1"])), None);
    }

    #[test]
    fn records() {
        let r = record("K7MQ", r#"{"id":"m1","name":"Ann","open":2}"#).unwrap();
        assert_eq!(parse(&r), Some(("K7MQ".into(), r.clone())));
        assert_eq!(parse(""), None);
        assert_eq!(parse(r#"{"code":"rtcoXX"}"#), None);
        assert_eq!(record("K7MQ", "not json"), None);
    }

    /// Two hosts on one address take two slots, a browse lists both, and a host that closes is gone.
    #[test]
    fn hosts_listed_and_unlisted() {
        let rt = tokio::runtime::Builder::new_current_thread().enable_time().build().unwrap();
        rt.block_on(async {
            // an address of its own: other tests' records don't meet it
            let ip = Ipv4Addr::from(u32::from_str_radix(&crate::recover::token()[..8], 16).unwrap());
            let live = |code: &'static str, open: u8, on: Arc<Mutex<bool>>| {
                move || {
                    (*on.lock().unwrap()).then(|| HostNow {
                        code: Some(code.into()),
                        listing: Some(format!(r#"{{"id":"m","name":"{code}","open":{open}}}"#)),
                        ip: Some(ip),
                    })
                }
            };
            let (a_on, b_on) = (Arc::new(Mutex::new(true)), Arc::new(Mutex::new(true)));
            let a = tokio::spawn(host(live("AAAA", 3, a_on.clone())));
            tokio::time::sleep(Duration::from_millis(50)).await;
            let b = tokio::spawn(host(live("BBBB", 2, b_on.clone())));
            let (found, weak) = new_found();
            let browsing = tokio::spawn(browse(weak, async move { Ok(ip) }));
            let until = |f: Arc<Found>, want: Vec<&'static str>| async move {
                for _ in 0..300 {
                    let codes: Vec<String> = f.games().into_iter().map(|g| g.0).collect();
                    if codes == want {
                        return;
                    }
                    tokio::time::sleep(Duration::from_millis(100)).await;
                }
                panic!("never listed {want:?}: {:?}", f.games());
            };
            until(found.clone(), vec!["AAAA", "BBBB"]).await;
            assert_eq!(found.ip(), Some(ip));
            *a_on.lock().unwrap() = false;
            a.await.unwrap();
            until(found.clone(), vec!["BBBB"]).await;
            *b_on.lock().unwrap() = false;
            b.await.unwrap();
            until(found.clone(), vec![]).await;
            drop(found);
            tokio::time::timeout(Duration::from_secs(30), browsing).await.unwrap().unwrap();
        });
    }

    #[test]
    fn no_address_no_browse() {
        let rt = tokio::runtime::Builder::new_current_thread().enable_time().build().unwrap();
        rt.block_on(async {
            let (found, weak) = new_found();
            browse(weak, async { Err("no STUN".to_string()) }).await;
            assert!(!found.works());
            assert!(found.games().is_empty());
        });
    }
}
