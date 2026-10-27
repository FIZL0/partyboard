//! Join codes: a host's whole [`EndpointTicket`], a bare endpoint ID, or a [`SHORT`]-symbol code people can read out.
//!
//! Ported from opentennis `hst-base/src/net.rs` (*short codes*, B104). A short code names a well-known ed25519 key
//! (its seed is the code, [`code_key`]); the host puts its ticket under that key as a BEP 44 mutable item on the
//! mainline DHT (no salt; seq = unix µs), as a one-TXT-record DNS packet so the same item is also a pkarr record and a
//! pkarr relay ([`PKARR`], an HTTP gateway onto the same DHT items) serves it too. A host re-rolls a code someone wrote
//! less than [`STALE`] ago and republishes while its lobby is open; once it stops, the code goes stale and is free
//! again. Anyone may overwrite a code's item (the key is public): the joiner still dials the ticket's endpoint, which
//! QUIC authenticates.

use std::time::{Duration, SystemTime};

use iroh::{Endpoint, EndpointAddr, EndpointId, RelayUrl, SecretKey};
use iroh_tickets::endpoint::EndpointTicket;
use n0_mainline::{Dht, MutableItem, SigningKey};

/// The short codes' symbols: digits and capitals without the look-alikes 0/O and 1/I (32^4 ≈ 1M codes).
pub const SYMBOLS: &[u8; 32] = b"23456789ABCDEFGHJKLMNPQRSTUVWXYZ";
pub const SHORT: usize = 4;
/// A code's record older than this (seconds) is stale: its lobby is gone and the code free.
pub const STALE: u64 = 150;
/// How often a host puts its code's record again while its lobby is open (well inside [`STALE`]).
pub const REPUBLISH: Duration = Duration::from_secs(60);
/// The public pkarr relay codes are also put on and read from (the DHT's own lookup can miss a fresh put).
pub const PKARR: &str = "https://relay.pkarr.org";
/// The longest a host's code check and put may take before it counts as failed (the relay's put takes 5-20 s, a
/// taken code's lookup up to 15 s).
pub const ROLL_CAP: Duration = Duration::from_secs(60);

/// An own pkarr relay (`code_server` in the online settings), asked before [`PKARR`] when set; empty: none.
static CODE_SERVER: std::sync::RwLock<String> = std::sync::RwLock::new(String::new());
/// While [`CODE_SERVER`] failed recently (unix µs), [`PKARR`] alone is asked.
static OWN_DOWN_UNTIL: std::sync::atomic::AtomicI64 = std::sync::atomic::AtomicI64::new(0);
const OWN_PAUSE: i64 = 60_000_000;

/// Sets the own pkarr relay asked first for short codes (`""`: only [`PKARR`]).
pub fn set_code_server(url: &str) {
    *CODE_SERVER.write().unwrap() = url.trim().trim_end_matches('/').to_string();
}

fn own_url() -> Option<String> {
    Some(CODE_SERVER.read().unwrap().clone()).filter(|u| !u.is_empty())
}

pub fn is_short(code: &str) -> bool {
    code.len() == SHORT && code.bytes().all(|b| SYMBOLS.contains(&b))
}

/// `code` is a host's whole ticket.
pub fn is_ticket(code: &str) -> bool {
    code.parse::<EndpointTicket>().is_ok()
}

/// `code` is a join code: a host's ticket, a bare endpoint ID or a short code.
pub fn is_code(code: &str) -> bool {
    is_short(code) || is_ticket(code) || code.parse::<EndpointId>().is_ok()
}

/// The join code in pasted or typed text: the code itself, or a link's `?code=` (short codes in capitals).
pub fn code_in(text: &str) -> Option<String> {
    let t = text.trim();
    let t = t.split_once("code=").map_or(t, |(_, c)| c.split(['&', '#']).next().unwrap_or(""));
    let up = t.to_ascii_uppercase();
    if is_short(&up) {
        Some(up)
    } else {
        is_code(t).then(|| t.to_string())
    }
}

/// A random short code.
pub fn new_short() -> String {
    // a fresh key's bytes as the randomness (no rand dependency); 5 bits of each of the first 4
    SecretKey::generate().to_bytes()[..SHORT].iter().map(|b| SYMBOLS[(b & 31) as usize] as char).collect()
}

/// The (public) key a short code's record is written under.
fn code_key(code: &str) -> SecretKey {
    let mut seed = [0u8; 32];
    let s = format!("pb-lobby-code/1/{code}");
    seed[..s.len().min(32)].copy_from_slice(&s.as_bytes()[..s.len().min(32)]);
    SecretKey::from_bytes(&seed)
}

