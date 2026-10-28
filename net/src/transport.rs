//! Netplay transport: iroh QUIC between peers, ported from opentennis `hst-base/src/net.rs` without Bevy.
//!
//! - Peers are iroh endpoints (QUIC, encrypted, hole punching). **Addresses** go on the mainline DHT as pkarr
//!   records, so a bare endpoint ID can be dialled; on a shared network **LAN** peers find each other by mDNS (service
//!   [`MDNS`]), with no internet. A host may put a short listing in its mDNS record ([`Transport::set_lan_listing`]);
//!   [`Transport::lan`] has the ones heard.
//! - **Relay** for when hole punching fails ([`Relay`]): n0's public relays, an own `iroh-relay`, or none (LAN only).
//! - **Codes** (`codes.rs`): the host's whole ticket, its bare ID, or a short code on the DHT / a pkarr relay.
//! - On a connection ([`Peer`]): one reliable bidirectional stream of messages, each JSON with a little-endian u32
//!   length before it ([`write_msg`], [`read_msg`]: opentennis's lobby framing), and unreliable datagrams for inputs
//!   ([`datagram`]: the other end's peer name in front, as opentennis). The joiner opens the stream and speaks first.
//!
//! Everything here is async on tokio; the C ABI's runtime thread wraps it elsewhere.

use std::collections::BTreeMap;
use std::sync::{Arc, Mutex};
use std::time::Duration;

use iroh::address_lookup::AddrFilter;
use iroh::endpoint::{presets, Connection, RecvStream, SendStream};
use iroh::endpoint_info::UserData;
use iroh::{Endpoint, EndpointAddr, EndpointId, RelayMode, RelayUrl, SecretKey};
use iroh_mainline_address_lookup::DhtAddressLookup;
use iroh_mdns_address_lookup::{DiscoveryEvent, MdnsAddressLookup};
use iroh_tickets::endpoint::EndpointTicket;
use n0_future::StreamExt;
use n0_mainline::Dht;
use serde::de::DeserializeOwned;
use serde::Serialize;
use tokio::sync::mpsc;

use crate::codes;

/// The connection's protocol (one stream of length-prefixed JSON, plus datagrams).
pub const ALPN: &[u8] = b"partyboard/lobby/1";
/// The mDNS service name (`<id>._pbn._udp.local`), so only our endpoints answer.
pub const MDNS: &str = "pbn";
/// The LAN listing's prefix in the mDNS user data.
const LAN_TAG: &str = "pb1";
/// The longest message on the stream.
pub const MSG_MAX: usize = 1 << 20;
/// The biggest datagram that surely gets through (QUIC's 1200-byte minimum path MTU less its headers; iroh drops a
/// bigger one, silently).
pub const DATAGRAM_MAX: usize = 1100;

// ---------------------------------------------------------------- settings

/// Which relay to fall back on when hole punching fails.
#[derive(Clone, Debug, PartialEq)]
pub enum Relay {
    /// n0's free public relays.
    N0,
    /// An own `iroh-relay` at this URL.
    Own(String),
    /// None: LAN only (and direct internet when both sides can hole punch).
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

    fn mode(&self) -> Result<RelayMode, String> {
        Ok(match self {
            Relay::N0 => RelayMode::Default,
            Relay::Own(url) => RelayMode::Custom(url.parse::<RelayUrl>().map_err(|e| format!("relay_url {url}: {e}"))?.into()),
            Relay::Off => RelayMode::Disabled,
        })
    }
}

/// A typed relay URL as the endpoint will dial it, or why it can't be one: http(s) with a host.
pub fn relay_url(s: &str) -> Result<String, String> {
    let u = s.trim().parse::<RelayUrl>().map_err(|e| e.to_string())?;
    match u.scheme() {
        "http" | "https" if u.host_str().is_some_and(|h| !h.is_empty()) => Ok(u.to_string()),
        "http" | "https" => Err("no host".into()),
        s => Err(format!("{s}: not http or https")),
    }
}

// ---------------------------------------------------------------- wire

/// Writes `m` as JSON with its little-endian u32 length before it.
pub async fn write_msg<T: Serialize>(s: &mut SendStream, m: &T) -> Result<(), String> {
    let b = serde_json::to_vec(m).map_err(|e| e.to_string())?;
    write_frame(s, &b).await
}

