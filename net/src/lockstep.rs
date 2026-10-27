//! Delay-based lockstep (ported from opentennis `hst-play/src/play/netplay.rs` `Lockstep`, without Bevy).
//!
//! Every peer runs the same frames on the same four pads. A peer's own seats' pads for frame `t` are taken
//! when it is about to run frame `t - delay`, so at steady speed nobody waits; frames `0..delay` are idle for
//! every seat. [`Session::step`] runs one frame when every seat's pads for it are in ([`Step::Ready`]), else
//! [`Step::Wait`] (the caller skips the game tick and calls again next real frame).
//!
//! Datagrams are unreliable: every step sends each peer every own frame it has not acknowledged yet (oldest
//! first, capped per datagram), so a lost one is covered by the next. Each datagram also carries the
//! receiver's ack, a timestamp echo (round trip, for [`delay_for`]) and this peer's latest frame hashes. Hashes
//! (of the state before a frame, computed by C, one every [`HASH_EVERY`] frames) are compared whichever side
//! comes first; a mismatch is queued for [`Session::desync`].
//!
//! The transport is the [`Transport`] trait (peers by line-up index); tests use an in-memory wire with fake
//! lag and loss.

use std::collections::{BTreeMap, VecDeque};

/// C hashes the state before every this many frames.
pub const HASH_EVERY: u32 = 30;
/// Bytes of one pad on the wire.
pub const PAD_WIRE: usize = 12;
/// Datagram budget for the pads in one packet (stays under a 1200-byte QUIC datagram with the header).
const PAD_BUDGET: usize = 1024;
/// Own hashes carried in every datagram (the latest ones).
const HASHES_SENT: usize = 2;
/// Own hashes kept for comparing late peers' (one per [`HASH_EVERY`] frames: ~2 minutes).
const HASHES_KEPT: usize = 256;
/// A peer's hashes kept while ours for that frame isn't in yet.
const THEIRS_KEPT: usize = 16;
/// Datagram tag of a frame packet.
const TAG_FRAMES: u8 = 0x50;

/// One pad, laid out as aurora's `PADStatus` on PC (`TARGET_PC` adds `extButton`, 16 bytes). Only the first
/// 11 fields go on the wire; `ext_button` is not used by the game and arrives as 0.
#[repr(C)]
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq, Hash)]
pub struct Pad {
    pub button: u16,
    pub stick_x: i8,
    pub stick_y: i8,
    pub substick_x: i8,
    pub substick_y: i8,
    pub trigger_left: u8,
    pub trigger_right: u8,
    pub analog_a: u8,
    pub analog_b: u8,
    pub err: i8,
    pub ext_button: u32,
}

const _: () = assert!(std::mem::size_of::<Pad>() == 16 && std::mem::offset_of!(Pad, err) == 10 && std::mem::offset_of!(Pad, ext_button) == 12);

/// No buttons, sticks centred, connected: the pad of an empty seat and of the first `delay` frames.
pub const IDLE: Pad = Pad {
    button: 0,
    stick_x: 0,
    stick_y: 0,
    substick_x: 0,
    substick_y: 0,
    trigger_left: 0,
    trigger_right: 0,
    analog_a: 0,
    analog_b: 0,
    err: 0,
    ext_button: 0,
};

impl Pad {
    /// The 12-byte wire form: `button` little-endian, the ten byte fields, one zero.
    pub fn wire(&self) -> [u8; PAD_WIRE] {
        let b = self.button.to_le_bytes();
        [
            b[0],
            b[1],
            self.stick_x as u8,
            self.stick_y as u8,
            self.substick_x as u8,
            self.substick_y as u8,
            self.trigger_left,
            self.trigger_right,
            self.analog_a,
            self.analog_b,
            self.err as u8,
            0,
        ]
    }

    pub fn unwire(w: &[u8]) -> Pad {
        Pad {
            button: u16::from_le_bytes([w[0], w[1]]),
            stick_x: w[2] as i8,
            stick_y: w[3] as i8,
            substick_x: w[4] as i8,
            substick_y: w[5] as i8,
            trigger_left: w[6],
            trigger_right: w[7],
            analog_a: w[8],
            analog_b: w[9],
            err: w[10] as i8,
            ext_button: 0,
        }
    }
}

/// The input delay (frames) for a round trip of `rtt_ms`: enough 60 Hz frames to cover one way, plus one for
/// jitter, clamped to 2..=15 (opentennis `net::delay_for`).
pub fn delay_for(rtt_ms: u32) -> u32 {
    ((rtt_ms as f64 / 1000.0 / 2.0 * 60.0).ceil() as u32 + 1).clamp(2, 15)
}

