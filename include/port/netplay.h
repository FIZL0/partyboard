#pragma once

/*
 * C side of online play (src/port/netplay.c). See plan/NET.md.
 *
 * While a session is active the game must run identically on every peer, so:
 *  - OSGetTime/OSGetTick return a value derived from the netplay frame counter and the session seed
 *    (linker-wrapped, see cmake/netplay.cmake);
 *  - frand, rand8 and BoardRand are seeded from the session seed, and again at every gameplay point;
 *  - netplay_hash() hashes the gameplay state that has to match between peers.
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
/* The game's random seeds from the session seed and the current frame, as netplay_begin sets them. */
void netplay_reseed(void);
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
 * 64-bit FNV-1a hash of the gameplay state (N64): GWSystem, GWPlayer[4], GWPlayerCfg[4] and the HuPad state. Equal on
 * every peer as long as they play alike; effects, menus and the random seeds are not in it. The lockstep sends it to
 * pbnet only while a gameplay overlay runs (netplay_gameplay_ovl), so no desync is raised outside gameplay.
 */
u64 netplay_hash(void);
/* netplay_hash plus the frand, rand8 and BoardRand seeds (the pre-N64 hash): the --hash-log column full=. */
u64 netplay_full_hash(void);
/* A board (w01..w06) or minigame (m300..m463) overlay: what the desync check covers. */
BOOL netplay_gameplay_ovl(s32 ovl);
/*
 * Gameplay points (N64): every player's turn start (netplay_turn_checkpoint), a minigame's start and the results'
 * entry (netplay_ovl_start). frand, rand8 and BoardRand are set from the session seed, GWSystem.turn,
 * GWSystem.player_curr, the point and `ovl` (the overlay starting, or omcurovl), not the frame: the same on every peer
 * however many cosmetic rolls each made before, so those never reach later dice or minigames.
 */
#define NETPLAY_POINT_TURN 1
#define NETPLAY_POINT_MINIGAME 2
#define NETPLAY_POINT_RESULTS 3
void netplay_point_reseed(u32 point, s32 ovl);

/*
 * Debug: parse `--script <file>` and `--hash-log <file>` from the command line. With --script the
 * game starts a local session at boot and plays the scripted pads; with --hash-log every frame's
 * netplay_hash() is written as "<frame> <hash> <section>=<value>...". `--match <board>@<frame>` (with
 * --script) starts a match on that board after that frame (netplay_match_parse_debug); with `--continue <online
 * save .json>` that match continues the save instead (N9); `--leave-at <frame>` leaves it at that frame as if its
 * lobby had gone (N34, netplay_match_leave). Returns FALSE (and logs why) if a file or argument can't be used.
 */
BOOL netplay_debug_init(int argc, char *argv[]);
/* True if --script was given (the prelaunch UI is skipped so runs need no clicks). */
BOOL netplay_debug_scripted(void);
/* True if the --script has `mg-explain off`: a --match party then starts with the minigame explanations off. */
BOOL netplay_debug_mg_explain_off(void);
/*
 * m444's plunger loop (fn_1_9418), while netplay_active(): called every frame it waits for its human player to pull the
 * plunger (stick down) and press A, with the pad it reads. --script's `plunger-stick on` (N114) then pulls that pad's
 * stick and holds its A back until the pull is a full one: a launch from a short pull can leave the ball bouncing
 * between the plunger and the lane's wall for ever.
 */
void netplay_script_plunger_wait(s32 pad);
/* N104: the pad m444's plunger waits on (netplay_script_plunger_wait in this frame or the last), else -1. For page
 * scripts' fake gamepads (src/port/web/web_stick_probe.c), which play it as `plunger-stick on` does. */
s32 netplay_plunger_waiting(void);
/*
 * `--turbo` (with --script, N50): the run skips the 60 Hz frame limiter, like holding the TAB turbo key, so
 * scripted checks run as fast as the machine goes. True only for a --script run outside lockstep (a lockstep
 * session keeps the pace its peers share); without --script the flag is ignored (and logged).
 */
BOOL netplay_debug_turbo(void);
/* Before netplay_debug_init: argv has --turbo and --script, so the window opens without vsync (portmain). */
BOOL netplay_debug_turbo_arg(int argc, char *argv[]);

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
 * N67: objmain.c omWatchOverlayProc, while netplay_active(), after each sleep before an overlay starts: TRUE in the
 * frame the match start switched to the board, which then starts in the next frame (the same frame on every peer,
 * whatever overlay or overlay switch each came from).
 */
BOOL netplay_ovl_hold(void);
/*
 * True once the match is over: netplay_ovl_start saw the party's results (mstory3dll, N64) or a menu overlay
 * (modeseldll, mentDll, bootDll: the board's own exit) start after the board. A menu gets the player's own GWGameStat
 * back before it sets up; the results keep the canned one until the menu after them (netplay_match_results). Cleared by
 * netplay_match_end. netplay_frame_end then ends the session at the end of that frame (the same on every peer):
 * pbnet_lockstep_finish, netplay_end, pbnet_lobby_back; a --script run only ends the match and keeps its session.
 */
BOOL netplay_match_over(void);
/* netplay_end: gives the player's own GWGameStat back (after the results: once a menu starts). */
void netplay_match_end(void);
/*
 * N64: TRUE from a match's end at its results until a menu overlay starts (or a match does). The results then run on
 * this machine's own pads, outside the session; objmain.c still calls netplay_ovl_start, which gives the player's own
 * GWGameStat back when the menu starts.
 */
