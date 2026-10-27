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
 * Still frame for a lockstep WAIT (src/port/netplay_still.c): netplay_still_keep copies the EFB, call it
 * at the end of every lockstep tick (after the game drew, before aurora_end_frame); netplay_still_draw
 * draws the last copy over the whole EFB in a WAIT frame (between aurora_begin_frame and aurora_end_frame,
 * before ui_update), so RmlUi has the game's last image under it. netplay_still_drop frees the copy.
 */
void netplay_still_keep(void);
void netplay_still_draw(void);
void netplay_still_drop(void);
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
 * netplay_hash() is written as "<frame> <hash> <section>=<value>...". `--match <board>@<frame>` (with
 * --script) starts a match on that board after that frame (netplay_match_parse_debug); with `--continue <online
 * save .json>` that match continues the save instead (N9). Returns FALSE (and logs why) if a file or argument
 * can't be used.
 */
BOOL netplay_debug_init(int argc, char *argv[]);
/* True if --script was given (the prelaunch UI is skipped so runs need no clicks). */
BOOL netplay_debug_scripted(void);

/*
 * Match start (src/port/netplay_match.c). The lobby's Room as the game needs it; N20/N7 fill it from the
 * Room of the lobby's Start message, the same on every peer.
 */
#define NETPLAY_BOARDS 6
typedef struct {
    u8 character;  /* 0..7 (GWPlayerCfg.character), unique */
    u8 cpu;        /* the computer plays this seat */
    u8 difficulty; /* CPU difficulty 0..3 (GWPlayerCfg.diff) */
    u8 team;       /* 0 or 1 (GWPlayerCfg.group) when teams is set */
    u8 handicap;   /* stars the seat starts with, 0..9 */
} NetplaySeat;

typedef struct {
    u64 seed;               /* Room.seed */
    u8 board;               /* 0..5: w01..w06 (GWSystem.board) */
    u8 turns;               /* 10..50, steps of 5 */
    u8 bonus;               /* bonus stars */
    u8 teams;               /* 2 vs 2 by seats[].team */
    NetplaySeat seats[4];   /* 1P..4P; seat i plays with lockstep pad i */
} NetplayMatch;

/* NULL if the match can be played, else why not (static string). */
const char *netplay_match_invalid(const NetplayMatch *m);
/*
 * Starts the match: call on the game thread outside the game tick (e.g. from the UI's update), on every peer
 * with the same `m`, once the lockstep session runs (or with none, for a local --match run). Starts the
 * netplay session (netplay_begin) at once; the next netplay_tick_begin puts every peer in the same state
 * (see netplay_match.c) before frame 0's hash and pads, and that frame enters the board. FALSE (and logs
 * why) if `m` is invalid or a match is already starting.
 */
BOOL netplay_match_start(const NetplayMatch *m);
/* True from netplay_match_start until netplay_end. */
BOOL netplay_match_running(void);
/* netplay_tick_begin: applies a started match's state, once, before anything else of frame 0. */
void netplay_match_tick(void);
/* objmain.c omWatchOverlayProc, while netplay_active(): `ovl` is about to start. */
void netplay_ovl_start(s32 ovl);
/*
 * True once the match is over: netplay_ovl_start saw a menu overlay (modeseldll, mentDll, bootDll) start after the
 * board (board -> mstory3dll -> modeseldll), and gave the player's own GWGameStat back before it set up. Cleared by
 * netplay_match_end. netplay_frame_end then ends the session at the end of that frame (the same on every peer):
 * pbnet_lockstep_finish, netplay_end, pbnet_lobby_back; a --script run only ends the match and keeps its session.
 */