/// Unreliable, unordered datagrams to the other peers, addressed by line-up index.
pub trait Transport {
    /// Sends one datagram to peer `to`. May drop it.
    fn send(&mut self, to: u8, datagram: &[u8]);
    /// The next datagram that came in, with the index of the peer it came from.
    fn recv(&mut self) -> Option<(u8, Vec<u8>)>;
}

impl<T: Transport + ?Sized> Transport for Box<T> {
    fn send(&mut self, to: u8, datagram: &[u8]) {
        (**self).send(to, datagram)
    }
    fn recv(&mut self) -> Option<(u8, Vec<u8>)> {
        (**self).recv()
    }
}

/// No seat: idle pads.
pub const NO_PEER: u8 = 0xff;

/// Who plays where.
#[derive(Clone, Debug)]
pub struct Config {
    /// This machine's index in the line-up (`0..peers`).
    pub me: u8,
    /// Peers in the match, this one included (1..=4).
    pub peers: u8,
    /// Each seat's peer, or [`NO_PEER`] for an empty seat (always idle; a CPU seat).
    pub seats: [u8; 4],
    /// Input delay in frames ([`delay_for`] of the slowest peer's round trip, picked by the host).
    pub delay: u32,
}

/// One frame's outcome.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Step {
    /// Run the frame on these pads.
    Ready([Pad; 4]),
    /// Some peer's pads for the frame aren't in: don't run it.
    Wait,
}

/// Two peers' hashes of the state before `frame` differ.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct Desync {
    pub frame: u32,
    pub peer: u8,
    pub ours: u64,
    pub theirs: u64,
}

struct Remote {
    /// Its seats, in seat order.
    seats: Vec<usize>,
    /// Its pads by frame, frames `>= tick` that came in (out of order allowed).
    frames: BTreeMap<u32, Vec<Pad>>,
    /// Every frame below this is in (or already run): the ack we send it.
    next: u32,
    /// It has every own frame below this.
    acked: u32,
    /// Its last timestamp and our clock when it came, for the echo.
    stamp: Option<(u32, u64)>,
    /// Smoothed round trip (ms).
    rtt: Option<u32>,
    /// Its hashes whose frame we haven't hashed yet.
    theirs: BTreeMap<u32, u64>,
    /// Frames of its hashes already compared (dropping the repeats every datagram carries).
    compared: VecDeque<u32>,
    /// Our clock when its last datagram came (N10: silence).
    heard: Option<u64>,
}

/// One machine's lockstep.
pub struct Session<T: Transport> {
    cfg: Config,
    wire: T,
    /// The next frame to run.
    tick: u32,
    /// Own seats, in seat order.
    mine: Vec<usize>,
    /// Own pads by frame: unacked by somebody, or not run yet.
    own: BTreeMap<u32, Vec<Pad>>,
    /// The next own frame to take pads for.
    own_next: u32,
    remotes: Vec<Option<Remote>>,
    hashes: BTreeMap<u32, u64>,
    desyncs: VecDeque<Desync>,
    /// The clock of the first step (a peer never heard is silent since then).
    began: Option<u64>,
}

impl<T: Transport> Session<T> {
    pub fn new(cfg: Config, wire: T) -> Self {
        assert!(cfg.peers >= 1 && cfg.peers <= 4 && cfg.me < cfg.peers, "line-up {cfg:?}");
        let delay = cfg.delay;
        let of = |p: u8| (0..4).filter(|&s| cfg.seats[s] == p).collect::<Vec<_>>();
        let remotes = (0..cfg.peers)
            .map(|p| {
                (p != cfg.me).then(|| Remote {
                    seats: of(p),
                    frames: BTreeMap::new(),
                    next: delay,
                    acked: delay,
                    stamp: None,
                    rtt: None,
                    theirs: BTreeMap::new(),
                    compared: VecDeque::new(),
                    heard: None,
                })
            })
            .collect();
        Session {
            mine: of(cfg.me),
            cfg,
            wire,
            tick: 0,
            own: BTreeMap::new(),
            own_next: delay,
            remotes,
            hashes: BTreeMap::new(),
            desyncs: VecDeque::new(),
            began: None,
        }
    }

    /// Who plays where.
    pub fn config(&self) -> &Config {
        &self.cfg
    }

    /// How long (ms, on the `step` clock) peer `p` has been silent: since its last datagram, or since the first
    /// step if none came yet. None for this machine or no such peer. Every step sends every peer a datagram (also
    /// while it waits), so a peer that still runs is never silent for long: this is the keep-alive.
    pub fn quiet_ms(&self, p: u8, now_ms: u64) -> Option<u64> {
        let r = self.remotes.get(p as usize)?.as_ref()?;
        let since = r.heard.or(self.began).unwrap_or(now_ms);
        Some(now_ms.saturating_sub(since))
    }

