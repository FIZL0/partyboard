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
 * One real frame. local: this machine's seats' pads in seat order (n_local of them; NULL if 0).
 * out: 4 writable PADStatus, written only on PBNET_READY. Pads passed while it returns PBNET_WAIT are not
 * taken. extButton is not sent and arrives as 0.
 */
int pbnet_step(const struct PADStatus *local, int n_local, struct PADStatus *out);
/* The next frame pbnet_step runs (0 with no session). */
uint32_t pbnet_frame(void);
/* Bit per line-up index of the peers the next frame waits for. */
uint32_t pbnet_waiting_on(void);

/* Hash of the state before `frame`: call when pbnet_frame() % 30 == 0, before that frame's pbnet_step. */
void pbnet_set_hash(uint32_t frame, uint64_t hash);
/* Oldest hash mismatch: 1 and its fields (NULL pointers skipped), or 0 if none. */
int pbnet_poll_desync(uint32_t *frame, int *peer, uint64_t *ours, uint64_t *theirs);

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
 *    "resume": "<online save path>" (host, optional: continue that save, N9)}
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
 *    ready [peer names], ver, started, delay), "gen": n}
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
 * Drops never end a match (net/src/recover.rs, N10). After the start a guest whose connection drops redials the
 * host and rejoins with its token: the lockstep resumes on the same frame, or (a peer that lost its state, a CPU
 * stand-in) every peer reloads the room's last checkpoint as a new epoch of the started lobby.
 *
 * pbnet_recover_set_dir: the user dir (config, saves); the rejoin token is kept in <dir>/rejoin.txt. Call before
 *   hosting or joining.
 * pbnet_rejoin_record: the match this machine can rejoin (JSON {"code","id","token","at"}, snprintf-style), 0 if none.
 * pbnet_rejoin_forget: forget it. pbnet_lobby_rejoin: rejoin it (setup as pbnet_lobby_join), 1 if dialling.
 * pbnet_recover_json: {"running","host","epoch","waiting":[{"index","name","seats","quiet_ms"}],"reconnecting",
 *   "host_left","reloading","message"}, snprintf-style. waiting: peers silent for 3 s (the lockstep holds on its own);
 *   message: "Waiting for <name>" or, on a guest, "Host left: the match is saved, any player can host Continue".
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
