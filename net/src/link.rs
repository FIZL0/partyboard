//! N96: a desktop lobby's transport, one of two per lobby: iroh (`transport.rs`, the desktop's own lobbies and short
//! codes) or WebRTC (`rtc.rs` on str0m, `rtc/native.rs`), which a browser speaks. A lobby is WebRTC when it is hosted
//! for browsers (the setup's `webrtc`) or joined with a browser host's offer code (`rtco…`); its codes, answers and
//! channels are then the browser's own. The lobby (`lobby_c.rs`) uses the names below as it used `transport.rs`'s.

use std::time::Duration;

use serde::de::DeserializeOwned;
use serde::Serialize;

use crate::{codes, rtc, transport};

pub use transport::{datagram, fits, undatagram, Incoming, LanHost, Relay, DATAGRAM_MAX};

/// The relay setting as the WebRTC side reads it (`n0`: public STUN; `own`: the URL as a STUN/TURN server; `off`:
/// host candidates only).
fn rtc_relay(relay: &Relay) -> rtc::Relay {
    match relay {
        Relay::N0 => rtc::Relay::N0,
        Relay::Own(u) => rtc::Relay::Own(u.clone()),
        Relay::Off => rtc::Relay::Off,
    }
}

/// One peer connection, on either transport.
pub enum Peer<T> {
    Iroh(transport::Peer<T>),
    Rtc(rtc::Peer<T>),
}

impl<T: Serialize + DeserializeOwned + Send + 'static> Peer<T> {
    /// The other end's peer name.
    pub fn id(&self) -> String {
        match self {
            Peer::Iroh(p) => p.id().to_string(),
            Peer::Rtc(p) => p.id().to_string(),
        }
    }

    pub fn send(&self, m: T) {
        match self {
            Peer::Iroh(p) => p.send(m),
            Peer::Rtc(p) => p.send(m),
        }
    }

    pub async fn recv(&mut self) -> Result<T, String> {
        match self {
            Peer::Iroh(p) => p.recv().await,
            Peer::Rtc(p) => p.recv().await,
        }
    }

    pub fn send_datagram(&self, d: Vec<u8>) {
        match self {
            Peer::Iroh(p) => p.send_datagram(d),
            Peer::Rtc(p) => p.send_datagram(d),
        }
    }

    pub async fn recv_datagram(&mut self) -> Option<Vec<u8>> {
        match self {
            Peer::Iroh(p) => p.recv_datagram().await,
            Peer::Rtc(p) => p.recv_datagram().await,
        }
    }

    /// The next message or datagram, whichever comes first (cancel-safe).
    pub async fn recv_any(&mut self) -> Incoming<T> {
        match self {
            Peer::Iroh(p) => p.recv_any().await,
            Peer::Rtc(p) => match p.recv_any().await {
                rtc::Incoming::Msg(m) => Incoming::Msg(m),
                rtc::Incoming::Datagram(d) => Incoming::Datagram(d),
            },
        }
    }

    pub fn rtt(&self) -> Option<Duration> {
        match self {
            Peer::Iroh(p) => p.rtt(),
            Peer::Rtc(p) => p.rtt(),
        }
    }

    pub fn close(&self, why: &str) {
        match self {
            Peer::Iroh(p) => p.close(why),
            Peer::Rtc(p) => p.close(why),
        }
    }
}

/// This machine on the network for one lobby.
pub enum Transport {
    Iroh(transport::Transport),
    Rtc(rtc::Transport),
}

impl From<rtc::Transport> for Transport {
    fn from(t: rtc::Transport) -> Transport {
        Transport::Rtc(t)
    }
}

impl Transport {
    /// An iroh endpoint ([`transport::Transport::bind`]).
    pub async fn bind(relay: &Relay) -> Result<Transport, String> {
        transport::Transport::bind(relay).await.map(Transport::Iroh)
    }

    /// As [`Transport::bind`], with the DHT node for short codes (tests).
    pub async fn bind_with(relay: &Relay, dht: n0_mainline::Dht) -> Result<Transport, String> {
        transport::Transport::bind_with(relay, dht).await.map(Transport::Iroh)
    }

    /// N139a: the LAN tab's endpoint: iroh's, which hears the LAN by mDNS (WebRTC's `nearby.rs` records are the
    /// browser's).
    pub async fn bind_browse(relay: &Relay) -> Result<Transport, String> {
        Transport::bind(relay).await
    }

    /// WebRTC, as a browser: to host for browsers or join a browser's lobby.
    pub async fn bind_rtc(relay: &Relay) -> Result<Transport, String> {
        rtc::Transport::bind(&rtc_relay(relay)).await.map(Transport::Rtc)
    }