    /// The peers silent for `after_ms` or more (bit per line-up index).
    pub fn silent(&self, now_ms: u64, after_ms: u64) -> u8 {
        (0..self.remotes.len() as u8).filter(|&p| self.quiet_ms(p, now_ms).is_some_and(|q| q >= after_ms)).fold(0, |m, p| m | 1 << p)
    }

    /// The next frame to run.
    pub fn frame(&self) -> u32 {
        self.tick
    }

    pub fn delay(&self) -> u32 {
        self.cfg.delay
    }

    /// How many pads `step` takes: this machine's seats.
    pub fn local_seats(&self) -> usize {
        self.mine.len()
    }

    /// Peer `p`'s smoothed round trip (ms), once one was measured.
    pub fn rtt_ms(&self, p: u8) -> Option<u32> {
        self.remotes.get(p as usize)?.as_ref()?.rtt
    }

    /// The peers whose pads for the next frame aren't in (bit per line-up index).
    pub fn waiting_on(&self) -> u8 {
        let mut m = 0;
        for (p, r) in self.remotes.iter().enumerate() {
            if let Some(r) = r {
                if !r.seats.is_empty() && !Self::has(r, self.tick) {
                    m |= 1 << p;
                }
            }
        }
        m
    }

    fn has(r: &Remote, t: u32) -> bool {
        t < r.next || r.frames.contains_key(&t)
    }

    /// One real frame: takes in what came, takes this machine's pads (`local`, its seats in seat order;
    /// missing ones idle) for frame `frame() + delay` if not taken yet, sends, and runs the next frame if
    /// every seat's pads are in. `now_ms` is a monotonic clock (for the round trip only).
    ///
    /// While it waits, `local` is not taken: the pads that go out are the ones of the call that first
    /// reached a frame.
    pub fn step(&mut self, now_ms: u64, local: &[Pad]) -> Step {
        self.began.get_or_insert(now_ms);
        self.pump(now_ms);
        if self.own_next == self.tick + self.cfg.delay {
            let pads = (0..self.mine.len()).map(|i| local.get(i).copied().unwrap_or(IDLE)).collect();
            self.own.insert(self.own_next, pads);
            self.own_next += 1;
        }
        self.send(now_ms);
        if self.waiting_on() != 0 {
            return Step::Wait;
        }
        let t = self.tick;
        let mut pads = [IDLE; 4];
        if t >= self.cfg.delay {
            for (i, &s) in self.mine.iter().enumerate() {
                pads[s] = self.own[&t][i];
            }
            for r in self.remotes.iter_mut().flatten() {
                if let Some(f) = r.frames.remove(&t) {
                    for (i, &s) in r.seats.iter().enumerate() {
                        pads[s] = f[i];
                    }
                }
            }
        }
        self.tick += 1;
        self.prune();
        Step::Ready(pads)
    }

    /// After the last frame (every peer stops at the same one): takes in what came and sends each peer the own pads
    /// it still lacks and the acks of its pads, without taking pads or running a frame. True once every peer has
    /// every own frame this machine ran, i.e. no peer needs anything more from it to reach the same last frame.
    pub fn drain(&mut self, now_ms: u64) -> bool {
        self.pump(now_ms);
        self.send(now_ms);
        let tick = self.tick;
        self.remotes.iter().flatten().all(|r| r.acked >= tick)
    }

    /// This machine's hash of the state before `frame` (C calls it when `frame() % HASH_EVERY == 0`, before
    /// `step` runs that frame). Compared with the peers' as they come in.
    pub fn set_hash(&mut self, frame: u32, hash: u64) {
        self.hashes.insert(frame, hash);
        while self.hashes.len() > HASHES_KEPT {
            self.hashes.pop_first();
        }
        for p in 0..self.remotes.len() {
            if let Some(theirs) = self.remotes[p].as_mut().and_then(|r| r.theirs.remove(&frame)) {
                self.compare(p as u8, frame, theirs);
            }
        }
    }

    /// The next hash mismatch found, oldest first.
    pub fn desync(&mut self) -> Option<Desync> {
        self.desyncs.pop_front()
    }

    fn compare(&mut self, p: u8, frame: u32, theirs: u64) {
        let Some(r) = self.remotes[p as usize].as_mut() else { return };
        if r.compared.contains(&frame) {
            return;
        }
        r.compared.push_back(frame);
        if r.compared.len() > HASHES_SENT * 4 {
            r.compared.pop_front();
        }
        let ours = self.hashes[&frame];
        if ours != theirs {
            self.desyncs.push_back(Desync { frame, peer: p, ours, theirs });
        }
    }