/// `text` as a DNS response with one TXT record `_pbn` (strings of ≤ 255 bytes): a pkarr relay only takes DNS packets.
fn dns_txt(text: &str) -> Vec<u8> {
    let mut rdata = Vec::new();
    for c in text.as_bytes().chunks(255) {
        rdata.push(c.len() as u8);
        rdata.extend(c);
    }
    let mut p = vec![0, 0, 0x84, 0, 0, 0, 0, 1, 0, 0, 0, 0, 4, b'_', b'p', b'b', b'n', 0, 0, 16, 0, 1, 0, 0, 1, 44];
    p.extend((rdata.len() as u16).to_be_bytes());
    p.extend(rdata);
    p
}

/// The first answer's TXT strings, joined (`dns_txt` read back).
fn txt_of(p: &[u8]) -> Option<String> {
    let mut i = 12;
    loop {
        match *p.get(i)? {
            0 => break i += 1,
            l if l >= 0xc0 => break i += 2,
            l => i += 1 + l as usize,
        }
    }
    let n = u16::from_be_bytes(p.get(i + 8..i + 10)?.try_into().ok()?) as usize;
    let mut r = p.get(i + 10..i + 10 + n)?;
    let mut out = Vec::new();
    while let Some((&l, rest)) = r.split_first() {
        out.extend(rest.get(..l as usize)?);
        r = &rest[l as usize..];
    }
    String::from_utf8(out).ok()
}

/// A relay's answer to a GET: signature, seq (big-endian), the packet.
fn relay_record(b: &[u8]) -> Option<(String, i64)> {
    let seq = b.get(64..72).and_then(|s| s.try_into().ok()).map(i64::from_be_bytes)?;
    Some((b.get(72..).and_then(txt_of)?, seq))
}

fn unix_micros() -> i64 {
    SystemTime::now().duration_since(SystemTime::UNIX_EPOCH).map_or(0, |d| d.as_micros() as i64)
}

/// Written less than [`STALE`] ago.
fn fresh(seq: i64) -> bool {
    unix_micros().saturating_sub(seq) <= STALE as i64 * 1_000_000
}

/// The ticket under `code` on the DHT and when it was written; Ok(None): nobody wrote it; Err: why it couldn't be read.
async fn lookup(dht: &Dht, code: &str) -> Result<Option<(String, i64)>, String> {
    let item = dht.get_mutable_most_recent(code_key(code).public().as_bytes(), None).await.map_err(|e| format!("the DHT: {e}"))?;
    Ok(item.and_then(|i| Some((txt_of(i.value())?, i.seq()))))
}

/// What a short code's lookup says, `secs` at most.
async fn lookup_for(dht: &Dht, code: &str, secs: u64) -> Result<Option<(String, i64)>, String> {
    let r = tokio::time::timeout(Duration::from_secs(secs), lookup(dht, code)).await;
    r.unwrap_or_else(|_| Err(format!("the DHT didn't answer in {secs} s")))
}

fn client() -> reqwest::Client {
    let _ = rustls::crypto::ring::default_provider().install_default();
    static C: std::sync::OnceLock<reqwest::Client> = std::sync::OnceLock::new();
    // N16: on Android reqwest's own verifier panics without its JNI init, so the TLS settings come from `android.rs`.
    #[cfg(target_os = "android")]
    return C
        .get_or_init(|| reqwest::Client::builder().tls_backend_preconfigured(crate::android::tls()).build().expect("the HTTP client"))
        .clone();
    #[cfg(not(target_os = "android"))]
    C.get_or_init(reqwest::Client::new).clone()
}

/// A pkarr relay's refusals and errors in words a player can act on.
fn relay_trouble(status: Option<u16>, err: &str) -> String {
    match status {
        Some(429) => "the code server (relay.pkarr.org) is refusing this network for now (too many requests): wait a minute and try again".into(),
        Some(s) => format!("the code server (relay.pkarr.org) answered HTTP {s}: try again in a minute"),
        None => format!("couldn't reach the code server (relay.pkarr.org: {err}): check the internet connection, and that no firewall or VPN blocks it"),
    }
}

/// A refused request's wait before asking the pkarr relay again: 2, 4, then 8 s, spread by up to 2 s.
fn relay_pause(k: u32) -> Duration {
    Duration::from_millis((2000 << k.min(2)) + SecretKey::generate().to_bytes()[0] as u64 * 8)
}

/// `ask` on the own code server with `secs` to answer: None (and it skipped for a minute) if it didn't.
async fn own<T>(secs: u64, ask: impl std::future::Future<Output = Result<T, String>>) -> Option<T> {
    use std::sync::atomic::Ordering::Relaxed;
    let url = own_url()?;
    if unix_micros() < OWN_DOWN_UNTIL.load(Relaxed) {
        return None;
    }
    let why = match tokio::time::timeout(Duration::from_secs(secs), ask).await {
        Ok(Ok(t)) => return Some(t),
        Ok(Err(e)) => e,
        Err(_) => format!("no answer in {secs} s"),
    };
    eprintln!("pbnet: {url}: {why}; {PKARR} for a minute");
    OWN_DOWN_UNTIL.store(unix_micros() + OWN_PAUSE, Relaxed);
    None
}

