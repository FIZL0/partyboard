//! N119: LAN discovery that an iPhone can take part in: standard DNS-SD for the service `_pbn._udp` (the name
//! iroh's mDNS uses, [`crate::transport::MDNS`]).
//!
//! iroh's mDNS (`iroh-mdns-address-lookup`, swarm-discovery) sends raw multicast, which an iOS 14+ app may only do with
//! Apple's restricted multicast entitlement, and its records aren't DNS-SD: no PTR record, so Bonjour's browse never
//! sees them. So beside it:
//! - on iOS, `bonjour.rs` registers and browses `_pbn._udp` through the system's mDNSResponder (dns_sd; the
//!   Info.plist's NSBonjourServices allows it, no entitlement);
//! - everywhere else, `dnssd.rs` answers and listens for the same records on its own multicast socket, so an iPhone's
//!   browse finds desktop hosts and the desktop hears an iPhone's registration.
//!
//! Both fill a [`Book`] of the endpoints heard, which iroh resolves bare IDs from ([`LanLookup`] is an
//! [`AddressLookup`]) and the LAN tab lists. The record, as iroh's mDNS writes it: instance `<id>._pbn._udp.local`
//! (`<id>` the endpoint ID in lowercase base32, [`label`]), SRV to iroh's UDP port, TXT `user-data=<listing>` and
//! `relay=<url>` ([`txt`]).
//!
//! But never byte for byte iroh's: swarm-discovery sends every record with TTL 0, which a DNS-SD cache takes as the
//! goodbye of an identical record. So our TXT starts with `sd=<n>` ([`SEQ`]) and our SRV target is `<id>.local`, not
//! its `<id>-<port>.local`.
//!
//! The `n` counts the endpoint's TXT changes: a cache can hold the old TXT beside the new one for a while (Avahi gives
//! a query started within a second of a change the old one and never removes it), so a reader takes the TXT with
//! the highest `n`.

use std::collections::{BTreeMap, BTreeSet};
use std::net::SocketAddr;
use std::sync::{Arc, Mutex};
use std::time::{Duration, Instant};

use iroh::address_lookup::{AddressLookup, EndpointData, Error, Item};
use iroh::endpoint_info::EndpointInfo;
use iroh::EndpointId;
use n0_future::boxed::BoxStream;
use n0_future::StreamExt;
use tokio::sync::watch;

/// The DNS-SD service type (with `.local` the browse domain).
pub fn service() -> String {
    format!("_{}._udp", crate::transport::MDNS)
}

/// An endpoint ID as the first label of its instance name (iroh's mDNS: base32 without padding, lowercase).
pub fn label(id: &EndpointId) -> String {
    data_encoding::BASE32_NOPAD.encode(id.as_bytes()).to_ascii_lowercase()
}

/// The endpoint ID a label names, if it is one.
pub fn id_of(label: &str) -> Option<EndpointId> {
    let b = data_encoding::BASE32_NOPAD.decode(label.to_ascii_uppercase().as_bytes()).ok()?;
    EndpointId::from_bytes(&b.try_into().ok()?).ok()
}

/// The key of our TXT's first string, the TXT's sequence number (see the module doc).
pub const SEQ: &str = "sd";
/// The TXT key iroh's mDNS puts the user data under.
const USER_DATA: &str = "user-data";
/// The TXT key iroh's mDNS puts the home relay under.
const RELAY: &str = "relay";

/// An endpoint's TXT record data (length-prefixed strings): sequence number `seq` ([`SEQ`]), its relay and user data.
pub fn txt(data: &EndpointData, seq: u64) -> Vec<u8> {
    let mut items = vec![format!("{SEQ}={seq}")];
    if let Some(r) = data.relay_urls().next() {
        items.push(format!("{RELAY}={r}"));
    }
    if let Some(u) = data.user_data() {
        items.push(format!("{USER_DATA}={u}"));
    }
    let mut out = Vec::new();
    for i in items.iter().filter(|i| i.len() <= 255) {
        out.push(i.len() as u8);
        out.extend_from_slice(i.as_bytes());
    }
    out
}

/// What a TXT record says (the first string with each key).
#[derive(Clone, Debug, Default, PartialEq)]
pub struct Txt {
    pub user_data: Option<String>,
    pub relay: Option<String>,
    /// Ours have one; iroh's mDNS's none.
    pub seq: Option<u64>,
}

/// Reads a TXT record's data.
pub fn untxt(rdata: &[u8]) -> Txt {
    let (mut user, mut relay, mut seq) = (None, None, None);
    let mut rest = rdata;
    while let Some((&n, r)) = rest.split_first() {
        let Some(s) = r.get(..n as usize) else { break };
        rest = &r[n as usize..];
        let Ok(s) = std::str::from_utf8(s) else { continue };
        match s.split_once('=') {
            Some((USER_DATA, v)) if user.is_none() => user = Some(v.to_string()),
            Some((RELAY, v)) if relay.is_none() => relay = Some(v.to_string()),
            Some((SEQ, v)) if seq.is_none() => seq = v.parse().ok(),
            _ => {}
        }
    }
    Txt { user_data: user, relay, seq }
}

