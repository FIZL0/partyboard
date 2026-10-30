#pragma once

/*
 * C ABI of the `pbnet` Rust crate (net/). Hand-written: keep in sync with the
 * `extern "C"` fns in net/src/lib.rs, net/src/lockstep_c.rs, net/src/lobby_c.rs, net/src/save.rs and
 * net/src/recover.rs.
 */

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* aurora's PADStatus (TARGET_PC layout, 16 bytes); pbnet mirrors it as lockstep::Pad. */
struct PADStatus;

/*
 * Version of pbnet as a NUL-terminated string, e.g. "0.1.0".
 * Never NULL; points to static storage (do not free or modify). Thread-safe.
 */
const char *pbnet_version(void);

/*
 * Lockstep (net/src/lockstep.rs, C ABI in net/src/lockstep_c.rs). One session per process, started from
 * Rust at the match start (the lobby, when it reaches PBNET_LOBBY_STARTED); C only steps it. All calls are thread-safe; with no session they do nothing.
 */

#define PBNET_READY 1 /* run the frame on out[0..4] */
#define PBNET_WAIT 0  /* skip the game tick, call again next real frame */
#define PBNET_OFF (-1) /* no lockstep session */

/* 1 if a lockstep session runs, else 0. */
int pbnet_lockstep_active(void);
/* Ends the lockstep session, if one runs. */
void pbnet_lockstep_stop(void);
/*
 * Ends the lockstep session after its last frame (every peer ends after the same one): pbnet_lockstep_active is 0
 * at once, while a pbnet thread keeps sending the peers the pads they still lack (up to 5 s), so a peer still
 * behind reaches the same last frame.
 */
void pbnet_lockstep_finish(void);

/*
 * One real frame. local: this machine's seats' pads in seat order (n_local of them; NULL if 0).
 * out: 4 writable PADStatus, written only on PBNET_READY. Pads passed while it returns PBNET_WAIT are not
 * taken. extButton is not sent and arrives as 0.
 */
int pbnet_step(const struct PADStatus *local, int n_local, struct PADStatus *out);
/* The next frame pbnet_step runs (0 with no session). */
uint32_t pbnet_frame(void);
/* Bit per line-up index of the peers the next frame waits for. */
uint32_t pbnet_waiting_on(void);

/*
 * N110 (net/src/keeper.rs): the player switched away (the app went to the background, the window was minimised or
 * hidden) and the game holds its frames: pbnet_keeper_hidden(1) when the hold starts, (0) when frames run again;
 * pbnet_keeper_frame() once per real frame held. The peers hold with "Waiting for <name>: their game is in the
 * background" instead of hearing nothing; a keeper thread speaks while the game's own loop is stopped too.
 */
void pbnet_keeper_hidden(int hidden);
void pbnet_keeper_frame(void);

/* Hash of the state before `frame`: call when pbnet_frame() % 30 == 0, before that frame's pbnet_step. */
void pbnet_set_hash(uint32_t frame, uint64_t hash);
/* Oldest hash mismatch: 1 and its fields (NULL pointers skipped), or 0 if none. */
int pbnet_poll_desync(uint32_t *frame, int *peer, uint64_t *ours, uint64_t *theirs);
/*
 * A mismatch from pbnet_poll_desync (net/src/desync.rs, N11, N46). The first one of a lockstep session writes its input
 * log (pads of the frames run, its hashes) to <user dir>/desync-<match>-<time>-e<epoch>-f<frame>-p<index>.json (the
 * current directory without pbnet_recover_set_dir); both sides of a mismatch see it, so both write theirs. On the
 * host it also has every peer resync: reload the newest resync point (pbnet_resync_point, or the last online save;
 * pbnet_recover_epoch moves, as any N10 reload). Never refused: one more from the same point holds the game 2, 4, 8,
 * 16 s first and goes from pbnet_resync_poll. A guest shows "Resyncing with <host>..." and waits for the host.
 * 1 if a resync was asked now; mismatches while one is under way do nothing.
 */