    fn their_hash(&mut self, p: u8, frame: u32, hash: u64) {
        if self.hashes.contains_key(&frame) {
            self.compare(p, frame, hash);
            return;
        }
        // older than every hash we keep: nothing to compare with
        if self.hashes.first_key_value().is_some_and(|(&f, _)| frame < f) && self.hashes.len() == HASHES_KEPT {
            return;
        }
        let Some(r) = self.remotes[p as usize].as_mut() else { return };
        if r.compared.contains(&frame) {
            return;
        }
        r.theirs.insert(frame, hash);
        while r.theirs.len() > THEIRS_KEPT {
            r.theirs.pop_first();
        }
    }

    /// Drops own frames every peer has and this machine ran.
    fn prune(&mut self) {
        let low = self.remotes.iter().flatten().map(|r| r.acked).min().unwrap_or(u32::MAX).min(self.tick);
        while self.own.first_key_value().is_some_and(|(&f, _)| f < low) {
            self.own.pop_first();
        }
    }

    fn send(&mut self, now_ms: u64) {
        let per = if self.mine.is_empty() { 0 } else { PAD_BUDGET / (PAD_WIRE * self.mine.len()) };
        for p in 0..self.remotes.len() {
            let Some(r) = self.remotes[p].as_ref() else { continue };
            let first = r.acked;
            let count = if per == 0 { 0 } else { self.own_next.saturating_sub(first).min(per as u32).min(255) };
            let mut d = Vec::with_capacity(32 + count as usize * PAD_WIRE * self.mine.len());
            d.push(TAG_FRAMES);
            d.push(self.cfg.me);
            d.extend_from_slice(&r.next.to_le_bytes());
            d.extend_from_slice(&first.to_le_bytes());
            d.push(count as u8);
            d.push(self.mine.len() as u8);
            for f in first..first + count {
                for pad in &self.own[&f] {
                    d.extend_from_slice(&pad.wire());
                }
            }
            let hs: Vec<_> = self.hashes.iter().rev().take(HASHES_SENT).collect();
            d.push(hs.len() as u8);
            for (&f, &h) in hs {
                d.extend_from_slice(&f.to_le_bytes());
                d.extend_from_slice(&h.to_le_bytes());
            }
            // our stamp, then the echo of theirs and how long we held it
            d.extend_from_slice(&(now_ms as u32).to_le_bytes());
            let (echo, held) = match r.stamp {
                Some((s, at)) => (s, now_ms.saturating_sub(at).min(u32::MAX as u64 - 1) as u32),
                None => (0, u32::MAX),
            };
            d.extend_from_slice(&echo.to_le_bytes());
            d.extend_from_slice(&held.to_le_bytes());
            self.wire.send(p as u8, &d);
        }
    }

    fn pump(&mut self, now_ms: u64) {
        while let Some((from, d)) = self.wire.recv() {
            // a malformed or stray datagram is dropped
            let _ = self.heard(now_ms, from, &d);
        }
    }

    fn heard(&mut self, now_ms: u64, from: u8, d: &[u8]) -> Option<()> {
        let mut c = Cursor(d);
        if c.u8()? != TAG_FRAMES || c.u8()? != from {
            return None;
        }
        let tick = self.tick;
        let ahead = 2 * self.cfg.delay + 60;
        let own_next = self.own_next;
        let r = self.remotes.get_mut(from as usize)?.as_mut()?;
        let ack = c.u32()?;
        let first = c.u32()?;
        let count = c.u8()? as u32;
        let n = c.u8()? as usize;
        if n != r.seats.len() {
            return None;
        }
        let mut frames = Vec::with_capacity(count as usize);
        for f in first..first.checked_add(count)? {
            let pads = (0..n).map(|_| c.take(PAD_WIRE).map(Pad::unwire)).collect::<Option<Vec<_>>>()?;
            frames.push((f, pads));
        }
        let nh = c.u8()?;
        let mut hashes = Vec::with_capacity(nh as usize);
        for _ in 0..nh {
            hashes.push((c.u32()?, c.u64()?));
        }
        let (stamp, echo, held) = (c.u32()?, c.u32()?, c.u32()?);

        // the whole datagram parsed: take it in
        r.acked = r.acked.max(ack).min(own_next);
        for (f, pads) in frames {
            // run already, or too far ahead to be honest (it resends)
            if f >= r.next && f >= tick && f - tick <= ahead {
                r.frames.entry(f).or_insert(pads);
            }
        }
        while r.frames.contains_key(&r.next) || r.next < tick {
            r.next += 1;
        }
        r.frames.retain(|&f, _| f >= tick);
        if held != u32::MAX {
            let rtt = (now_ms as u32).wrapping_sub(echo).wrapping_sub(held);
            // a wrapped or reordered echo reads as a huge round trip: ignore it
            if rtt < 60_000 {
                r.rtt = Some(match r.rtt {
                    Some(old) => (old * 7 + rtt) / 8,
                    None => rtt,
                });
            }
        }
        r.heard = Some(now_ms);
        if r.stamp.is_none_or(|(s, _)| stamp.wrapping_sub(s) < u32::MAX / 2) {
            r.stamp = Some((stamp, now_ms));
        }
        for (f, h) in hashes {
            self.their_hash(from, f, h);
        }
        Some(())
    }
}

