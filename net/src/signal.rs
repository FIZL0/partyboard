//! N139: WebRTC signalling through a pkarr relay ([`PKARR`], the HTTPS gateway `codes.rs` already uses for the
//! desktop's short codes), so a browser guest joins with one short code and sends nothing back.
//!
//! - The host (a browser, or a desktop hosting for browsers) puts its offer (`rtco…`) under a short code
//!   ([`SYMBOLS`], the desktop's alphabet) as a signed pkarr record, under the same key a desktop puts its iroh ticket
//!   (`pb-lobby-code/1/<code>`): one code names either kind of lobby, and the record says which.
//! - A guest gets the offer, makes its answer (`rtca…`) and puts it under the code's answer key
//!   (`pb-lobby-code/1/<code>/a`). Anyone with the code may answer: the code is the secret, as on the desktop.
//! - The host polls the answer key ([`POLL`]) and takes each new answer as if it were pasted; its next offer goes
//!   under the code again.
//!
//! The relay limits each address to a burst of ~10 requests and ~1 every 2 s after it (measured 2026-10-28; 429 with
//! `retry-after` past it), shared by every tab and game on that address: hence [`POLL`] of 2 s, not 1.
//!
//! HTTP: reqwest on the desktop and phones, the page's `fetch` in the browser (polled, `src/port/web/pbnet_rtc.js`),
//! and an in-process relay in tests (no request leaves the machine).

use std::time::{Duration, SystemTime};

/// The short codes' symbols: as `codes.rs`'s (digits and capitals without 0/O and 1/I).
pub const SYMBOLS: &[u8; 32] = b"23456789ABCDEFGHJKLMNPQRSTUVWXYZ";
pub const SHORT: usize = 4;
/// A record older than this (seconds) is stale: its lobby is gone (as `codes.rs`'s).
pub const STALE: u64 = 150;
/// How often a host puts its offer again while it doesn't change (well inside [`STALE`]).
pub const REPUBLISH: Duration = Duration::from_secs(60);
/// How often a host asks for a guest's answer.
pub const POLL: Duration = Duration::from_secs(2);
/// The public pkarr relay.
pub const PKARR: &str = "https://relay.pkarr.org";
/// The biggest record a relay takes (BEP 44's `v`).
const RECORD_MAX: usize = 1000;
/// The longest one request may take (a relay's PUT waits for its DHT put: 1-20 s, now and then over 25).
#[cfg_attr(any(test, not(any(feature = "iroh", target_os = "emscripten"))), allow(dead_code))]
const REQUEST_CAP: Duration = Duration::from_secs(40);

pub fn is_short(code: &str) -> bool {
    code.len() == SHORT && code.bytes().all(|b| SYMBOLS.contains(&b))
}

/// The short code in typed or pasted text: the code, or a link's `?code=` (any case).
pub fn short_in(text: &str) -> Option<String> {
    let t = text.trim();
    let t = t.split_once("code=").map_or(t, |(_, c)| c.split(['&', '#']).next().unwrap_or("")).trim();
    let up = t.to_ascii_uppercase();
    is_short(&up).then_some(up)
}

/// A random short code.
pub fn new_short() -> String {
    let t = crate::recover::token();
    t.as_bytes().chunks(2).take(SHORT).map(|h| SYMBOLS[(u8::from_str_radix(std::str::from_utf8(h).unwrap_or("0"), 16).unwrap_or(0) & 31) as usize] as char).collect()
}

/// The answer key's name for `code`.
pub fn answer_name(code: &str) -> String {
    format!("{code}/a")
}

/// The signing key a record named `name` (a code, or [`answer_name`]) is written with: as `codes.rs`'s `code_key`
/// (the same seed, so the same public key).
fn key(name: &str) -> ed25519_dalek::SigningKey {
    let mut seed = [0u8; 32];
    let s = format!("pb-lobby-code/1/{name}");
    seed[..s.len().min(32)].copy_from_slice(&s.as_bytes()[..s.len().min(32)]);
    ed25519_dalek::SigningKey::from_bytes(&seed)
}