/// Writes one already-serialized message `b` with its length before it.
async fn write_frame(s: &mut SendStream, b: &[u8]) -> Result<(), String> {
    s.write_all(&(b.len() as u32).to_le_bytes()).await.map_err(|e| e.to_string())?;
    s.write_all(b).await.map_err(|e| e.to_string())
}

/// Reads one [`write_msg`] message (Err: the stream ended, or it was longer than [`MSG_MAX`] or not a `T`).
pub async fn read_msg<T: DeserializeOwned>(r: &mut RecvStream) -> Result<T, String> {
    let mut n = [0; 4];
    r.read_exact(&mut n).await.map_err(|e| e.to_string())?;
    let n = u32::from_le_bytes(n) as usize;
    if n > MSG_MAX {
        return Err("message too long".into());
    }
    let mut b = vec![0; n];
    r.read_exact(&mut b).await.map_err(|e| e.to_string())?;
    serde_json::from_slice(&b).map_err(|e| e.to_string())
}

/// Whether `data` fits in a datagram with its peer name (an endpoint id, 64 characters) in front.
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

// ---------------------------------------------------------------- a connection

/// What [`Peer::recv_any`] got.
pub enum Incoming<T> {
    Msg(Result<T, String>),
    Datagram(Vec<u8>),
}

/// One peer connection: messages of type `T` on its stream, datagrams beside. Its stream and datagrams are read on
/// their own tasks (a stream read isn't cancel-safe, so it can't sit in a `select!`); messages go out in order
/// through a task of their own, so [`Peer::send`] never waits.
pub struct Peer<T> {
    conn: Connection,
    out: mpsc::UnboundedSender<Vec<u8>>,
    msgs: mpsc::UnboundedReceiver<Result<T, String>>,
    dgrams: mpsc::UnboundedReceiver<Vec<u8>>,
}

impl<T: Serialize + DeserializeOwned + Send + 'static> Peer<T> {
    fn new(conn: Connection, mut tx: SendStream, mut rx: RecvStream) -> Peer<T> {
        let (mt, msgs) = mpsc::unbounded_channel();
        tokio::spawn(async move {
            loop {
                let m = read_msg(&mut rx).await;
                let end = m.is_err();
                if mt.send(m).is_err() || end {
                    return;
                }
            }
        });
        let (dt, dgrams) = mpsc::unbounded_channel();
        let c = conn.clone();
        tokio::spawn(async move {
            while let Ok(d) = c.read_datagram().await {
                if dt.send(d.to_vec()).is_err() {
                    return;
                }
            }
        });
        let (out, mut r) = mpsc::unbounded_channel::<Vec<u8>>();
        tokio::spawn(async move {
            while let Some(b) = r.recv().await {
                if write_frame(&mut tx, &b).await.is_err() {
                    break;
                }
            }
        });
        Peer { conn, out, msgs, dgrams }
    }

    /// The other end's endpoint ID (its peer name).
    pub fn id(&self) -> EndpointId {
        self.conn.remote_id()
    }

    /// Queues `m` on the stream (lost only if the connection is gone, or `m` can't be JSON).
    pub fn send(&self, m: T) {
        match serde_json::to_vec(&m) {
            Ok(b) => drop(self.out.send(b)),
            Err(e) => eprintln!("pbnet: message not sent: {e}"),
        }
    }

    /// The next message; Err once the stream ended (why).
    pub async fn recv(&mut self) -> Result<T, String> {
        self.msgs.recv().await.unwrap_or_else(|| Err("connection closed".into()))
    }

    /// Sends a datagram as is; one that doesn't fit ([`DATAGRAM_MAX`]) or finds the connection gone is lost, as
    /// datagrams are.
    pub fn send_datagram(&self, d: Vec<u8>) {
        if d.len() <= DATAGRAM_MAX {
            let _ = self.conn.send_datagram(d.into());
        }
    }

    /// The next datagram; None once the connection is gone.
    pub async fn recv_datagram(&mut self) -> Option<Vec<u8>> {
        self.dgrams.recv().await
    }

    /// The next message or datagram, whichever comes first (both cancel-safe, so it can sit in a `select!`). After
    /// the datagrams end, only messages; Err once the stream ended.
    pub async fn recv_any(&mut self) -> Incoming<T> {
        tokio::select! {
            m = self.msgs.recv() => Incoming::Msg(m.unwrap_or_else(|| Err("connection closed".into()))),
            Some(d) = self.dgrams.recv() => Incoming::Datagram(d),
        }
    }

    /// The current round-trip time estimate.
    pub fn rtt(&self) -> Option<Duration> {
        self.conn.paths().iter().find(|p| p.is_selected()).map(|p| p.rtt())
    }

    pub fn close(&self, why: &str) {
        self.conn.close(0u32.into(), why.as_bytes());
    }
}