BOOL netplay_match_over(void);
/* netplay_end: gives the player's own GWGameStat back. */
void netplay_match_end(void);
/* TRUE once after an online match ended and its lobby went back to open (or closed meanwhile): show the lobby. */
BOOL netplay_lobby_returned(void);
/* FNV-1a of every heap's used blocks (heap, offset, size, num): equal when peers' heaps are laid out alike. */
u64 netplay_heap_hash(void);
/* "--match <board>@<frame>": parse a debug match (seat 1P human, 2P-4P CPU) for a --script run. */
BOOL netplay_match_parse_debug(const char *spec, NetplayMatch *m, u32 *at_frame);

/*
 * Continue (N9, netplay_match.c): as netplay_match_start, but the board state is the checkpoint `blob` (an online
 * save's SLSaveBoard blob: the save file's or the lobby's, pbnet_lobby_checkpoint) instead of a new party. Board,
 * turn, characters, teams and coins/stars are the blob's; from `m` only the seed and each seat's cpu and difficulty
 * (who plays the seat now: unclaimed seats are the CPU's) are used. Every peer must pass the same `m` and blob. FALSE
 * (and logs why) if the blob is not a board checkpoint of this build or `m` is refused by netplay_match_start.
 */
BOOL netplay_match_continue(const NetplayMatch *m, const void *blob, u32 size);

/*
 * Online saves (N9, src/port/netplay_save.c). The checkpoint blob is what SLSaveBoard writes into the card buffer
 * (GWSystem + GWPlayer[4], big-endian), taken and given back without changing the local card's buffer.
 */
#define NETPLAY_BLOB_MAX 1024
u32 netplay_board_blob_size(void);
/* SLSaveBoard of the current state into out (cap bytes); its size, 0 if cap is too small. */
u32 netplay_board_blob_get(u8 *out, u32 cap);
/* SLLoadBoard of blob (GWSystem, GWPlayer, GWPlayerCfg from it); FALSE if size is not netplay_board_blob_size(). */
BOOL netplay_board_blob_set(const u8 *blob, u32 size);
/* GWSystem.board of blob without loading it (backs up and restores the state); -1 if it is no board blob. */
s32 netplay_board_blob_board(const u8 *blob, u32 size);
/*
 * resultDll, at the board checkpoint (where SaveExec writes the card), while netplay_active(): during a match, every
 * peer writes <user dir>/online/<match id>.json (pbnet_save_write) and keeps the blob (netplay_checkpoint_last).
 */
void netplay_checkpoint(void);
/* The last checkpoint's blob of this match into out (cap bytes): its size, 0 if none yet. */
u32 netplay_checkpoint_last(u8 *out, u32 cap);
/* netplay_match.c: forget the last checkpoint (size 0: a match starts or ends) or take `blob` as it (a continue). */
void netplay_checkpoint_set(const u8 *blob, u32 size);
/* The user dir (config, saves), UTF-8; never NULL ("" before it is known). src/port/netplay_dir.cpp. */
const char *netplay_user_dir(void);
/* The Room JSON for a save when no lobby runs (a --match debug run): from the match, id "local-<seed>". */
int netplay_match_room_json(char *buf, int cap);

/*
 * Lobby to match (src/port/netplay.c). netplay_match_from_lobby reads a started lobby's JSON (pbnet_lobby_json)
 * into `m`: NULL if it is a playable match, else why not (static string). local_seats (may be NULL) gets bit i
 * set for each seat this machine plays (pbnet feeds them local pads 0, 1, ... in seat order).
 */
const char *netplay_match_from_lobby(const char *json, NetplayMatch *m, u8 *local_seats);
/*
 * Call once per loop on the game thread, outside the game tick (ui_update does): once the lobby reaches
 * PBNET_LOBBY_STARTED, starts its match (netplay_match_start), once per started lobby. A started lobby whose
 * room is not a playable match is left (pbnet_lobby_leave), so the other peers do not wait on this one.
 */
void netplay_lobby_poll(void);

/* Seed accessors for the hash and session start (frand_seed is static in src/game/frand.c). */
u32 frandSeedGet(void);
void frandSeedSet(u32 seed);

#ifdef __cplusplus
}
#endif