/// z-base-32, as pkarr names keys.
fn z32(b: &[u8]) -> String {
    const A: &[u8; 32] = b"ybndrfg8ejkmcpqxot1uwisza345h769";
    let (mut s, mut acc, mut bits) = (String::new(), 0u32, 0);
    for &x in b {
        acc = acc << 8 | x as u32;
        bits += 8;
        while bits >= 5 {
            bits -= 5;
            s.push(A[(acc >> bits) as usize & 31] as char);
        }
    }
    if bits > 0 {
        s.push(A[(acc << (5 - bits)) as usize & 31] as char);
    }
    s
}

fn url(name: &str) -> String {
    format!("{PKARR}/{}", z32(key(name).verifying_key().as_bytes()))
}

/// `text` as a DNS response with one TXT record `_pbn` (as `codes.rs`'s: a pkarr relay only takes DNS packets).
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

pub fn unix_micros() -> i64 {
    SystemTime::now().duration_since(SystemTime::UNIX_EPOCH).map_or(0, |d| d.as_micros() as i64)
}

/// Written less than [`STALE`] ago.
pub fn fresh(seq: i64) -> bool {
    unix_micros().saturating_sub(seq) <= STALE as i64 * 1_000_000
}

/// The relay's PUT body for `text` under `name` at `seq`: signature, seq (big-endian), the packet.
fn record(name: &str, text: &str, seq: i64) -> Result<Vec<u8>, String> {
    use ed25519_dalek::Signer;
    let v = dns_txt(text);
    if v.len() > RECORD_MAX {
        return Err(format!("the code is too long for the code server ({} bytes)", v.len()));
    }
    // BEP 44's signed form: bencoded seq and v
    let sig = key(name).sign(&[format!("3:seqi{seq}e1:v{}:", v.len()).into_bytes(), v.clone()].concat());
    Ok([sig.to_bytes().to_vec(), seq.to_be_bytes().to_vec(), v].concat())
}

/// A relay's refusals and errors in words a player can act on.
fn trouble(status: Option<u16>, err: &str) -> String {
    match status {
        Some(429) => "the code server (relay.pkarr.org) is refusing this network for now (too many requests)".into(),
        Some(s) => format!("the code server (relay.pkarr.org) answered HTTP {s}"),
        None => format!("couldn't reach the code server (relay.pkarr.org: {err})"),
    }
}

/// A refused request's wait: 2, 4, then 8 s.
fn pause(k: u32) -> Duration {
    Duration::from_secs(2 << k.min(2))
}

/// The record named `name` and its seq; Ok(None): there is none. Asked again after a 429 or a failed request
/// (three times at most).
pub async fn get(name: &str) -> Result<Option<(String, i64)>, String> {
    let mut why = String::new();
    for k in 0..3 {
        // the relay answers with max-age=300: the query makes each lookup a fresh request
        match http::get(&format!("{}?t={}", url(name), unix_micros())).await {
            Ok((404, _)) => return Ok(None),
            Ok((s, b)) if (200..300).contains(&s) => {
                let seq = b.get(64..72).and_then(|s| s.try_into().ok()).map(i64::from_be_bytes);
                return Ok(seq.and_then(|seq| Some((b.get(72..).and_then(txt_of)?, seq))));
            }
            Ok((429, _)) => why = trouble(Some(429), ""),
            Ok((s, _)) => return Err(trouble(Some(s), "")),
            // the relay's 429 has no CORS headers: in a browser it is a failed fetch
            Err(e) => why = trouble(None, &e),
        }
        if k < 2 {
            tokio::time::sleep(pause(k)).await;
        }
    }
    Err(why)
}

/// Puts `text` under `name` (seq: now, or past `after` should that be later); the seq written, or why not. A relay
/// that has a later record (another machine's clock is ahead) gets one past it.
pub async fn put(name: &str, text: &str, after: i64) -> Result<i64, String> {
    let mut seq = unix_micros().max(after + 1);
    let mut why = String::new();
    for k in 0..3 {
        match http::put(&url(name), record(name, text, seq)?).await {
            Ok(s) if (200..300).contains(&s) => return Ok(seq),
            // 409: older than the record it has
            Ok(409) => {
                if let Ok(Some((_, theirs))) = get(name).await {
                    seq = seq.max(theirs + 1);
                }
                why = trouble(Some(409), "");
                continue;
            }
            Ok(429) => why = trouble(Some(429), ""),
            Ok(s) => return Err(trouble(Some(s), "")),
            // as in `get`: maybe a 429 the browser couldn't read
            Err(e) => why = trouble(None, &e),
        }
        if k < 2 {
            tokio::time::sleep(pause(k)).await;
        }
    }
    Err(why)
}