/// The record under `code` on the pkarr relays: Ok(None) if they have none.
pub async fn relay_get(code: &str) -> Result<Option<(String, i64)>, String> {
    let z32 = code_key(code).public().to_z32();
    let ours = own(8, async {
        let r = client().get(format!("{}/{z32}?t={}", own_url().unwrap_or_default(), unix_micros())).send().await.map_err(|e| e.to_string())?;
        match r.status().as_u16() {
            404 => Ok(None),
            s if (200..300).contains(&s) => Ok(relay_record(&r.bytes().await.map_err(|e| e.to_string())?)),
            s => Err(format!("HTTP {s}")),
        }
    });
    if let Some(r) = ours.await {
        return Ok(r);
    }
    let mut why = relay_trouble(Some(429), "");
    for k in 0..4 {
        // the relay answers with max-age=300: the query makes each lookup a fresh request
        let r = match client().get(format!("{PKARR}/{z32}?t={}", unix_micros())).send().await {
            Ok(r) => r,
            Err(e) => {
                why = relay_trouble(None, &e.to_string());
                tokio::time::sleep(relay_pause(k)).await;
                continue;
            }
        };
        match r.status().as_u16() {
            404 => return Ok(None),
            429 => {
                why = relay_trouble(Some(429), "");
                tokio::time::sleep(relay_pause(k)).await;
            }
            s if !(200..300).contains(&s) => return Err(relay_trouble(Some(s), "")),
            _ => {
                let b = r.bytes().await.map_err(|e| relay_trouble(None, &e.to_string()))?;
                return Ok(relay_record(&b));
            }
        }
    }
    Err(why)
}

/// Puts the code's record on a pkarr relay (which also puts it on the DHT); Err: why it didn't take it.
async fn relay_put(code: &str, ticket: &str) -> Result<(), String> {
    let key = code_key(code);
    let (v, seq) = (dns_txt(ticket), unix_micros());
    // BEP 44's signed form: bencoded seq and v
    let sig = key.sign(&[format!("3:seqi{seq}e1:v{}:", v.len()).into_bytes(), v.clone()].concat());
    let body = [sig.to_bytes().to_vec(), seq.to_be_bytes().to_vec(), v].concat();
    let z32 = key.public().to_z32();
    let ours = own(25, async {
        let r = client().put(format!("{}/{z32}", own_url().unwrap_or_default())).body(body.clone()).send().await.map_err(|e| e.to_string())?;
        let s = r.status().as_u16();
        if (200..300).contains(&s) {
            Ok(())
        } else {
            Err(format!("HTTP {s}"))
        }
    });
    if ours.await.is_some() {
        return Ok(());
    }
    let mut why = String::new();
    for k in 0..5 {
        let r = client().put(format!("{PKARR}/{z32}")).body(body.clone()).send().await;
        match r.map(|r| r.status().as_u16()) {
            Ok(s) if (200..300).contains(&s) => return Ok(()),
            Ok(s) => why = relay_trouble(Some(s), ""),
            Err(e) => why = relay_trouble(None, &e.to_string()),
        }
        eprintln!("pbnet: lobby code {code}: {why}");
        if !why.contains("too many") {
            break;
        }
        tokio::time::sleep(relay_pause(k)).await;
    }
    Err(why)
}

/// Put `ticket` under `code`: Ok once it went out (on the DHT, or the relay when the DHT put fails), else why not.
pub async fn publish(dht: &Dht, code: &str, ticket: &str) -> Result<(), String> {
    // the relay's own DHT lookup misses a fresh put now and then: it goes out alongside the DHT put, in the background
    // (not in tests: their DHT is a local testnet, and their codes must not reach the real one)
    let relay = (!cfg!(test)).then(|| {
        let (code, ticket) = (code.to_string(), ticket.to_string());
        tokio::spawn(async move { relay_put(&code, &ticket).await })
    });
    let item = MutableItem::new(&SigningKey::from_bytes(&code_key(code).to_bytes()), &dns_txt(ticket), unix_micros(), None);
    let put = dht.put_mutable(item, None).await.map_err(|e| eprintln!("pbnet: lobby code {code}: {e}")).is_ok();
    match relay {
        _ if put => Ok(()),
        Some(r) => r.await.map_err(|e| e.to_string())?,
        None => Err(format!("lobby code {code}: the DHT didn't take it")),
    }
}