    /// The transport for a lobby: WebRTC when `rtc` (hosting for browsers) or `code` is a browser's offer.
    pub async fn bind_for(relay: &Relay, rtc: bool, code: &str) -> Result<Transport, String> {
        if rtc || is_rtc_code(code) {
            Transport::bind_rtc(relay).await
        } else {
            Transport::bind(relay).await
        }
    }

    pub fn is_rtc(&self) -> bool {
        matches!(self, Transport::Rtc(_))
    }

    /// This machine's peer name.
    pub fn id(&self) -> String {
        match self {
            Transport::Iroh(t) => t.id().to_string(),
            Transport::Rtc(t) => t.id().to_string(),
        }
    }

    pub fn has_lan(&self) -> bool {
        match self {
            Transport::Iroh(t) => t.has_lan(),
            Transport::Rtc(t) => t.has_lan(),
        }
    }

    pub fn ticket(&self) -> String {
        match self {
            Transport::Iroh(t) => t.ticket(),
            Transport::Rtc(t) => t.ticket(),
        }
    }

    pub async fn ready(&self) {
        match self {
            Transport::Iroh(t) => t.ready().await,
            Transport::Rtc(t) => t.ready().await,
        }
    }

    /// The host's code: iroh's short code, or the WebRTC offer for the next guest.
    pub async fn publish_code(&self, first: Option<String>) -> Result<String, String> {
        match self {
            Transport::Iroh(t) => t.publish_code(first).await,
            Transport::Rtc(t) => t.publish_code(first).await,
        }
    }

    /// How often [`Transport::publish_code`] is asked again.
    pub fn republish(&self) -> Duration {
        match self {
            Transport::Iroh(_) => codes::REPUBLISH,
            Transport::Rtc(t) => t.republish(),
        }
    }

    /// WebRTC hosting: why the last pasted answer didn't connect (Some("") once one did); never for iroh.
    pub fn take_error(&self) -> Option<String> {
        match self {
            Transport::Iroh(_) => None,
            Transport::Rtc(t) => t.take_error(),
        }
    }

    pub fn set_lan_listing(&self, listing: Option<&str>) -> Result<(), String> {
        match self {
            Transport::Iroh(t) => t.set_lan_listing(listing),
            Transport::Rtc(t) => t.set_lan_listing(listing),
        }
    }

    pub fn lan(&self) -> Vec<LanHost> {
        match self {
            Transport::Iroh(t) => t.lan(),
            Transport::Rtc(_) => Vec::new(),
        }
    }

    pub async fn accept<T: Serialize + DeserializeOwned + Send + 'static>(&self) -> Option<Peer<T>> {
        match self {
            Transport::Iroh(t) => t.accept().await.map(Peer::Iroh),
            Transport::Rtc(t) => t.accept().await.map(Peer::Rtc),
        }
    }

    pub async fn dial<T: Serialize + DeserializeOwned + Send + 'static>(&self, code: &str) -> Result<Peer<T>, String> {
        match self {
            Transport::Iroh(_) if is_rtc_code(code) => Err("That code is a browser's lobby: join it again from the Join screen.".into()),
            Transport::Iroh(t) => t.dial(code).await.map(Peer::Iroh),
            Transport::Rtc(t) => t.dial(code).await.map(Peer::Rtc),
        }
    }

    pub async fn close(&self) {
        match self {
            Transport::Iroh(t) => t.close().await,
            Transport::Rtc(t) => t.close().await,
        }
    }
}

/// `code` (or a link with it) is a browser host's WebRTC offer.
pub fn is_rtc_code(code: &str) -> bool {
    rtc::codes::offer_in(code).is_some()
}

/// A WebRTC guest's answer for its host while it waits (empty otherwise).
pub fn answer_code() -> String {
    rtc::answer_code()
}

/// A WebRTC host takes a guest's answer.
pub fn take_answer(answer: &str) -> Result<(), String> {
    rtc::take_answer(answer)
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn rtc_codes_pick_webrtc() {
        let sig = rtc::Sig { ufrag: "abcd".into(), pwd: "p".repeat(22), fingerprint: [1; 32], cands: vec![("10.0.0.2".into(), 5000)], id: [2; 8], to: String::new() };
        let code = sig.code(rtc::OFFER);
        assert!(is_rtc_code(&code));
        assert!(is_rtc_code(&format!("https://example.org/play/?code={code}")));
        assert!(!is_rtc_code("ABCD"));
        assert!(!is_rtc_code(&sig.code(rtc::ANSWER)));
    }
}