/// A short code for a host's `text`: `code` if no fresh record has it, else a new one (five tries); put there.
pub async fn roll(text: &str) -> Result<(String, i64), String> {
    let mut code = new_short();
    for _ in 0..5 {
        match get(&code).await? {
            Some((_, seq)) if fresh(seq) => code = new_short(),
            _ => return Ok((code.clone(), put(&code, text, 0).await?)),
        }
    }
    Err("five codes in a row were taken".into())
}

// ---------------------------------------------------------------- HTTP

/// The relay's HTTP: `get` (status, body) and `put` (status), each [`REQUEST_CAP`] at most.
mod http {
    #[cfg(all(feature = "iroh", not(test)))]
    pub use reqwest_http::*;
    #[cfg(all(target_os = "emscripten", not(test)))]
    pub use page::*;
    #[cfg(any(test, all(not(feature = "iroh"), not(target_os = "emscripten"))))]
    pub use super::fake::{get, put};

    #[cfg(all(feature = "iroh", not(test)))]
    mod reqwest_http {
        use super::super::REQUEST_CAP;

        async fn send(r: reqwest::RequestBuilder) -> Result<(u16, Vec<u8>), String> {
            let ask = async {
                let r = r.send().await.map_err(|e| e.to_string())?;
                let s = r.status().as_u16();
                Ok((s, r.bytes().await.map_err(|e| e.to_string())?.to_vec()))
            };
            tokio::time::timeout(REQUEST_CAP, ask).await.unwrap_or_else(|_| Err(format!("no answer in {} s", REQUEST_CAP.as_secs())))
        }

        pub async fn get(url: &str) -> Result<(u16, Vec<u8>), String> {
            send(crate::codes::client().get(url)).await
        }

        pub async fn put(url: &str, body: Vec<u8>) -> Result<u16, String> {
            send(crate::codes::client().put(url).body(body)).await.map(|(s, _)| s)
        }
    }

    /// The page's `fetch` (`pbnet_rtc.js`), started and polled: no callbacks into wasm.
    #[cfg(all(target_os = "emscripten", not(test)))]
    mod page {
        use super::super::REQUEST_CAP;
        use std::ffi::{c_char, CString};
        use std::time::Duration;

        extern "C" {
            fn pbnet_js_http_start(method: *const c_char, url: *const c_char, body: *const u8, len: i32) -> i32;
            fn pbnet_js_http_poll(id: i32) -> i32;
            fn pbnet_js_http_body(id: i32, buf: *mut u8, cap: i32) -> i32;
            fn pbnet_js_http_drop(id: i32);
        }

        /// A request in flight, forgotten by the page when dropped.
        struct Req(i32);

        impl Drop for Req {
            fn drop(&mut self) {
                // SAFETY: takes a request number; one the page doesn't know is ignored.
                unsafe { pbnet_js_http_drop(self.0) }
            }
        }

        async fn send(method: &str, url: &str, body: &[u8]) -> Result<(u16, Vec<u8>), String> {
            let (m, u) = (CString::new(method).map_err(|e| e.to_string())?, CString::new(url).map_err(|e| e.to_string())?);
            // SAFETY: NUL-terminated strings and a buffer of `len` bytes, all living for the call; the page copies them.
            let req = Req(unsafe { pbnet_js_http_start(m.as_ptr(), u.as_ptr(), body.as_ptr(), body.len() as i32) });
            let until = tokio::time::Instant::now() + REQUEST_CAP;
            loop {
                // SAFETY: takes a request number only.
                match unsafe { pbnet_js_http_poll(req.0) } {
                    -2 if tokio::time::Instant::now() >= until => return Err(format!("no answer in {} s", REQUEST_CAP.as_secs())),
                    -2 => tokio::time::sleep(Duration::from_millis(20)).await,
                    -1 => return Err("the browser couldn't send the request (offline, or blocked)".into()),
                    s => {
                        let mut buf = vec![0u8; 2048];
                        loop {
                            // SAFETY: `buf` holds `cap` writable bytes for the call.
                            let n = unsafe { pbnet_js_http_body(req.0, buf.as_mut_ptr(), buf.len() as i32) };
                            if n < 0 {
                                return Err("the request went away".into());
                            }
                            if n as usize <= buf.len() {
                                buf.truncate(n as usize);
                                return Ok((s as u16, buf));
                            }
                            buf.resize(n as usize, 0);
                        }
                    }
                }
            }
        }