// ---------------------------------------------------------------- the endpoint

/// A LAN host heard by mDNS: its endpoint and the listing it put in its record.
#[derive(Clone, Debug, PartialEq)]
pub struct LanHost {
    pub id: EndpointId,
    pub listing: String,
}

type Lan = Arc<Mutex<BTreeMap<EndpointId, String>>>;

/// This machine on the network: its iroh endpoint, a DHT node for short codes, and what mDNS hears on the LAN.
pub struct Transport {
    ep: Endpoint,
    dht: Dht,
    relays: bool,
    lan: Lan,
    mdns: bool,
}

impl Transport {
    /// Binds an endpoint with `relay` and a DHT node on the public mainline DHT.
    pub async fn bind(relay: &Relay) -> Result<Transport, String> {
        let dht = Dht::client().map_err(|e| format!("DHT: {e}"))?;
        Transport::bind_with(relay, dht).await
    }

    /// As [`Transport::bind`], with the DHT node to use for short codes (tests: one on a local testnet).
    pub async fn bind_with(relay: &Relay, dht: Dht) -> Result<Transport, String> {
        // the DHT record carries the direct addresses too, so a peer with relays off can still be dialled by ID
        let lookup = DhtAddressLookup::builder().addr_filter(AddrFilter::unfiltered());
        let b = Endpoint::builder(presets::Minimal)
            .secret_key(SecretKey::generate())
            .alpns(vec![ALPN.to_vec()])
            .relay_mode(relay.mode()?)
            .address_lookup(lookup);
        // no waiting for the home relay here: a short code's ticket and a join wait for it themselves
        let ep = b.bind().await.map_err(|e| e.to_string())?;
        // LAN: bare IDs resolve by mDNS and hosts' listings ride on it (no internet needed). An iPhone app may not
        // use multicast without Apple's entitlement: then no LAN, but the rest still works.
        let mdns = MdnsAddressLookup::builder().service_name(MDNS).build(ep.id()).map_err(|e| e.to_string());
        let mdns = mdns.and_then(|m| ep.address_lookup().map_err(|e| e.to_string()).map(|l| l.add(m.clone())).map(|_| m));
        let mdns = mdns.inspect_err(|e| eprintln!("pbnet: mDNS: {e}: no LAN lobbies on this device")).ok();
        let has_mdns = mdns.is_some();
        Ok(Transport { ep, dht, relays: *relay != Relay::Off, lan: watch_lan(mdns), mdns: has_mdns })
    }

    pub fn endpoint(&self) -> &Endpoint {
        &self.ep
    }

    /// This machine's endpoint ID (its peer name).
    pub fn id(&self) -> EndpointId {
        self.ep.id()
    }

    /// Whether mDNS is on (LAN discovery).
    pub fn has_lan(&self) -> bool {
        self.mdns
    }

    /// This endpoint's whole ticket (ID, relay and direct addresses) as it is now.
    pub fn ticket(&self) -> String {
        EndpointTicket::new(self.ep.addr()).to_string()
    }

    /// Waits until the address is worth a code: a home relay while the relays are on (30 s at most), and some direct
    /// address or relay (5 s at most; right after bind it can still be empty).
    pub async fn ready(&self) {
        if self.relays && self.ep.addr().relay_urls().next().is_none() {
            let _ = tokio::time::timeout(Duration::from_secs(30), self.ep.online()).await;
        }
        for _ in 0..50 {
            let a = self.ep.addr();
            if a.ip_addrs().next().is_some() || a.relay_urls().next().is_some() {
                break;
            }
            tokio::time::sleep(Duration::from_millis(100)).await;
        }
    }