struct Cursor<'a>(&'a [u8]);

impl<'a> Cursor<'a> {
    fn take(&mut self, n: usize) -> Option<&'a [u8]> {
        if self.0.len() < n {
            return None;
        }
        let (a, b) = self.0.split_at(n);
        self.0 = b;
        Some(a)
    }
    fn u8(&mut self) -> Option<u8> {
        Some(self.take(1)?[0])
    }
    fn u32(&mut self) -> Option<u32> {
        Some(u32::from_le_bytes(self.take(4)?.try_into().ok()?))
    }
    fn u64(&mut self) -> Option<u64> {
        Some(u64::from_le_bytes(self.take(8)?.try_into().ok()?))
    }
}

#[cfg(test)]
pub mod fake {
    //! An in-memory wire: every datagram arrives `lag_ms` later (one way), or is lost with probability `loss`.

    use super::Transport;
    use std::cell::RefCell;
    use std::rc::Rc;

    struct Hub {
        now: u64,
        lag_ms: u64,
        loss: f64,
        rng: u64,
        /// (arrives at, from, to, datagram)
        flight: Vec<(u64, u8, u8, Vec<u8>)>,
        sent: u64,
        lost: u64,
        /// Peers whose link is down (N10): everything to and from them is lost.
        cut: u8,
    }

    #[derive(Clone)]
    pub struct Net(Rc<RefCell<Hub>>);

    pub struct End {
        net: Net,
        me: u8,
    }

    impl Net {
        pub fn new(lag_ms: u64, loss: f64, seed: u64) -> Net {
            Net(Rc::new(RefCell::new(Hub { now: 0, lag_ms, loss, rng: seed | 1, flight: Vec::new(), sent: 0, lost: 0, cut: 0 })))
        }
        pub fn end(&self, me: u8) -> End {
            End { net: self.clone(), me }
        }
        pub fn set_now(&self, ms: u64) {
            self.0.borrow_mut().now = ms;
        }
        /// Takes peer `p`'s link down (`down`) or back up; datagrams already in flight still arrive.
        pub fn cut(&self, p: u8, down: bool) {
            let mut h = self.0.borrow_mut();
            if down {
                h.cut |= 1 << p;
            } else {
                h.cut &= !(1 << p);
            }
        }
        /// (sent, lost)
        pub fn counts(&self) -> (u64, u64) {
            let h = self.0.borrow();
            (h.sent, h.lost)
        }
    }

    impl Transport for End {
        fn send(&mut self, to: u8, d: &[u8]) {
            let mut h = self.net.0.borrow_mut();
            // xorshift64
            h.rng ^= h.rng << 13;
            h.rng ^= h.rng >> 7;
            h.rng ^= h.rng << 17;
            h.sent += 1;
            if (h.rng >> 11) as f64 / (1u64 << 53) as f64 <= h.loss || h.cut & (1 << self.me | 1 << to) != 0 {
                h.lost += 1;
                return;
            }
            let at = h.now + h.lag_ms;
            h.flight.push((at, self.me, to, d.to_vec()));
        }
        fn recv(&mut self) -> Option<(u8, Vec<u8>)> {
            let mut h = self.net.0.borrow_mut();
            let now = h.now;
            let i = h.flight.iter().position(|(at, _, to, _)| *at <= now && *to == self.me)?;
            let (_, from, _, d) = h.flight.remove(i);
            Some((from, d))
        }
    }
}

#[cfg(test)]
mod tests {
    use super::fake::{End, Net};
    use super::*;

    fn pad(b: u16, x: i8) -> Pad {
        Pad { button: b, stick_x: x, err: 0, ..IDLE }
    }