        pub async fn get(url: &str) -> Result<(u16, Vec<u8>), String> {
            send("GET", url, &[]).await
        }

        pub async fn put(url: &str, body: Vec<u8>) -> Result<u16, String> {
            send("PUT", url, &body).await.map(|(s, _)| s)
        }
    }
}

/// An in-process relay for tests (and a browser-transport build that is neither the page nor iroh): records by
/// URL, seq checked as the relay does.
#[cfg(any(test, all(not(feature = "iroh"), not(target_os = "emscripten"))))]
pub mod fake {
    use std::collections::BTreeMap;
    use std::sync::Mutex;

    static RECORDS: Mutex<BTreeMap<String, Vec<u8>>> = Mutex::new(BTreeMap::new());
    thread_local! {
        /// This thread's requests fail (a test's own: tests run side by side on their own threads).
        static DOWN: std::cell::Cell<bool> = const { std::cell::Cell::new(false) };
    }

    /// Every request from this thread fails from now on (`true`), as with no internet, or works again.
    pub fn set_down(down: bool) {
        DOWN.with(|d| d.set(down));
    }

    fn seq(b: &[u8]) -> i64 {
        b.get(64..72).and_then(|s| s.try_into().ok()).map_or(0, i64::from_be_bytes)
    }

    pub async fn get(url: &str) -> Result<(u16, Vec<u8>), String> {
        tokio::task::yield_now().await;
        if DOWN.with(|d| d.get()) {
            return Err("offline".into());
        }
        let k = url.split('?').next().unwrap_or("");
        Ok(RECORDS.lock().unwrap().get(k).map_or((404, Vec::new()), |b| (200, b.clone())))
    }

    pub async fn put(url: &str, body: Vec<u8>) -> Result<u16, String> {
        tokio::task::yield_now().await;
        if DOWN.with(|d| d.get()) {
            return Err("offline".into());
        }
        let mut g = RECORDS.lock().unwrap();
        if g.get(url).is_some_and(|b| seq(b) >= seq(&body)) {
            return Ok(409);
        }
        g.insert(url.to_string(), body);
        Ok(200)
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn short_codes() {
        for _ in 0..50 {
            assert!(is_short(&new_short()));
        }
        assert_eq!(short_in(" https://x/?code=k7mq&y=1 ").as_deref(), Some("K7MQ"));
        assert_eq!(short_in("rtcoAAAA"), None);
        assert!(!is_short("AB0D"));
    }

    #[test]
    fn z32_and_records() {
        assert_eq!(z32(&[0; 32]).len(), 52);
        assert_eq!(z32(&[0xff]), "9h");
        let r = record("ABCD", "rtcoxyz", 42).unwrap();
        assert_eq!(&r[64..72], &42i64.to_be_bytes());
        assert_eq!(txt_of(&r[72..]).as_deref(), Some("rtcoxyz"));
        assert!(record("ABCD", &"x".repeat(1200), 1).is_err());
        assert_ne!(url("ABCD"), url(&answer_name("ABCD")));
    }

    /// The key is `codes.rs`'s: a desktop's iroh short code and a WebRTC one share their names.
    #[cfg(feature = "iroh")]
    #[test]
    fn same_key_as_iroh_codes() {
        let mut seed = [0u8; 32];
        seed[..20].copy_from_slice(b"pb-lobby-code/1/ABCD");
        assert_eq!(z32(key("ABCD").verifying_key().as_bytes()), iroh::SecretKey::from_bytes(&seed).public().to_z32());
    }

    #[test]
    fn put_and_get_on_the_fake_relay() {
        let rt = tokio::runtime::Builder::new_current_thread().enable_time().build().unwrap();
        rt.block_on(async {
            let name = format!("{}/t", new_short());
            assert_eq!(get(&name).await, Ok(None));
            let s1 = put(&name, "one", 0).await.unwrap();
            assert_eq!(get(&name).await.unwrap(), Some(("one".into(), s1)));
            // a later seq than the record's, even from a clock behind it
            let s2 = put(&name, "two", s1 + 5_000_000).await.unwrap();
            assert!(s2 > s1);
            assert_eq!(get(&name).await.unwrap().map(|r| r.0).as_deref(), Some("two"));
        });
    }
}