int pbnet_desync_recover(uint32_t frame, int peer, uint64_t ours, uint64_t theirs);
/* N46: call once per loop on the game thread (netplay_lobby_poll does): a held-back resync goes when its wait is over. */
int pbnet_resync_poll(void);
/*
 * N46: blob (SLSaveBoard, len bytes; turn/max_turn of GWSystem) is the board state at the start of a player's turn:
 * this machine's newest resync point of the started match (memory only). 1 if taken, 0 without a started lobby.
 */
int pbnet_resync_point(const uint8_t *blob, int len, int turn, int max_turn);

/* Input delay (frames, 2..15) for a round trip of rtt_ms: one-way 60 Hz frames + 1. */
uint32_t pbnet_delay_for(uint32_t rtt_ms);

/*
 * Lobby (net/src/lobby.rs, on the network and C ABI in net/src/lobby_c.rs). One lobby per process; its network
 * runs on pbnet's own threads, so every call returns at once and the menu polls. All calls are thread-safe.
 *
 * setup (JSON) for host and join:
 *   {"build": "<version and commit>", "disc": "GMPE01-00", "players": [{"name": "...", "key": "<stable id>"}, ...],
 *    "relay": "n0" | "own" | "off", "relay_url": "https://..." (own only),
 *    "id": "<match id>" (host, optional), "short": true (host: put a short code online, default true),
 *    "resume": "<online save path>" (host, optional: continue that save, N9),
 *    "webrtc": true (N96, desktop host, optional: host for browsers, WebRTC offer codes and answers)}
 *  A join whose code is a browser's offer (rtco...) runs on WebRTC by itself.
 * players: this machine's 1..4 local players, in the order they take seats.
 */

#define PBNET_LOBBY_NONE 0       /* no lobby */
#define PBNET_LOBBY_CONNECTING 1 /* going online (host) / dialling, waiting for a seat (guest) */
#define PBNET_LOBBY_OPEN 2       /* hosting / seated */
#define PBNET_LOBBY_STARTED 3    /* the match started: the room is final (seed, CPU seats) */
#define PBNET_LOBBY_CLOSED 4     /* refused, host left, connection failed: "error" in the JSON says why */

/* Hosts a lobby (leaving one that runs). 1 if going online, 0 if setup is not valid. */
int pbnet_lobby_host(const char *setup);
/* Joins the lobby at code (short code, ticket, endpoint ID or a link with ?code=), leaving one that runs.
 * 1 if dialling, 0 if setup is not valid or code empty. */
int pbnet_lobby_join(const char *setup, const char *code);
/* Leaves: the host's lobby closes for everyone, a guest's seats open; a started lobby's lockstep stops. Then
 * PBNET_LOBBY_NONE. */
void pbnet_lobby_leave(void);
/*
 * This machine's started match is over (after pbnet_lockstep_finish): back to PBNET_LOBBY_OPEN with the same
 * people. The host reopens the room for everyone (new match id, nobody ready, seats and rules kept); a guest takes
 * the host's room after the match, heard before or after this call. 1 if the lobby was started, else 0.
 */
int pbnet_lobby_back(void);
/* PBNET_LOBBY_* */
int pbnet_lobby_state(void);
/* Moves on every change to the lobby (0 with none): re-read the JSON when it does. */
uint32_t pbnet_lobby_gen(void);
/*
 * The lobby as JSON, snprintf-style: at most cap-1 bytes and a NUL into buf (NULL: none), returns the full
 * length (>= cap: call again with a bigger buffer). With no lobby {"state":"none"}, else
 *   {"state": "connecting"|"open"|"started"|"closed", "host": bool, "me": "<peer name>",
 *    "code": "<host: ticket>", "short": "<host: short code, once online>", "error": "<why closed / no short code>",
 *    "seats": [this machine's seat indices], "room": null | Room (net/src/lobby.rs: id, seats[4] of null |
 *    {peer, name, key, character, cpu, difficulty, team}, board, turns, bonus, teams, handicaps[4], seed,
 *    ready [peer names], ver, started, delay), "gen": n,
 *    "answer": "<WebRTC guest while it connects: its answer>" (when set), "webrtc": true (N96: a WebRTC lobby; left
 *    out otherwise)}
 */