/// A short code for the lobby with `ticket`, `code` first: a free one, published (Err: why the DHT or relay
/// didn't take it). Each code tried after a taken one goes to `show`.
pub async fn roll(dht: &Dht, mut code: String, ticket: &str, show: impl Fn(&str)) -> Result<String, String> {
    for _ in 0..5 {
        match tokio::time::timeout(Duration::from_secs(15), lookup(dht, &code)).await {
            Ok(Ok(Some((_, seq)))) if fresh(seq) => {
                code = new_short();
                show(&code);
            }
            _ => return publish(dht, &code, ticket).await.map(|_| code),
        }
    }
    Err("five codes in a row were taken".into())
}

/// The ticket the short code `code` stands for, if its lobby is open; else what the lookup found instead.
/// `relays`: also ask the pkarr relays (off: LAN and DHT only).
pub async fn resolve(dht: &Dht, code: &str, relays: bool) -> Result<String, String> {
    // asked again until 40 s are up, as a code just rolled can take a while to reach the DHT nodes this machine asks
    let until = tokio::time::Instant::now() + Duration::from_secs(40);
    loop {
        let left = until.saturating_duration_since(tokio::time::Instant::now()).as_secs().max(1);
        let mut r = lookup_for(dht, code, left).await;
        if relays && !matches!(&r, Ok(Some((_, seq))) if fresh(*seq)) && !cfg!(test) {
            if let Ok(Ok(Some(found))) = tokio::time::timeout(Duration::from_secs(15), relay_get(code)).await {
                r = Ok(Some(found));
            }
        }
        if let Ok(Some((t, seq))) = &r {
            if fresh(*seq) {
                return Ok(t.clone());
            }
        }
        // no lookup starts with under 20 s left, so the last one ends by itself and its reason is what's said
        if tokio::time::Instant::now() + Duration::from_secs(20) >= until {
            return Err(missed(code, r.map(|r| r.map(|(_, seq)| seq))));
        }
        tokio::time::sleep(Duration::from_secs(5)).await;
    }
}

/// Why the short code `code` led to no lobby, from its last lookup.
fn missed(code: &str, last: Result<Option<i64>, String>) -> String {
    match last {
        Ok(Some(seq)) => {
            let m = unix_micros().saturating_sub(seq) / 60_000_000;
            let ago = if m < 2 { "about a minute".into() } else if m < 120 { format!("{m} minutes") } else { "hours".into() };
            format!("The lobby {code} stopped answering {ago} ago: the host left it or their game was closed. Ask for the code their lobby shows now.")
        }
        Ok(None) => format!("No open lobby has the code {code}: check it against the host's screen."),
        Err(e) => format!("Couldn't look up {code}: {e}."),
    }
}

/// The ticket put under a short code: the endpoint's whole address if it fits a DHT item (1000 bytes), else its ID
/// and relay; `home` if the address has no relay just now.
pub fn code_ticket(ep: &Endpoint, home: Option<&RelayUrl>) -> String {
    let mut addr = ep.addr();
    if let Some(h) = home.filter(|_| addr.relay_urls().next().is_none()) {
        addr = addr.with_relay_url(h.clone());
    }
    let t = EndpointTicket::new(addr.clone()).to_string();
    if t.len() <= 900 {
        return t;
    }
    let mut a = EndpointAddr::new(addr.id);
    for u in addr.relay_urls() {
        a = a.with_relay_url(u.clone());
    }
    EndpointTicket::new(a).to_string()
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn short_codes() {
        for _ in 0..50 {
            let c = new_short();
            assert!(is_short(&c), "{c}");
            assert!(is_code(&c));
        }
        assert!(!is_short("ABC0"));
        assert!(!is_short("abcd"));
        assert!(!is_short("ABCDE"));
        assert_eq!(code_in(" https://x/?code=k7mq&y=1 ").as_deref(), Some("K7MQ"));
        assert_eq!(code_in("hello"), None);
        let id = SecretKey::generate().public().to_string();
        assert_eq!(code_in(&id), Some(id));
    }

    #[test]
    fn dns_txt_round_trips() {
        for n in [0, 1, 255, 256, 900] {
            let s: String = (0..n).map(|i| (b'a' + (i % 26) as u8) as char).collect();
            assert_eq!(txt_of(&dns_txt(&s)).as_deref(), Some(s.as_str()));
        }
        let mut rec = vec![0u8; 64];
        rec.extend(42i64.to_be_bytes());
        rec.extend(dns_txt("ticket"));
        assert_eq!(relay_record(&rec), Some(("ticket".into(), 42)));
        assert_eq!(relay_record(&rec[..70]), None);
    }

    #[test]
    fn code_keys_differ_and_are_stable() {
        assert_eq!(code_key("ABCD").public(), code_key("ABCD").public());
        assert_ne!(code_key("ABCD").public(), code_key("ABCE").public());
    }

    #[test]
    fn freshness() {
        assert!(fresh(unix_micros()));
        assert!(!fresh(unix_micros() - (STALE as i64 + 1) * 1_000_000));
    }
}