    /// Peer `p`'s pad for its seat-th seat on its frame-th local read (a pattern the test can recompute).
    fn input(p: u8, seat: usize, n: u32) -> Pad {
        let v = n.wrapping_mul(2654435761).wrapping_add(p as u32 * 977 + seat as u32 * 31);
        pad((v >> 7) as u16, (v >> 3) as i8)
    }

    struct Run {
        net: Net,
        peers: Vec<Session<End>>,
        /// Every peer's pads as run, frame by frame.
        logs: Vec<Vec<[Pad; 4]>>,
        /// Every peer's local reads so far.
        reads: Vec<u32>,
    }

    fn run(seats: [u8; 4], lag_ms: u64, loss: f64, frames: u32, seed: u64) -> Run {
        run_late(seats, lag_ms, loss, frames, seed, 0)
    }

    /// `run` with the last peer starting `late` frames after the others.
    fn run_late(seats: [u8; 4], lag_ms: u64, loss: f64, frames: u32, seed: u64, late: u32) -> Run {
        let peers = seats.iter().filter(|&&p| p != NO_PEER).max().unwrap() + 1;
        let delay = delay_for(2 * lag_ms as u32);
        let net = Net::new(lag_ms, loss, seed);
        let mut r = Run {
            peers: (0..peers).map(|me| Session::new(Config { me, peers, seats, delay }, net.end(me))).collect(),
            net,
            logs: vec![Vec::new(); peers as usize],
            reads: vec![0; peers as usize],
        };
        for f in 0..frames {
            // 60 Hz real time
            let now = f as u64 * 1000 / 60;
            r.net.set_now(now);
            let last = r.peers.len() - 1;
            for (i, s) in r.peers.iter_mut().enumerate() {
                if i == last && f < late {
                    continue;
                }
                let n = r.reads[i];
                let local: Vec<Pad> = (0..s.local_seats()).map(|k| input(i as u8, k, n)).collect();
                let before = s.own_next;
                if let Step::Ready(p) = s.step(now, &local) {
                    r.logs[i].push(p);
                }
                if s.own_next != before {
                    r.reads[i] += 1;
                }
            }
        }
        r
    }

    /// Every peer ran the same pads on every frame they both ran, and the pads are the ones each peer read.
    fn assert_in_step(r: &Run, seats: [u8; 4]) {
        let delay = r.peers[0].delay() as usize;
        let shortest = r.logs.iter().map(Vec::len).min().unwrap();
        for (i, log) in r.logs.iter().enumerate() {
            assert_eq!(&log[..shortest], &r.logs[0][..shortest], "peer {i} ran other pads");
        }
        for (t, pads) in r.logs[0].iter().enumerate() {
            for s in 0..4 {
                let want = if seats[s] == NO_PEER || t < delay {
                    IDLE
                } else {
                    let k = (0..s).filter(|&o| seats[o] == seats[s]).count();
                    input(seats[s], k, (t - delay) as u32)
                };
                assert_eq!(pads[s], want, "frame {t} seat {s}");
            }
        }
    }

    const TWO: [u8; 4] = [0, 1, NO_PEER, NO_PEER];
    const FOUR: [u8; 4] = [0, 1, 2, 3];

    #[test]
    fn delay_for_matches_opentennis() {
        assert_eq!(delay_for(0), 2);
        assert_eq!(delay_for(30), 2);
        assert_eq!(delay_for(100), 4);
        assert_eq!(delay_for(150), 6);
        assert_eq!(delay_for(300), 10);
        assert_eq!(delay_for(600), 15);
        assert_eq!(delay_for(5000), 15);
    }

    #[test]
    fn pad_wire_round_trip() {
        let p = Pad {
            button: 0x1f7f,
            stick_x: -128,
            stick_y: 127,
            substick_x: -1,
            substick_y: 5,
            trigger_left: 200,
            trigger_right: 1,
            analog_a: 2,
            analog_b: 3,
            err: -1,
            ext_button: 0,
        };
        let w = p.wire();
        assert_eq!(w.len(), 12);
        assert_eq!(Pad::unwire(&w), p);
    }

    #[test]
    fn alone_runs_every_frame_on_its_own_pads() {
        let r = run([0, NO_PEER, 0, NO_PEER], 0, 0.0, 100, 1);
        assert_eq!(r.logs[0].len(), 100);
        assert_in_step(&r, [0, NO_PEER, 0, NO_PEER]);
    }