    /// Puts this endpoint's ticket under a free short code (rolled, `first` if given) and returns the code. Call it
    /// again every [`codes::REPUBLISH`] with the code to keep it fresh; it is free again [`codes::STALE`] after the
    /// last put.
    pub async fn publish_code(&self, first: Option<String>) -> Result<String, String> {
        self.ready().await;
        let ticket = codes::code_ticket(&self.ep, None);
        let code = first.unwrap_or_else(codes::new_short);
        let r = codes::roll(&self.dht, code, &ticket, |_| {});
        tokio::time::timeout(codes::ROLL_CAP, r).await.unwrap_or_else(|_| Err(format!("no answer in {} s", codes::ROLL_CAP.as_secs())))
    }

    /// Advertises `listing` (a short text, e.g. JSON; None: not hosting) in this endpoint's mDNS record for LAN peers.
    pub fn set_lan_listing(&self, listing: Option<&str>) -> Result<(), String> {
        let data = match listing {
            Some(l) => Some(format!("{LAN_TAG}{l}").parse::<UserData>().map_err(|e| e.to_string())?),
            None => None,
        };
        self.ep.set_user_data_for_address_lookup(data);
        Ok(())
    }

    /// The hosts mDNS hears on the LAN now, with their listings.
    pub fn lan(&self) -> Vec<LanHost> {
        self.lan.lock().unwrap().iter().map(|(id, l)| LanHost { id: *id, listing: l.clone() }).collect()
    }

    /// The next incoming connection whose joiner opened its stream; None once the endpoint is closed. A connection
    /// that fails before its stream is skipped.
    pub async fn accept<T: Serialize + DeserializeOwned + Send + 'static>(&self) -> Option<Peer<T>> {
        loop {
            let inc = self.ep.accept().await?;
            let Ok(conn) = inc.await else { continue };
            // the joiner's stream shows up with its first message
            let Ok((tx, rx)) = conn.accept_bi().await else { continue };
            return Some(Peer::new(conn, tx, rx));
        }
    }

    /// Connects to the host by `code` (a short code, ticket or bare endpoint ID) and opens the stream (the joiner
    /// speaks first: the host sees the connection with its first message).
    pub async fn dial<T: Serialize + DeserializeOwned + Send + 'static>(&self, code: &str) -> Result<Peer<T>, String> {
        let code = code.trim();
        let resolved;
        let code = if codes::is_short(code) {
            resolved = codes::resolve(&self.dht, code, self.relays).await?;
            resolved.as_str()
        } else {
            code
        };
        let ticket = code.parse::<EndpointTicket>();
        let addr: EndpointAddr = match &ticket {
            Ok(t) => t.clone().into(),
            Err(_) => code.parse::<EndpointId>().map_err(|_| "That code isn't a lobby code.".to_string())?.into(),
        };
        if !self.relays && ticket.is_ok() && addr.ip_addrs().next().is_none() {
            return Err("That lobby can only be reached through a relay: set Relay to n0 to join it.".into());
        }
        let conn = match tokio::time::timeout(Duration::from_secs(30), self.ep.connect(addr, ALPN)).await {
            Ok(Ok(c)) => c,
            Ok(Err(e)) => return Err(format!("Couldn't reach the host: {e}")),
            Err(_) => return Err("Couldn't reach the host (timed out).".into()),
        };
        let (tx, rx) = conn.open_bi().await.map_err(|_| "The host closed the connection.".to_string())?;
        Ok(Peer::new(conn, tx, rx))
    }

    /// Closes the endpoint (every connection with it).
    pub async fn close(&self) {
        self.ep.close().await;
    }
}

/// N92: a browser guest's answer for its host (`rtc.rs`); iroh's guests need none.
pub fn answer_code() -> String {
    String::new()
}

/// N92: a browser host takes a guest's answer (`rtc.rs`); iroh's hosts never need one.
pub fn take_answer(_answer: &str) -> Result<(), String> {
    Err("Only a lobby hosted in a browser takes answers.".into())
}

