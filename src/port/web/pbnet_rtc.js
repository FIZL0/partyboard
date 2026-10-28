// N92: the page's side of pbnet's browser transport (net/src/rtc.rs, `mod js`), linked with --js-library. Each
// connection is an RTCPeerConnection with two data channels negotiated up front: 0 reliable and ordered (the lobby's
// JSON messages), 1 unordered with no retransmits (the lockstep's datagrams). Everything here returns at once and
// never calls into wasm: pbnet polls the descriptions, the states and the queues from the game's frames.
addToLibrary({
  $PBRTC: {
    next: 1,
    conns: {},
    // ICE gathering waits at most this long (host candidates take well under a second; STUN may not answer).
    GATHER_MS: 3000,
    gathered(pc) {
      return new Promise(done => {
        if (pc.iceGatheringState === 'complete') return done();
        const t = setTimeout(done, PBRTC.GATHER_MS);
        pc.addEventListener('icegatheringstatechange', () => {
          if (pc.iceGatheringState === 'complete') clearTimeout(t), done();
        });
      });
    },
    // Runs `make` (an offer or answer promise), sets it as the local description and keeps the gathered SDP.
    async describe(c, make) {
      try {
        await c.pc.setLocalDescription(await make());
        await PBRTC.gathered(c.pc);
        c.local = c.pc.localDescription.sdp;
      } catch (e) {
        console.error('pbnet: WebRTC description:', e);
        c.failed = true;
      }
    },
  },

  pbnet_js_rtc_open__deps: ['$PBRTC', '$UTF8ToString'],
  pbnet_js_rtc_open: function (ice) {
    if (typeof RTCPeerConnection === 'undefined') return -1;
    let pc;
    try {
      pc = new RTCPeerConnection({ iceServers: JSON.parse(UTF8ToString(ice) || '[]') });
    } catch (e) {
      console.error('pbnet: RTCPeerConnection:', e);
      return -1;
    }
    const id = PBRTC.next++;
    const c = { pc, local: null, failed: false, rtt: -1, queues: [[], []], channels: [] };
    c.channels = [
      pc.createDataChannel('lobby', { negotiated: true, id: 0, ordered: true }),
      pc.createDataChannel('lockstep', { negotiated: true, id: 1, ordered: false, maxRetransmits: 0 }),
    ];
    c.channels.forEach((ch, i) => {
      ch.binaryType = 'arraybuffer';
      ch.onmessage = e => c.queues[i].push(new Uint8Array(e.data instanceof ArrayBuffer ? e.data : new TextEncoder().encode(e.data)));
    });
    // the selected pair's round trip, for the host's input delay
    c.timer = setInterval(async () => {
      if (pc.connectionState !== 'connected') return;
      try {
        (await pc.getStats()).forEach(s => {
          if (s.type === 'candidate-pair' && s.nominated && s.state === 'succeeded' && s.currentRoundTripTime !== undefined) {
            c.rtt = Math.round(s.currentRoundTripTime * 1000);
          }
        });
      } catch (e) {}
    }, 500);
    PBRTC.conns[id] = c;
    return id;
  },

  pbnet_js_rtc_offer__deps: ['$PBRTC'],
  pbnet_js_rtc_offer: function (id) {
    const c = PBRTC.conns[id];
    if (c) PBRTC.describe(c, () => c.pc.createOffer());
  },

  pbnet_js_rtc_answer__deps: ['$PBRTC', '$UTF8ToString'],
  pbnet_js_rtc_answer: function (id, offer) {
    const c = PBRTC.conns[id];
    if (!c) return;
    // N98: the offer's candidates go in once this side's are gathered. With them in the offer, Chromium ended the
    // answer's gathering without its STUN (server-reflexive) candidate, which a guest behind a NAT needs to be
    // reached; without them it gathers it.
    const lines = UTF8ToString(offer).split('\r\n');
    const theirs = lines.filter(l => l.startsWith('a=candidate:')).map(l => l.slice(2));
    const sdp = lines.filter(l => !l.startsWith('a=candidate:') && l !== 'a=end-of-candidates').join('\r\n');
    const mid = (lines.find(l => l.startsWith('a=mid:')) ?? 'a=mid:0').slice(6);
    PBRTC.describe(c, async () => {
      await c.pc.setRemoteDescription({ type: 'offer', sdp });
      return c.pc.createAnswer();
    }).then(() => Promise.all(theirs.map(candidate => c.pc.addIceCandidate({ candidate, sdpMid: mid, sdpMLineIndex: 0 }))))
      .catch(e => {
        console.error('pbnet: WebRTC candidates:', e);
        c.failed = true;
      });
  },

  pbnet_js_rtc_accept__deps: ['$PBRTC', '$UTF8ToString'],
  pbnet_js_rtc_accept: function (id, answer) {
    const c = PBRTC.conns[id];
    if (!c) return;
    c.pc.setRemoteDescription({ type: 'answer', sdp: UTF8ToString(answer) }).catch(e => {
      console.error('pbnet: WebRTC answer:', e);
      c.failed = true;
    });
  },

  pbnet_js_rtc_local__deps: ['$PBRTC', '$stringToUTF8', '$lengthBytesUTF8'],
  pbnet_js_rtc_local: function (id, buf, cap) {
    const c = PBRTC.conns[id];
    if (!c || c.failed) return -1;
    if (c.local === null) return 0;
    const n = lengthBytesUTF8(c.local);
    if (n < cap) stringToUTF8(c.local, buf, cap);
    return n;
  },

  pbnet_js_rtc_state__deps: ['$PBRTC'],
  pbnet_js_rtc_state: function (id) {
    const c = PBRTC.conns[id];
    if (!c || c.failed || ['failed', 'closed'].includes(c.pc.connectionState)) return 2;
    const states = c.channels.map(ch => ch.readyState);
    if (states.some(s => s === 'closing' || s === 'closed')) return 2;
    return states.every(s => s === 'open') ? 1 : 0;
  },

  pbnet_js_rtc_send__deps: ['$PBRTC'],
  pbnet_js_rtc_send: function (id, chan, data, len) {
    const ch = PBRTC.conns[id]?.channels[chan];
    if (!ch || ch.readyState !== 'open') return 0;
    try {
      ch.send(HEAPU8.slice(data, data + len));
      return 1;
    } catch (e) {
      return 0;
    }
  },

  pbnet_js_rtc_recv__deps: ['$PBRTC'],
  pbnet_js_rtc_recv: function (id, chan, buf, cap) {
    const q = PBRTC.conns[id]?.queues[chan];
    if (!q || q.length === 0) return -1;
    const m = q[0];
    if (m.length > cap) return m.length;
    HEAPU8.set(m, buf);
    q.shift();
    return m.length;
  },

  pbnet_js_rtc_rtt__deps: ['$PBRTC'],
  pbnet_js_rtc_rtt: function (id) {
    return PBRTC.conns[id]?.rtt ?? -1;
  },

  pbnet_js_rtc_close__deps: ['$PBRTC'],
  pbnet_js_rtc_close: function (id) {
    const c = PBRTC.conns[id];
    if (!c) return;
    clearInterval(c.timer);
    try {
      c.pc.close();
    } catch (e) {}
    delete PBRTC.conns[id];
  },
});