/// The newest of the TXT records (data) a cache has for one instance: the highest sequence number, the last given of
/// equals.
pub fn newest<'a>(txts: impl IntoIterator<Item = &'a [u8]>) -> Txt {
    let all = txts.into_iter().map(untxt).enumerate();
    all.max_by_key(|(i, t)| (t.seq.unwrap_or(0), *i)).map(|x| x.1).unwrap_or_default()
}

/// An endpoint heard on the LAN.
#[derive(Clone, Debug, Default, PartialEq)]
pub struct Heard {
    pub addrs: BTreeSet<SocketAddr>,
    pub user_data: Option<String>,
    pub relay: Option<String>,
    /// When it is forgotten unless heard again (None: when its backend says it is gone).
    pub until: Option<Instant>,
}

/// The endpoints heard on the LAN now.
#[derive(Clone, Debug, Default)]
pub struct Book(Arc<Mutex<BTreeMap<EndpointId, Heard>>>);

impl Book {
    pub fn set(&self, id: EndpointId, h: Heard) {
        self.0.lock().unwrap().insert(id, h);
    }

    pub fn remove(&self, id: &EndpointId) {
        self.0.lock().unwrap().remove(id);
    }

    pub fn get(&self, id: &EndpointId) -> Option<Heard> {
        let mut b = self.0.lock().unwrap();
        b.retain(|_, h| h.until.is_none_or(|t| t > Instant::now()));
        b.get(id).cloned()
    }

    /// Every endpoint heard (and not forgotten) with its user data.
    pub fn user_data(&self) -> Vec<(EndpointId, String)> {
        let mut b = self.0.lock().unwrap();
        b.retain(|_, h| h.until.is_none_or(|t| t > Instant::now()));
        b.iter().filter_map(|(id, h)| Some((*id, h.user_data.clone()?))).collect()
    }
}

/// iroh's view of a [`Book`]: bare IDs resolve from it (waiting up to [`RESOLVE_WAIT`] for one not heard yet), and
/// this endpoint's addresses and user data go to the backend that advertises them.
#[derive(Clone, Debug)]
pub struct LanLookup {
    pub book: Book,
    pub publish: watch::Sender<Option<EndpointData>>,
}

/// How long a resolve waits for an endpoint to be heard (iroh's mDNS: 10 s).
const RESOLVE_WAIT: Duration = Duration::from_secs(10);
/// The provenance iroh logs for what we resolve.
const NAME: &str = "dnssd";

impl LanLookup {
    pub fn new() -> (LanLookup, watch::Receiver<Option<EndpointData>>) {
        let (publish, rx) = watch::channel(None);
        (LanLookup { book: Book::default(), publish }, rx)
    }
}

fn item(id: EndpointId, h: &Heard) -> Item {
    let mut data = EndpointData::from(h.addrs.clone());
    if let Some(r) = h.relay.as_ref().and_then(|r| r.parse().ok()) {
        data.add_relay_url(r);
    }
    data.set_user_data(h.user_data.as_ref().and_then(|u| u.parse().ok()));
    Item::new(EndpointInfo::from_parts(id, data), NAME, None)
}

impl AddressLookup for LanLookup {
    fn publish(&self, data: &EndpointData) {
        self.publish.send_replace(Some(data.clone()));
    }

    fn resolve(&self, id: EndpointId) -> Option<BoxStream<Result<Item, Error>>> {
        let book = self.book.clone();
        let found = async move {
            let deadline = Instant::now() + RESOLVE_WAIT;
            loop {
                if let Some(h) = book.get(&id).filter(|h| !h.addrs.is_empty()) {
                    return Some(Ok(item(id, &h)));
                }
                if Instant::now() > deadline {
                    return None;
                }
                tokio::time::sleep(Duration::from_millis(100)).await;
            }
        };
        Some(Box::pin(n0_future::stream::once_future(found).filter_map(|x| x)))
    }
}

/// Which DNS-SD backend advertises and listens.
#[derive(Clone, Copy, Debug, PartialEq)]
pub enum Kind {
    /// Our own multicast socket (`dnssd.rs`), beside iroh's mDNS.
    #[cfg(not(target_os = "ios"))]
    Multicast,
    /// The system's mDNSResponder (`bonjour.rs`), instead of iroh's mDNS: iOS (and tests standing in for it).
    #[cfg(any(target_os = "ios", feature = "bonjour"))]
    Bonjour,
}