    fn check(seats: [u8; 4], lag_ms: u64) {
        let frames = 1800;
        let r = run(seats, lag_ms, 0.05, frames, 0x9e3779b97f4a7c15 ^ lag_ms);
        let (sent, lost) = r.net.counts();
        assert!(lost * 100 / sent >= 3, "the wire lost {lost} of {sent}");
        assert_in_step(&r, seats);
        let ran = r.logs.iter().map(Vec::len).min().unwrap() as u32;
        eprintln!("{seats:?} {lag_ms} ms: delay {} ran {ran}/{frames}, lost {lost}/{sent}, rtt {:?}", r.peers[0].delay(), r.peers[0].rtt_ms(1));
        // 300 ms one way needs 19 frames of delay; at the cap of 15 the match runs slower, never apart
        let floor = if delay_for(2 * lag_ms as u32) < 15 { frames * 9 / 10 } else { frames * 3 / 4 };
        assert!(ran >= floor, "{lag_ms} ms: ran {ran} of {frames}");
        for s in &r.peers {
            for p in 0..r.peers.len() as u8 {
                if let Some(rtt) = s.rtt_ms(p) {
                    // one frame of step granularity on each side
                    assert!(rtt.abs_diff(2 * lag_ms as u32) <= 40, "rtt {rtt} at {lag_ms} ms");
                }
            }
        }
    }

    #[test]
    fn two_peers_stay_in_step_at_0_ms_with_loss() {
        check(TWO, 0);
    }

    #[test]
    fn two_peers_stay_in_step_at_150_ms_with_loss() {
        check(TWO, 150);
    }

    #[test]
    fn two_peers_stay_in_step_at_300_ms_with_loss() {
        check(TWO, 300);
    }

    #[test]
    fn four_peers_stay_in_step_at_0_ms_with_loss() {
        check(FOUR, 0);
    }

    #[test]
    fn four_peers_stay_in_step_at_150_ms_with_loss() {
        check(FOUR, 150);
    }

    #[test]
    fn four_peers_stay_in_step_at_300_ms_with_loss() {
        check(FOUR, 300);
    }

    #[test]
    fn two_seats_on_one_peer_and_an_empty_seat() {
        let seats = [1, 0, 1, NO_PEER];
        let r = run(seats, 50, 0.05, 600, 7);
        assert_in_step(&r, seats);
    }

    #[test]
    fn a_peer_starting_late_is_waited_for() {
        let r = run_late(FOUR, 150, 0.05, 900, 5, 120);
        assert_in_step(&r, FOUR);
        // the others held at frame `delay` until it came
        let ran = r.logs.iter().map(Vec::len).min().unwrap() as u32;
        assert!(ran >= (900 - 120) * 9 / 10, "ran {ran}");
        assert!(r.logs[0].len() <= 900 - 120 + 20, "peer 0 ran {} without the late one", r.logs[0].len());
    }

    #[test]
    fn a_silent_peer_holds_everyone() {
        let net = Net::new(0, 0.0, 3);
        let cfg = |me| Config { me, peers: 2, seats: TWO, delay: 2 };
        let mut a = Session::new(cfg(0), net.end(0));
        let _b = Session::new(cfg(1), net.end(1));
        assert!(matches!(a.step(0, &[IDLE]), Step::Ready(_)));
        assert!(matches!(a.step(16, &[IDLE]), Step::Ready(_)));
        assert_eq!(a.step(33, &[IDLE]), Step::Wait);
        assert_eq!(a.waiting_on(), 0b10);
        assert_eq!(a.frame(), 2);
    }

    /// Runs two peers `frames` frames on the same wire, hashing a shared fake state; `forge` makes peer 1's
    /// hash at that frame differ.
    fn hashed(lag_ms: u64, frames: u32, forge: Option<u32>) -> (Vec<Desync>, Vec<Desync>) {
        let net = Net::new(lag_ms, 0.05, 11);
        let delay = delay_for(2 * lag_ms as u32);
        let mut s: Vec<_> = (0..2).map(|me| Session::new(Config { me, peers: 2, seats: TWO, delay }, net.end(me))).collect();
        let mut state = [0u64; 2];
        let mut found = (Vec::new(), Vec::new());
        for f in 0..frames {
            let now = f as u64 * 1000 / 60;
            net.set_now(now);
            for (i, p) in s.iter_mut().enumerate() {
                let t = p.frame();
                if t % HASH_EVERY == 0 {
                    let h = if forge == Some(t) && i == 1 { state[i] ^ 1 } else { state[i] };
                    p.set_hash(t, h);
                }
                if let Step::Ready(pads) = p.step(now, &[input(i as u8, 0, t)]) {
                    // the "game": fold the pads into the state
                    for q in pads {
                        state[i] = (state[i] ^ q.button as u64 ^ (q.stick_x as u8 as u64) << 16).wrapping_mul(0x100000001b3);
                    }
                }
                while let Some(d) = p.desync() {
                    if i == 0 { found.0.push(d) } else { found.1.push(d) }
                }
            }
        }
        found
    }