/// Keeps a map of the hosts mDNS hears now (a host's record updates when its listing changes; gone when it stops).
fn watch_lan(mdns: Option<MdnsAddressLookup>) -> Lan {
    let lan = Lan::default();
    let Some(mdns) = mdns else { return lan };
    let l = lan.clone();
    tokio::spawn(async move {
        let mut events = mdns.subscribe().await;
        while let Some(e) = events.next().await {
            let mut l = l.lock().unwrap();
            match e {
                DiscoveryEvent::Discovered { endpoint_info: i, .. } => match i.data.user_data().and_then(|d| d.as_ref().strip_prefix(LAN_TAG)) {
                    Some(listing) => l.insert(i.endpoint_id, listing.to_string()),
                    None => l.remove(&i.endpoint_id),
                },
                DiscoveryEvent::Expired { endpoint_id } => l.remove(&endpoint_id),
                _ => None,
            };
        }
    });
    lan
}

#[cfg(test)]
mod tests {
    use super::*;
    use serde::Deserialize;

    #[derive(Clone, Debug, PartialEq, Serialize, Deserialize)]
    enum Msg {
        Hello { name: String },
        Room(Vec<u8>),
    }

    #[test]
    fn relay_settings() {
        assert_eq!(Relay::parse("off", ""), Relay::Off);
        assert_eq!(Relay::parse("own", " https://r.example/ "), Relay::Own("https://r.example/".into()));
        assert_eq!(Relay::parse("whatever", ""), Relay::N0);
        assert_eq!(Relay::Own(String::new()).name(), "own");
        assert!(Relay::Own("not a url".into()).mode().is_err());
        assert_eq!(relay_url("https://relay.example.com").unwrap(), "https://relay.example.com/");
        assert!(relay_url("ftp://x").is_err());
    }

    #[test]
    fn datagram_framing() {
        let id = SecretKey::generate().public().to_string();
        let d = datagram(&id, b"pads");
        assert_eq!(d[0] as usize, id.len());
        assert_eq!(undatagram(&d), Some((id.clone(), &b"pads"[..])));
        assert_eq!(undatagram(&[]), None);
        assert_eq!(undatagram(&[5, b'a']), None);
        assert!(fits(&[0; DATAGRAM_MAX - 65]));
        assert!(!fits(&[0; DATAGRAM_MAX - 64]));
    }

