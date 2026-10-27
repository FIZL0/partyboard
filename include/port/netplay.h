#pragma once

/*
 * C side of online play (src/port/netplay.c). See plan/NET.md.
 *
 * While a session is active the game must run identically on every peer, so:
 *  - OSGetTime/OSGetTick return a value derived from the netplay frame counter and the session seed
 *    (linker-wrapped, see cmake/netplay.cmake);
 *  - frand, rand8 and BoardRand are seeded from the session seed;
 *  - netplay_hash() hashes the game state that has to match between peers.
 */

#include <dolphin/types.h>
#include <dolphin/os.h>
#include <dolphin/pad.h>

#ifdef __cplusplus
extern "C" {
#endif

/* True while a netplay session (or a --script debug run) is driving the game. */
BOOL netplay_active(void);

/* Start a session: resets the frame counter and the game's random seeds from `seed`. */
void netplay_begin(u32 seed);
void netplay_end(void);

u32 netplay_session_seed(void);
/* Number of game ticks completed since netplay_begin. */
u32 netplay_frame(void);

/* Salts for netplay_seed, one per random generator. */
#define NETPLAY_SALT_FRAND 1
#define NETPLAY_SALT_RAND8 2
#define NETPLAY_SALT_BOARD 3

/* A nonzero seed for `salt`, derived from the session seed and the current frame. Same on every peer. */
u32 netplay_seed(u32 salt);

/* Deterministic replacements for OSGetTime/OSGetTick while netplay_active(). */
OSTime netplay_time(void);

/* Call once at the end of every game tick while netplay_active(). */
void netplay_frame_end(void);

/*
 * Lockstep gate (pbnet): call once per real frame, before the game tick, while netplay_active(). Steps
 * the pbnet session with this machine's pads and returns FALSE when the frame has to wait for a peer
 * (skip the game tick, keep pumping events, UI and the frame limiter). TRUE with no lockstep session.
 */
BOOL netplay_tick_begin(void);
/* True while a pbnet lockstep session drives the game. */
BOOL netplay_lockstep(void);
/*
 * The game's PADRead while netplay_active(): the scripted pads (--script), the pads of the last frame
 * pbnet_step made ready (lockstep), else this machine's controllers. Returns no rumble bits.
 */
u32 netplay_pad_read(PADStatus *status);

/*
 * 64-bit FNV-1a hash of GWSystem, GWPlayer[4], GWPlayerCfg[4], the random seeds (frand, rand8,
 * BoardRand) and the HuPad state. Equal on every peer as long as they stay in step.
 */
u64 netplay_hash(void);

/*
 * Debug: parse `--script <file>` and `--hash-log <file>` from the command line. With --script the
 * game starts a local session at boot and plays the scripted pads; with --hash-log every frame's
 * netplay_hash() is written as "<frame> <hash> <section>=<value>...". Returns FALSE (and logs why) if a file can't be used.
 */
BOOL netplay_debug_init(int argc, char *argv[]);
/* True if --script was given (the prelaunch UI is skipped so runs need no clicks). */
BOOL netplay_debug_scripted(void);

/* Seed accessors for the hash and session start (frand_seed is static in src/game/frand.c). */
u32 frandSeedGet(void);
void frandSeedSet(u32 seed);

#ifdef __cplusplus
}
#endif