    #[test]
    fn matching_hashes_report_nothing() {
        let (a, b) = hashed(150, 1200, None);
        assert!(a.is_empty() && b.is_empty(), "{a:?} {b:?}");
    }

    #[test]
    fn a_forged_hash_is_reported_as_desync_on_both_sides() {
        let (a, b) = hashed(150, 1200, Some(300));
        assert_eq!(a.len(), 1, "{a:?}");
        assert_eq!((a[0].frame, a[0].peer), (300, 1));
        assert_eq!(a[0].ours ^ 1, a[0].theirs);
        assert_eq!(b.len(), 1, "{b:?}");
        assert_eq!((b[0].frame, b[0].peer), (300, 0));
        assert_eq!((b[0].ours, b[0].theirs), (a[0].theirs, a[0].ours));
    }

    #[test]
    fn malformed_datagrams_are_dropped() {
        struct Junk(Vec<Vec<u8>>);
        impl Transport for Junk {
            fn send(&mut self, _: u8, _: &[u8]) {}
            fn recv(&mut self) -> Option<(u8, Vec<u8>)> {
                self.0.pop().map(|d| (1, d))
            }
        }
        let mut long = vec![TAG_FRAMES, 1];
        long.extend_from_slice(&[0, 0, 0, 0, 0xff, 0xff, 0xff, 0xff, 255, 1]);
        let junk = Junk(vec![vec![], vec![TAG_FRAMES], vec![TAG_FRAMES, 0, 1, 2], long, vec![TAG_FRAMES, 1, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9]]);
        let mut s = Session::new(Config { me: 0, peers: 2, seats: TWO, delay: 2 }, junk);
        assert!(matches!(s.step(0, &[IDLE]), Step::Ready(_)));
    }

    /// Three peers run to the same last frame `END` on a lossy wire, the last one starting late. A peer that is done
    /// drains (if `drain`; else it goes silent) until drained. Each peer's last frame and pads.
    fn finish(loss: f64, seed: u64, drain: bool) -> Vec<(u32, Vec<[Pad; 4]>)> {
        const END: u32 = 300;
        let net = Net::new(50, loss, seed);
        let delay = delay_for(100);
        let mut peers: Vec<Session<End>> =
            (0..3).map(|me| Session::new(Config { me, peers: 3, seats: [0, 1, 2, NO_PEER], delay }, net.end(me))).collect();
        let mut logs = vec![Vec::new(); 3];
        let mut reads = [0u32; 3];
        let mut drained = [false; 3];
        for f in 0..6_000u32 {
            let now = f as u64 * 1000 / 60;
            net.set_now(now);
            for (i, s) in peers.iter_mut().enumerate() {
                if i == 2 && f < 60 {
                    continue;
                }
                if s.frame() < END {
                    let local = [input(i as u8, 0, reads[i])];
                    let before = s.own_next;
                    if let Step::Ready(p) = s.step(now, &local) {
                        logs[i].push(p);
                    }
                    if s.own_next != before {
                        reads[i] += 1;
                    }
                } else if !drained[i] {
                    drained[i] = !drain || s.drain(now);
                }
            }
        }
        peers.iter().zip(logs).map(|(s, l)| (s.frame(), l)).collect()
    }

    /// A peer that reaches the last frame first drains, so the others reach it too on a lossy wire; one that just goes
    /// silent can strand a peer short of it (the datagrams with its last pads were lost and nobody resends).
    #[test]
    fn a_finished_peer_drains_until_the_others_reach_the_end() {
        let mut stranded = 0;
        for seed in 1..=20u64 {
            let r = finish(0.5, seed, true);
            for (i, (end, log)) in r.iter().enumerate() {
                assert_eq!(*end, 300, "seed {seed}: peer {i} stopped short");
                assert_eq!(log, &r[0].1, "seed {seed}: peer {i} ran other pads");
            }
            stranded += finish(0.5, seed, false).iter().any(|(end, _)| *end < 300) as u32;
        }
        assert!(stranded > 0, "going silent never stranded a peer: the test proves nothing");
        // a drain takes no pads and runs nothing
        let net = Net::new(0, 0.0, 1);
        let mut s = Session::new(Config { me: 0, peers: 1, seats: [0, NO_PEER, NO_PEER, NO_PEER], delay: 2 }, net.end(0));
        s.step(0, &[IDLE]);
        let own = s.own_next;
        assert!(s.drain(0));
        assert_eq!((s.frame(), s.own_next), (1, own));
    }
}