int pbnet_lobby_json(char *buf, int cap);
/* This machine's picks for its seats in seat order (n of them). 1 if sent (open lobby only). */
int pbnet_lobby_picks(const uint8_t *characters, const uint8_t *teams, int n);
/* Guest: ready (nonzero) or not for the room it last saw; a change to the room clears it. 1 if sent. */
int pbnet_lobby_ready(int ready);
/* Host: the rules (clamped to the game's: board 0..5, turns 10..50 by 5, handicaps 0..9; NULL handicaps: 0). */
int pbnet_lobby_settings(int board, int turns, int bonus, int teams, const uint8_t *handicaps);
/* Host: the computer plays seat (0..3) at difficulty (0..3), or the seat opens again (difficulty < 0). */
int pbnet_lobby_cpu(int seat, int difficulty);
/* N149, continue lobby: this machine's player in seat `from` takes seat `to`, one the computer plays (the host
 * decides). In a continue lobby pbnet_lobby_cpu on a guest's seat gives it back to the computer. 1 if sent. */
int pbnet_lobby_take(int from, int to);
/* Host: starts once every guest is ready (open seats become CPUs); seed 0: from the clock. Every peer's lockstep
 * session then runs on the lobby's connections (pbnet_lockstep_active), delay from the host's round trips. 1 if
 * started, else 0 and the reason snprintf'd into why/cap (why may be NULL). */
int pbnet_lobby_start(uint64_t seed, char *why, int cap);

/*
 * The board checkpoint of a started continue (the online save's SLSaveBoard blob the host sent in the Room, N9):
 * copies at most cap bytes into buf (NULL: none) and returns the blob's length; 0 if the lobby's started match is a
 * new one (no checkpoint).
 */
int pbnet_lobby_checkpoint(uint8_t *buf, int cap);

/*
 * Online saves (net/src/save.rs, N9): <user_dir>/online/<match id>.json, one per match, rewritten at every checkpoint.
 * Thread-safe; file IO on the caller's thread.
 *
 * pbnet_save_write: the save of a board checkpoint. The room is the lobby's started one, else room_json (a Room as
 * JSON, for a match with no lobby); blob/len the SLSaveBoard blob; seeds: session, frand, rand8, BoardRand (NULL: 0).
 * 1 if written (its path snprintf'd into path/cap, path may be NULL), 0 if not (logged).
 */
int pbnet_save_write(const char *user_dir, const char *room_json, const uint8_t *blob, int len, uint32_t frame,
                     const uint32_t *seeds, char *path, int cap);
/*
 * The saves as a JSON array, newest first, snprintf-style (returns the full length):
 *   [{"id", "path", "time" (Unix s), "board", "turn", "max_turn", "seats": [{"name", "character", "cpu"}]}]
 * Continue one: pbnet_lobby_host with "resume": "<path>" in the setup.
 */
int pbnet_save_list(const char *user_dir, char *buf, int cap);
/* The blob of the save file at path (copies at most cap bytes, returns its length; 0 if not a readable save). */
int pbnet_save_blob(const char *path, uint8_t *buf, int cap);
/* Deletes a save (a path from pbnet_save_list; nothing outside an online/ dir). 1 if deleted. */
int pbnet_save_delete(const char *path);
/*
 * The match is over (N31): deletes its save, so Continue never lists a finished match. The match is the lobby's
 * started room, else room_json's (as pbnet_save_write). 1 if a save was deleted, 0 if none (or no match).
 */
int pbnet_save_finish(const char *user_dir, const char *room_json);

/*
 * LAN games (net/src/lobby_c.rs Browse). An open host lobby lists itself by mDNS on the local network: its room id,
 * host name (first local player) and open seats, kept up to date and taken down at the start or close.
 *
 * pbnet_lan_browse lists the lobbies heard, as JSON snprintf-style (as pbnet_lobby_json; returns -1 if setup is not
 * JSON). Its first call binds an endpoint with no lobby (on pbnet's threads, so it returns at once: poll it); setup is
 * the lobby setup JSON or NULL, and only its "relay"/"relay_url" are read (another relay setting rebinds). Then
 *   {"state": "binding"|"open"|"closed", "lan": bool (mDNS works on this device), "error": "<why closed>",
 *    "games": [{"code": "<host's endpoint ID: pass to pbnet_lobby_join>", "id": "<match id>", "name": "...",
 *               "open": n}, ...]}
 * pbnet_lobby_join dials from that endpoint when its relay setting matches, and the browse ends; pbnet_lobby_host
 * ends it too.
 */