BOOL netplay_match_results(void);
/*
 * N34: leaves the match before its end (call after netplay_end, or netplay_match_end for a --script run): from the
 * next netplay_match_leave_poll on, whatever overlay runs goes back to modeseldll (the board through its own exit,
 * BoardKill; any other overlay switched at once). netplay_match_leave_poll: once per frame, outside the game tick
 * (netplay_lobby_poll). netplay_match_leaving: TRUE until modeseldll runs.
 */
void netplay_match_leave(void);
void netplay_match_leave_poll(void);
BOOL netplay_match_leaving(void);
/* TRUE once after an online match ended and its lobby went back to open (or closed meanwhile): show the lobby. Not
 * while the match's results still run here (netplay_match_results). */
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
/*
 * board/main.c MainFunc, at the start of each player's turn, while netplay_turn_hooks() (N46, N47): during a match
 * every peer writes the online save (netplay_checkpoint: <user dir>/online/<match id>.json), and during a lockstep
 * match the same blob becomes this machine's newest desync resync point (pbnet_resync_point). Outside a match, a
 * local party writes its autosave (netplay_party_autosave). src/port/netplay_resync.c.
 */
void netplay_turn_checkpoint(void);
/* The guard of the board's turn hooks: netplay_active(), or a party (GWSystem.party) that autosaves offline (N47). */
BOOL netplay_turn_hooks(void);
/*
 * N47, offline party autosave (src/port/netplay_autosave.c): <user dir>/local/party.json, the SLSaveBoard blob at
 * the start of every player's turn, written to a temporary file and renamed over the last one.
 *  - netplay_party_autosave: from netplay_turn_checkpoint, in a party outside a match (not the tutorial).
 *  - netplay_party_offer: mentDll, before the party menu asks whether to continue: a readable autosave goes into the
 *    card buffer's party board (what that Continue's SLLoadBoard reads) and turns the question on; the board then
 *    resumes the saved player's turn (netplay_turn_resume_arm).
 *  - netplay_party_finish: mstory3Dll, when a party's results end it: the autosave is deleted.
 */
void netplay_party_autosave(void);
void netplay_party_offer(void);
void netplay_party_finish(void);
/* <dir>/<name> written whole to a temporary file, synced and renamed over it (src/port/netplay_dir.cpp); 1 if done. */
int netplay_file_replace(const char *dir, const char *name, const void *data, u32 size);
/* The file's bytes into buf (cap bytes): its size, -1 if it can't be read or is larger than cap. */
int netplay_file_read(const char *path, void *buf, u32 cap);
/* Removes the file; 1 if it was there. */
int netplay_file_remove(const char *path);
/*
 * A turn's resync point resumes that turn (N46). The board resets every player's space and items and starts at player
 * 0 unless BoardStartCheck says it comes back mid-round (after a minigame, a Bowser or battle event). A continue arms
 * netplay_turn_resume_arm (netplay_match_continue); while the board is created from a blob of a turn start (the board
 * already started, no event flag), BoardStartCheck answers yes (netplay_turn_resuming), as for a return from an event
 * with nothing left to run. MainFunc calls netplay_turn_resumed after CreateBoard, which disarms it.
 */
void netplay_turn_resume_arm(void);
BOOL netplay_turn_resuming(void);
void netplay_turn_resumed(void);
/*
 * N52, a resync point at a minigame's start (src/port/netplay_resync.c). netplay_ovl_start, when instDll starts after
 * the board's end-of-round minigame setup (mg_setup: flag (1,2)) in a lockstep match: the SLSaveBoard blob, then
 * NETPLAY_MG_MARK and GWPlayerCfg[0..3].group (mg_setup's sides, which the blob does not have: SLLoadBoard sets them
 * from GWPlayer.team) becomes the newest resync point. netplay_match_continue sees the mark (netplay_minigame_blob:
 * the board blob's size, the groups in `groups`; 0 if it has no mark) and resumes into instDll instead of the board.
 */
#define NETPLAY_MG_MARK "PBMG"
#define NETPLAY_MG_MARK_SIZE 4
#define NETPLAY_MG_TRAILER_SIZE (NETPLAY_MG_MARK_SIZE + 4)
void netplay_minigame_point(void);
u32 netplay_minigame_blob(const u8 *blob, u32 size, u8 groups[4]);
/* The last checkpoint's blob of this match into out (cap bytes): its size, 0 if none yet. */
u32 netplay_checkpoint_last(u8 *out, u32 cap);
/* netplay_match.c: forget the last checkpoint (size 0: a match starts or ends) or take `blob` as it (a continue). */
void netplay_checkpoint_set(const u8 *blob, u32 size);
/*
 * netplay_ovl_start, when the match is over (N31): deletes the match's online save on this peer (pbnet_save_finish,
 * else the file this peer's last checkpoint wrote), so Continue never offers a finished match.
 */
void netplay_checkpoint_finish(void);
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
 * room is not a playable match is left (pbnet_lobby_leave), so the other peers do not wait on this one. When the
 * lobby leaves PBNET_LOBBY_STARTED while its match runs (N34), the lockstep stops, the session ends and the game goes
 * back to modeseldll (netplay_match_leave).
 */
void netplay_lobby_poll(void);

/* Seed accessors for the hash and session start (frand_seed is static in src/game/frand.c). */
u32 frandSeedGet(void);
void frandSeedSet(u32 seed);

#ifdef __cplusplus
}
#endif