    /// The stream's framing is opentennis's: a little-endian u32 length, then JSON.
    #[test]
    fn stream_framing_is_u32_le_json() {
        let rt = tokio::runtime::Runtime::new().unwrap();
        rt.block_on(async {
            let net = n0_mainline::Testnet::new(3).await.unwrap();
            let dht = || Dht::builder().bootstrap(&net.bootstrap).build().unwrap();
            let host = Transport::bind_with(&Relay::Off, dht()).await.unwrap();
            let guest = Transport::bind_with(&Relay::Off, dht()).await.unwrap();
            host.ready().await;
            let ticket = host.ticket();
            let h = tokio::spawn(async move {
                let inc = host.ep.accept().await.unwrap();
                let conn = inc.await.unwrap();
                let (_tx, mut rx) = conn.accept_bi().await.unwrap();
                let mut n = [0; 4];
                rx.read_exact(&mut n).await.unwrap();
                let mut b = vec![0; u32::from_le_bytes(n) as usize];
                rx.read_exact(&mut b).await.unwrap();
                (b, host)
            });
            let g = guest.dial::<Msg>(&ticket).await.unwrap();
            g.send(Msg::Hello { name: "p".into() });
            let (b, _host) = h.await.unwrap();
            assert_eq!(b, br#"{"Hello":{"name":"p"}}"#);
        });
    }

    /// Two endpoints in-process: the host puts a short code on a local DHT testnet, the guest joins by it, and they
    /// swap stream messages and datagrams both ways; a ticket joins too.
    #[test]
    fn two_endpoints_connect_by_code_and_swap_messages() {
        let rt = tokio::runtime::Runtime::new().unwrap();
        rt.block_on(async {
            let net = n0_mainline::Testnet::new(3).await.unwrap();
            let dht = || Dht::builder().bootstrap(&net.bootstrap).build().unwrap();
            let host = Arc::new(Transport::bind_with(&Relay::Off, dht()).await.unwrap());
            let guest = Transport::bind_with(&Relay::Off, dht()).await.unwrap();
            let code = host.publish_code(None).await.unwrap();
            assert!(codes::is_short(&code), "{code}");

            let h = host.clone();
            let hosting = tokio::spawn(async move {
                let mut p = h.accept::<Msg>().await.unwrap();
                let hello = p.recv().await.unwrap();
                p.send(Msg::Room(vec![1, 2, 3]));
                let d = p.recv_datagram().await.unwrap();
                p.send_datagram(datagram(&p.id().to_string(), b"back"));
                (p, hello, d)
            });
            let mut g = tokio::time::timeout(Duration::from_secs(60), guest.dial::<Msg>(&code)).await.expect("join in time").unwrap();
            assert_eq!(g.id(), host.id());
            g.send(Msg::Hello { name: "guest".into() });
            assert_eq!(g.recv().await.unwrap(), Msg::Room(vec![1, 2, 3]));
            // datagrams are unreliable: sent until one gets through (on loopback, the first)
            let me = guest.id().to_string();
            let back = loop {
                g.send_datagram(datagram(&me, b"pads"));
                if let Ok(d) = tokio::time::timeout(Duration::from_millis(200), g.recv_datagram()).await {
                    break d.unwrap();
                }
            };
            let (p, hello, d) = tokio::time::timeout(Duration::from_secs(10), hosting).await.unwrap().unwrap();
            assert_eq!(hello, Msg::Hello { name: "guest".into() });
            assert_eq!(undatagram(&d), Some((me.clone(), &b"pads"[..])));
            assert_eq!(undatagram(&back), Some((me, &b"back"[..])));
            assert_eq!(p.id(), guest.id());

            // the stream ends for the other side when one closes
            p.close("bye");
            assert!(g.recv().await.is_err());

            // and by the host's whole ticket
            let h = host.clone();
            let hosting = tokio::spawn(async move { h.accept::<Msg>().await.unwrap().recv().await });
            let g2 = guest.dial::<Msg>(&host.ticket()).await.unwrap();
            g2.send(Msg::Hello { name: "again".into() });
            assert_eq!(hosting.await.unwrap().unwrap(), Msg::Hello { name: "again".into() });
        });
    }

    /// A code nobody published fails with words, not a hang.
    #[test]
    fn unknown_code_fails() {
        let rt = tokio::runtime::Runtime::new().unwrap();
        rt.block_on(async {
            let net = n0_mainline::Testnet::new(3).await.unwrap();
            let dht = Dht::builder().bootstrap(&net.bootstrap).build().unwrap();
            let guest = Transport::bind_with(&Relay::Off, dht).await.unwrap();
            assert!(guest.dial::<Msg>("not a code").await.err().unwrap().contains("isn't a lobby code"));
        });
    }

    /// LAN discovery by mDNS: a host's listing is heard by another endpoint. Needs multicast on the machine's network
    /// interfaces, which sandboxes and CI runners often lack: run with `cargo test -- --ignored`.
    #[test]
    #[ignore]
    fn lan_listing_heard_by_mdns() {
        let rt = tokio::runtime::Runtime::new().unwrap();
        rt.block_on(async {
            let net = n0_mainline::Testnet::new(3).await.unwrap();
            let dht = || Dht::builder().bootstrap(&net.bootstrap).build().unwrap();
            let host = Transport::bind_with(&Relay::Off, dht()).await.unwrap();
            let finder = Transport::bind_with(&Relay::Off, dht()).await.unwrap();
            assert!(host.has_lan() && finder.has_lan());
            host.set_lan_listing(Some(r#"{"name":"lan host"}"#)).unwrap();
            for _ in 0..150 {
                if let Some(l) = finder.lan().into_iter().find(|l| l.id == host.id()) {
                    assert_eq!(l.listing, r#"{"name":"lan host"}"#);
                    // and its bare ID dials over the LAN
                    let h = tokio::spawn(async move { host.accept::<Msg>().await.unwrap().recv().await });
                    let p = finder.dial::<Msg>(&l.id.to_string()).await.unwrap();
                    p.send(Msg::Hello { name: "lan".into() });
                    assert_eq!(h.await.unwrap().unwrap(), Msg::Hello { name: "lan".into() });
                    return;
                }
                tokio::time::sleep(Duration::from_millis(100)).await;
            }
            panic!("not heard in 15 s");
        });
    }
}
