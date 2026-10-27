#pragma once

/*
 * C ABI of the `pbnet` Rust crate (net/). Hand-written: keep in sync with the
 * `extern "C"` fns in net/src/lib.rs and net/src/lockstep_c.rs.
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
 * Rust at the match start; C only steps it. All calls are thread-safe; with no session they do nothing.
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