int pbnet_lan_browse(const char *setup, char *buf, int cap);
/* Ends the LAN browse, if one runs. */
void pbnet_lan_stop(void);

/*
 * N92, the browser build (net/src/rtc.rs): WebRTC needs a code each way. The host's code is an offer; a guest that
 * joins with it gets an answer (the lobby JSON's "answer", while it connects) to send back, and the host passes it
 * here: 1 if taken (the guest connects and is seated; if it can't, the lobby's "error" says so), else 0 and why
 * snprintf'd into why/cap (why may be NULL). The host's code then changes (a fresh offer for the next guest).
 * N96: the desktop speaks it too, in a lobby hosted for browsers (setup "webrtc": true) or joined with a browser's
 * offer code (the lobby JSON says "webrtc": true); 0 in its own (iroh) lobbies, whose guests need no answer.
 */
int pbnet_lobby_answer(const char *answer, char *why, int cap);

/*
 * Drops never end a match (net/src/recover.rs, N10). After the start a guest whose connection drops redials the
 * host and rejoins with its token: the lockstep resumes on the same frame, or (a peer that lost its state, a CPU
 * stand-in) every peer reloads the room's last checkpoint as a new epoch of the started lobby.
 *
 * pbnet_recover_set_dir: the user dir (config, saves); the rejoin token is kept in <dir>/rejoin.txt. Call before
 *   hosting or joining.
 * pbnet_rejoin_record: the match this machine can rejoin (JSON {"code","id","token","at"}, snprintf-style), 0 if none.
 * pbnet_rejoin_forget: forget it. pbnet_lobby_rejoin: rejoin it (setup as pbnet_lobby_join), 1 if dialling.
 * pbnet_recover_json: {"running","host","epoch","waiting":[{"index","name","seats","quiet_ms","hidden"}],"reconnecting",
 *   "host_left","reloading","message","resync","resyncs"}, snprintf-style. waiting: peers silent for 3 s (the lockstep holds on its own)
 *   or whose player switched away (hidden, N110); message: "Waiting for <name>" (": their game is in the background"
 *   when hidden) or, on a guest, "Host left: the match is saved, any player can host Continue".
 *   resync (N46): "Resyncing with <host>..." while a desync is mended (a notice: play goes on), else ""; resyncs: the
 *   host's count of resyncs asked.
 * pbnet_recover_epoch: 0 from the start, one more per reload. When it moves while the lobby is started, start the
 *   room again (pbnet_lobby_checkpoint has the checkpoint) and call pbnet_recover_go(epoch): until then every
 *   pbnet_step returns PBNET_WAIT. pbnet_recover_go returns 1 if that epoch's lockstep runs (or nothing was held).
 * pbnet_recover_cpu (host): the computer plays the seats of line-up index `index` (a waiting peer, 1..3); every peer
 *   reloads the last checkpoint. The player takes them back when it rejoins. 1 if done.
 * Host save and quit is pbnet_lobby_leave: every peer already has the last checkpoint's online save.
 */
void pbnet_recover_set_dir(const char *dir);
int pbnet_rejoin_record(char *buf, int cap);
void pbnet_rejoin_forget(void);
int pbnet_lobby_rejoin(const char *setup);
int pbnet_recover_json(char *buf, int cap);
uint32_t pbnet_recover_epoch(void);
int pbnet_recover_go(uint32_t epoch);
int pbnet_recover_cpu(int index);

#ifdef __ANDROID__
/*
 * Android (net/src/android.rs): sets up the phone's certificate check for the short codes' HTTPS. Call once at boot
 * on SDL's thread with SDL_GetAndroidJNIEnv() and SDL_GetAndroidActivity() (a JNIEnv* and a jobject; the activity
 * may be deleted after). 1 if on, 0 if not (then the bundled roots; also with a NULL argument).
 */
int pbnet_android_init_tls(void *env, void *activity);
#endif

#ifdef __cplusplus
}
#endif