impl Default for Kind {
    fn default() -> Kind {
        #[cfg(target_os = "ios")]
        return Kind::Bonjour;
        #[cfg(not(target_os = "ios"))]
        return Kind::Multicast;
    }
}

/// A running backend and the book it fills; stops (with a goodbye) when dropped.
pub struct Dns {
    pub book: Book,
    #[cfg(not(target_os = "ios"))]
    _multicast: Option<crate::dnssd::Dnssd>,
    #[cfg(any(target_os = "ios", feature = "bonjour"))]
    _bonjour: Option<crate::bonjour::Bonjour>,
}

impl Dns {
    /// Starts `kind` for `ep` (on the current tokio runtime) and adds it to the endpoint's address lookups.
    pub fn start(ep: &iroh::Endpoint, kind: Kind) -> Result<Dns, String> {
        let (lookup, rx) = LanLookup::new();
        let book = lookup.book.clone();
        let dns = match kind {
            #[cfg(not(target_os = "ios"))]
            Kind::Multicast => Dns {
                _multicast: Some(crate::dnssd::Dnssd::start(ep.id(), book.clone(), rx)?),
                #[cfg(any(target_os = "ios", feature = "bonjour"))]
                _bonjour: None,
                book,
            },
            #[cfg(any(target_os = "ios", feature = "bonjour"))]
            Kind::Bonjour => Dns {
                _bonjour: Some(crate::bonjour::Bonjour::start(ep.id(), book.clone(), rx)?),
                #[cfg(not(target_os = "ios"))]
                _multicast: None,
                book,
            },
        };
        ep.address_lookup().map_err(|e| e.to_string())?.add(lookup);
        Ok(dns)
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use iroh::SecretKey;

    #[test]
    fn labels_and_txt() {
        let id = SecretKey::generate().public();
        let l = label(&id);
        assert_eq!(l.len(), 52);
        assert_eq!(id_of(&l), Some(id));
        assert_eq!(id_of(&l.to_ascii_uppercase()), Some(id));
        assert_eq!(id_of("iphone"), None);

        let mut d = EndpointData::from(BTreeSet::from(["10.0.0.2:5000".parse::<SocketAddr>().unwrap()]));
        assert_eq!(txt(&d, 1), b"\x04sd=1");
        assert_eq!(untxt(&[0]), Txt::default());
        d.set_user_data(Some(r#"pb1{"name":"h"}"#.parse().unwrap()));
        d.add_relay_url("https://relay.example/".parse().unwrap());
        let t = txt(&d, 7);
        let want = Txt { user_data: Some(r#"pb1{"name":"h"}"#.into()), relay: Some("https://relay.example/".into()), seq: Some(7) };
        assert_eq!(untxt(&t), want);
        // the highest sequence number wins, wherever it is; then the last
        let old = txt(&EndpointData::from(BTreeSet::<SocketAddr>::new()), 6);
        assert_eq!(newest([&t[..], &old[..]]), want);
        assert_eq!(newest([&old[..], &t[..]]), want);
        assert_eq!(newest([&b"\x03a=1"[..], &b"\x03a=2"[..]]), Txt::default());
        assert_eq!(newest([]), Txt::default());
        // other keys, bad UTF-8 and a truncated string are skipped
        let mut odd = vec![3, b'a', b'=', b'b', 2, 0xff, 0xfe];
        odd.extend_from_slice(&t);
        odd.extend_from_slice(&[40, b'x']);
        assert_eq!(untxt(&odd).user_data.as_deref(), Some(r#"pb1{"name":"h"}"#));
    }

    #[test]
    fn book_forgets_and_resolves() {
        let rt = tokio::runtime::Runtime::new().unwrap();
        rt.block_on(async {
            let (l, _rx) = LanLookup::new();
            let a = SecretKey::generate().public();
            let b = SecretKey::generate().public();
            let addrs = BTreeSet::from(["192.168.1.5:4000".parse().unwrap()]);
            l.book.set(a, Heard { addrs: addrs.clone(), user_data: Some("pb1x".into()), relay: None, until: None });
            l.book.set(b, Heard { addrs, user_data: None, relay: None, until: Some(Instant::now()) });
            assert_eq!(l.book.user_data(), vec![(a, "pb1x".to_string())]);
            assert!(l.book.get(&b).is_none());
            let got: Vec<_> = l.resolve(a).unwrap().collect().await;
            assert_eq!(got.len(), 1);
            let info = got[0].as_ref().unwrap().endpoint_info().clone();
            assert_eq!(info.ip_addrs().copied().collect::<Vec<_>>(), vec!["192.168.1.5:4000".parse().unwrap()]);
            assert_eq!(info.user_data().map(|u| u.to_string()), Some("pb1x".into()));
        });
    }
}
