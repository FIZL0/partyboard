/*
 * Netplay match start: every peer leaves whatever menu it is in and enters the Room's board in the same
 * state, with the lockstep running from that board's first frame. Mirrors the party start of mentDll
 * (src/REL/mentDll/main.c, BoardPartyConfigSet + GWPlayerCfg + BoardSaveInit, then fn_1_C174's overlay call).
 *
 * The earliest state common to all peers is built in two steps (research/journal/2026-10-27-n8-match-start):
 *  1. netplay_match_tick, at the loop top before frame 0's hash and pads: the hashed game state (GWSystem,
 *     GWPlayer, GWPlayerCfg, the pads, the seeds) and the state the board reads but nobody hashes (GWGameStat,
 *     the wipe, pad.c's statics) are overwritten with values that depend on the match only. A process at the
 *     highest priority is queued for the overlay switch.
 *  2. That process runs first in frame 0: it kills the menu overlay before any of its processes runs again
 *     and calls the board, as mentDll does. Two frames later, when the board overlay starts
 *     (netplay_ovl_start), every heap's free memory is zeroed, so reads of memory the game did not write see
 *     the same bytes on every peer, and the heaps' layout is logged.
 *
 * The match is over when the game leaves the board for a menu (board -> mstory3dll's results -> modeseldll, or the
 * board straight to modeseldll): netplay_ovl_start gives the player's GWGameStat back before the menu sets itself up,
 * and netplay_frame_end ends the session at the end of that frame, the same frame on every peer (N25).
 *
 * A continue (netplay_match_continue, N9) is the same with the board state of an online save's checkpoint (the
 * SLSaveBoard blob, loaded with SLLoadBoard as mentDll's continue does) instead of a new party.
 */

#if defined(__linux__) && !defined(_GNU_SOURCE)
#define _GNU_SOURCE /* dladdr */
#endif

#include "dolphin.h"
#include "game/armem.h"
#include "game/audio.h"
#include "game/board/main.h"
#include "game/chrman.h"
#include "game/data.h"
#include "game/flag.h"
#include "game/gamework_data.h"
#include "game/hu3d.h"
#include "game/memory.h"
#include "game/minigame_seq.h"
#include "game/object.h"
#include "game/objsub.h"
#include "game/pad.h"
#include "game/process.h"
#include "game/sprite.h"
#include "game/wipe.h"
#include "port/netplay.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#include <windows.h>
#elif defined(__linux__) || defined(__APPLE__)
#include <dlfcn.h>
#elif defined(__EMSCRIPTEN__)
#include "port/web_rel.h"
#endif

SHARED_SYM extern GameStat GWGameStatDefault;
SHARED_SYM extern s32 saveExecF;
void BoardMGSetupPlayClear(void);
void BoardRollNetplayReset(void);
void BoardBasicSpaceNetplayReset(void);
void BoardBattleNetplayReset(void);
void BoardBlockNetplayReset(void);
void BoardBooNetplayReset(void);
void BoardBooHouseNetplayReset(void);
void BoardBowserNetplayReset(void);
void BoardCharWheelNetplayReset(void);
void BoardComNetplayReset(void);
void BoardFortuneNetplayReset(void);
void BoardItemNetplayReset(void);
void BoardLast5NetplayReset(void);
void BoardLotteryNetplayReset(void);
void BoardMainNetplayReset(void);
void BoardMGSetupNetplayReset(void);
void BoardModelNetplayReset(void);
void BoardMushroomNetplayReset(void);
void BoardPauseNetplayReset(void);
void BoardPlayerNetplayReset(void);
void BoardShopNetplayReset(void);
void BoardSpaceNetplayReset(void);
void BoardStarNetplayReset(void);
void BoardStartNetplayReset(void);
void BoardUiNetplayReset(void);
void BoardViewNetplayReset(void);
void BoardWarpNetplayReset(void);
void BoardWinNetplayReset(void);
s32 BoardItemNetplayKill(void);
BOOL MGSeqNetplayPauseKill(void);
void CharEffectNetplayReset(void);
s32 Hu3DModelNetplayReset(void);
s32 HuDataNetplayCloseAll(void);
SHARED_SYM extern u32 GlobalCounter;
SHARED_SYM extern s32 SystemInitF;
void HuSysVWaitSet(s16 vcount);
void HuWindowInit(void);
BOOL HuWinProcActive(void);
void HuWinAllKill(void);

/* Board 5 is Bowser's Gnarly Party (w06): mentDll wipes to black for it, to white for the others. */
#define BOARD_BOWSER 5

static struct {
    BOOL running; /* netplay_match_start .. netplay_end */
    BOOL pending; /* started; netplay_match_tick has not applied it yet */
    BOOL scrub;   /* applied; the board overlay has not started yet */
    BOOL resume;  /* a continue: the board state is `blob` */
    BOOL board;   /* the board overlay started */
    BOOL over;    /* the party's results or a menu started after the board: the match is over */
    BOOL results; /* over at the results' start: the canned GWGameStat stays until a menu starts */
    BOOL minigame; /* N52: a continue from a minigame's start: instDll, not the board, comes first */
    u8 groups[4];  /* N52: and mg_setup's sides (GWPlayerCfg[].group) */
    BOOL switched; /* N67: MatchStartProc called the board (or instDll), in frame `switch_frame` */
    u32 switch_frame;
    NetplayMatch m;
    u8 blob[NETPLAY_BLOB_MAX];
    u32 blob_size;
} match;

static GameStat own_stat;
static BOOL own_stat_saved;
/* N64: the match ended at its results, which run on outside the session; a menu overlay has not started yet. */
static BOOL results_running;

const char *netplay_match_invalid(const NetplayMatch *m)
{
    u32 chars = 0;
    s32 team0 = 0;
    s32 i;

    if (m->board >= NETPLAY_BOARDS) {
        return "no such board";
    }
    if (m->turns < 10 || m->turns > 50 || m->turns % 5 != 0) {
        return "turns must be 10 to 50 in steps of 5";
    }
    for (i = 0; i < 4; i++) {
        const NetplaySeat *s = &m->seats[i];
        if (s->character >= 8 || (chars & (1u << s->character))) {
            return "every seat needs its own character (0..7)";
        }
        chars |= 1u << s->character;
        if (s->difficulty >= 4) {
            return "CPU difficulty must be 0..3";
        }
        if (s->handicap > 9) {
            return "handicap must be 0..9";
        }
        if (s->team > 1) {
            return "team must be 0 or 1";
        }
        team0 += s->team == 0;
    }
    if (m->teams && team0 != 2) {
        return "a team battle needs two seats on each team";
    }
    return NULL;
}

BOOL netplay_match_start(const NetplayMatch *m)
{
    const char *why = netplay_match_invalid(m);
    if (why != NULL) {
        OSReport("netplay: cannot start the match: %s\n", why);
        return FALSE;
    }
    if (match.pending) {
        OSReport("netplay: cannot start the match: one is already starting\n");
        return FALSE;
    }
    match.m = *m;
    /* From the last match's results: the player's own GWGameStat is still in own_stat (game_stat_canned keeps it). */
    results_running = FALSE;
    match.running = TRUE;
    match.pending = TRUE;
    match.scrub = FALSE;
    match.resume = FALSE;
    match.minigame = FALSE;
    match.switched = FALSE;
    match.blob_size = 0;
    netplay_checkpoint_set(NULL, 0);
    /* A --script run keeps its session (and frame numbers), but the menus it went through used the seeds: they are
     * set again from the session seed and this frame (N26: two runs by different menus differed in rand8/frand). */
    if (!netplay_debug_scripted()) {
        netplay_begin((u32)m->seed ^ (u32)(m->seed >> 32));
    } else {
        netplay_reseed();
    }
    OSReport("netplay: match start: board %d, %d turns, bonus %d, teams %d\n", m->board, m->turns, m->bonus, m->teams);
    return TRUE;
}

BOOL netplay_match_continue(const NetplayMatch *m, const void *blob, u32 size)
{
    NetplayMatch c;
    s32 board;
    u8 groups[4] = { 0 };
    u32 mg = netplay_minigame_blob(blob, size, groups);

    /* N52: a minigame's resync point is the board blob and a mark; the board state is the blob alone. */
    if (mg != 0) {
        size = mg;
    }
    if (blob == NULL || size != netplay_board_blob_size() || size > sizeof(match.blob)) {
        OSReport("netplay: cannot continue: a checkpoint of %u bytes, this build's are %u\n", (unsigned)size,
            (unsigned)netplay_board_blob_size());
        return FALSE;
    }
    if (match.pending) {
        OSReport("netplay: cannot continue: a match is already starting\n");
        return FALSE;
    }
    board = netplay_board_blob_board(blob, size);
    if (board < 0 || board >= NETPLAY_BOARDS) {
        OSReport("netplay: cannot continue: the checkpoint is on board %d\n", (int)board);
        return FALSE;
    }
    /* The board is the save's (the overlay to call and the wipe colour); the rest of the state comes with the blob. */
    c = *m;
    c.board = (u8)board;
    if (!netplay_match_start(&c)) {
        return FALSE;
    }
    memcpy(match.blob, blob, size);
    match.blob_size = size;
    match.resume = TRUE;
    netplay_checkpoint_set(match.blob, size);
    match.minigame = mg != 0;
    memcpy(match.groups, groups, sizeof(match.groups));
    /* N46: a resync point at a turn start resumes that turn (netplay_turn_resuming). */
    if (!match.minigame) {
        netplay_turn_resume_arm();
    }
    OSReport("netplay: the match continues from a checkpoint%s\n", match.minigame ? " at a minigame's start" : "");
    return TRUE;
}

BOOL netplay_match_running(void)
{
    return match.running;
}

/* GWGameStat is the player's save file: unlocks, records, settings. The board reads parts of it (minigame
 * lists, very hard CPUs, language), so a match plays on the same canned one everywhere; the player's own
 * comes back at netplay_end (an online match leaves no trace in the local save). */
static void game_stat_canned(void)
{
    if (!own_stat_saved) {
        own_stat = GWGameStat;
        own_stat_saved = TRUE;
    }
    GWGameStat = GWGameStatDefault;
    GWGameStat.language = 1;
    GWGameStat.sound_mode = 1;
    GWGameStat.rumble = 1;
    GWGameStat.create_time = 0;
    GWGameStat.mg_avail[0] = GWGameStat.mg_avail[1] = 0xFFFFFFFF;
    GWGameStat.mg_custom[0] = GWGameStat.mg_custom[1] = 0;
    GWGameStat.story_continue = 0;
    GWGameStat.party_continue = 0;
    GWGameStat.open_w06 = 1;
    GWGameStat.veryHardUnlock = 1;
    GWGameStat.customPackEnable = 0;
    GWGameStat.musicAllF = 0;
}

/* The player's own GWGameStat back (the canned one was only for the match). */
static void game_stat_own(void)
{
    if (own_stat_saved) {
        GWGameStat = own_stat;
        own_stat_saved = FALSE;
    }
}

/* A wipe the menu left running would make the board's own WipeCreate a no-op on one peer only. */
static void wipe_reset(void)
{
    s32 i;
    if (wipeData.copy_data != NULL && !wipeData.keep_copy) {
        HuMemDirectFree(wipeData.copy_data);
    }
    wipeData.copy_data = NULL;
    wipeData.keep_copy = 0;
    for (i = 0; i < 8; i++) {
        if (wipeData.unk10[i] != NULL) {
            HuMemDirectFree(wipeData.unk10[i]);
            wipeData.unk10[i] = NULL;
        }
    }
    wipeData.unk0C = 0;
    wipeData.time = 0;
    wipeData.type = WIPE_TYPE_NORMAL;
    wipeData.mode = WIPE_MODE_BLANK;
    wipeData.stat = 0;
    if (match.m.board == BOARD_BOWSER) {
        WipeColorSet(0, 0, 0);
    } else {
        WipeColorSet(0xFF, 0xFF, 0xFF);
    }
}

/* What modeseldll and mentDll set up for a party, from the match only. */
static void party_config(const NetplayMatch *m)
{
    s32 i;

    memset(&GWSystem, 0, sizeof(GWSystem));
    memset(GWPlayer, 0, sizeof(GWPlayer));
    memset(GWPlayerCfg, 0, sizeof(GWPlayerCfg));
    /* modeseldll: the party options. Save mode 1 is "don't save": resultDll then never writes the local
     * memory card (online saves are N9's). */
    GWMGExplainSet(netplay_debug_mg_explain_off() ? 0 : 1);
    GWMGShowComSet(1);
    GWMGListSet(0);
    GWMessSpeedSet(1);
    GWSaveModeSet(1);
    /* mentDll fn_1_B654 (src/REL/mentDll/main.c:1880-1934). */
    BoardPartyConfigSet(m->teams ? 1 : 0, m->bonus ? 1 : 0, 0, m->turns, m->seats[0].handicap, m->seats[1].handicap,
        m->seats[2].handicap, m->seats[3].handicap);
    for (i = 0; i < 4; i++) {
        GWPlayerCfg[i].character = m->seats[i].character;
        /* Seat i plays with lockstep pad i (pbnet's out[i]). */
        GWPlayerCfg[i].pad_idx = i;
        GWPlayerCfg[i].diff = m->seats[i].difficulty;
        GWPlayerCfg[i].group = m->teams ? m->seats[i].team : 0;
        GWPlayerCfg[i].iscom = m->seats[i].cpu ? 1 : 0;
    }
    BoardSaveInit(m->board);
    /* N65: modeseldll's, so instDll picks the board's host (Minigame mode's sets 1). */
    mgBoardHostEnableF = 0;
}

/* A continue: mentDll's party continue (main.c:798-805, SLLoadBoard) from the checkpoint, then who plays each seat now. */
static void resume_config(const NetplayMatch *m)
{
    s32 i;

    memset(&GWSystem, 0, sizeof(GWSystem));
    memset(GWPlayer, 0, sizeof(GWPlayer));
    memset(GWPlayerCfg, 0, sizeof(GWPlayerCfg));
    netplay_board_blob_set(match.blob, match.blob_size);
    /* The checkpoint was taken online, so it is "don't save" already; set anyway (the local card stays untouched). */
    GWSaveModeSet(1);
    for (i = 0; i < 4; i++) {
        /* Seat i still plays with lockstep pad i; a seat nobody took back is the CPU's now (and the reverse). */
        GWPlayerCfg[i].pad_idx = i;
        GWPlayer[i].port = i;
        GWPlayerCfg[i].iscom = m->seats[i].cpu ? 1 : 0;
        GWPlayer[i].com = m->seats[i].cpu ? 1 : 0;
        if (match.minigame) {
            GWPlayerCfg[i].group = match.groups[i];
        }
        if (m->seats[i].cpu) {
            GWPlayerCfg[i].diff = m->seats[i].difficulty;
            GWPlayer[i].diff = m->seats[i].difficulty;
        }
    }
    /* The checkpoint has flag (1,0) set, so the board does not clear its "recently played minigames" lists
     * (BoardObjectSetup, board/main.c:102): dol statics, which differ between a peer that played the match up to here
     * and one that did not. Cleared here, as a continue after a power-off of the console has them. */
    BoardMGSetupPlayClear();
    /* mentDll's continue sets it to 1, and mstory3dll then writes the local card at the end of the party. */
    saveExecF = 0;
    /* N65: as in party_config. */
    mgBoardHostEnableF = 0;
}

/* The module (shared library) holding the code at `fn`, NULL if unknown. */
static const void *code_module(void (*fn)(void))
{
#if defined(_WIN32)
    HMODULE mod = NULL;
    if (!GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            (LPCSTR)(uintptr_t)fn, &mod)) {
        return NULL;
    }
    return mod;
#elif defined(__linux__) || defined(__APPLE__)
    Dl_info info;
    if (fn == NULL || dladdr((void *)(uintptr_t)fn, &info) == 0) {
        return NULL;
    }
    return info.dli_fbase;
#else
    (void)fn;
    return NULL;
#endif
}

/*
 * The module holding the function `fn` (a process's func or dtor), NULL if unknown. N99: the browser build links the
 * RELs statically and has no dladdr: code_module knew no process there, so the match start killed none, and bootDll's
 * BootExec went on under the board on each peer from wherever its title loop was (its wipes and windows a few frames
 * apart on two peers: the heaps' layout and then the board state split). web_rel_function_owner tells the RELs apart.
 * Return addresses (dead_code_block) are not function pointers there and stay with code_module.
 */
static const void *function_module(void (*fn)(void))
{
#if defined(__EMSCRIPTEN__)
    return web_rel_function_owner(fn);
#else
    return code_module(fn);
#endif
}

/* A process running (or destroyed by) code outside libdol: the menu overlay's own. */
static BOOL overlay_process(const Process *process, void *dol)
{
    const void *f = function_module(process->func);
    const void *d = function_module(process->dtor);
    return (f != NULL && f != dol) || (d != NULL && d != dol);
}

/*
 * A START pause (a minigame's, or the board's pause menu through omSysPauseCtrl) has paused every process, the 3D,
 * sprites and sounds (omSystemKeyCheck); its objects (omAllPause, the caller's objman only) go with the objman in
 * omOvlKill. Nothing of the next overlay lifts it: its omSystemKeyCheck object starts unpaused (N76: a peer paused
 * when a reload came kept Hu3DPauseF set on the new board, whose models then stood still, and MGSeqKillAll, which
 * returns while the 3D is paused, left the minigame's sequences running).
 */
static void match_pause_lift(void)
{
    if (MGSeqNetplayPauseKill()) {
        OSReport("netplay: match: ended the minigame's pause screen\n");
    }
    HuPrcAllPause(FALSE);
    HuPrcAllUPause(FALSE);
    Hu3DPauseSet(FALSE);
    HuSprPauseSet(FALSE);
    HuAudFXPauseAll(FALSE);
    HuAudSeqPauseAll(FALSE);
}

/* Frame 0, first process to run: mentDll fn_1_C174(3) without its wipe-out. */
static void MatchStartProc(void)
{
    s32 i;
    s32 killed;

    match_pause_lift();

    /* omOvlKill only kills omwatchproc's children (the objman). A menu's root processes (HuPrcCreate in its
     * ObjectSetup, e.g. bootDll's BootExec) end themselves after their own omOvlCallEx; called from here they
     * would run on in an unloaded DLL. Killed processes end later in this frame's HuPrcCall, before omwatchproc
     * (priority 0) unloads the DLL, so their destructors still find their code. */
    killed = HuPrcKillIf(overlay_process, (void *)function_module((void (*)(void))netplay_match_tick));
    OSReport("netplay: match start: killed %d menu processes\n", (int)killed);
    /* N62: a reload while the board runs. The board's processes (dol code, under boardObjMan) are only killed by
     * omOvlKill, after it has freed the 3D models, windows and sprites, and their destructors then ran on freed models
     * (DestroyShop with a player in the shop: SIGSEGV in Hu3DModelHookReset). They run here, while it all exists, as on
     * the board's own exit (BoardKill, then KillBoardFunc waits for them before omOvlCallEx). */
    if (omcurovl >= DLL_w01dll && omcurovl <= DLL_w06dll && boardObjMan != NULL) {
        OSReport("netplay: match start: ran %d board process destructors\n", (int)HuPrcChildDtorRun(boardObjMan));
    }
    /* N66: and an item's root process (ItemRotProc), which only its item ends. */
    killed = BoardItemNetplayKill();
    if (killed != 0) {
        OSReport("netplay: match start: killed %d item processes\n", (int)killed);
    }
    /* bootDll sets up the characters, windows and minigame messages once, after its first wipe-in (frame ~30, or
     * after the language choice on PAL). A match started before that (N41: --match 0@2 crashed in the board's
     * HuWinInit, no window data in ARAM) does it here, as bootDll's BootInitForSkippedSequence does. */
    if (!SystemInitF) {
        CharInit();
        /* N64: bootDll's title has run HuWinInit before it sets SystemInitF. HuWindowInit would forget that window
         * process without ending it, and the board's HuWinInit would start a second one: every window then ran twice
         * per frame on this peer only, so messages ended a frame or more apart (the party results' dust, the board
         * intro's turn-order dice). The windows are ended first, as on a peer that never opened one. */
        if (HuWinProcActive()) {
            HuWinAllKill();
        }
        HuWindowInit();
        MGSeqInit();
        SystemInitF = TRUE;
        OSReport("netplay: match start: system init done for bootDll\n");
    }
    CharModelKill(-1);
    MGSeqKillAll();
    for (i = 0; i < 4; i++) {
        CharMotionInit(GWPlayerCfg[i].character);
    }
    HuAudSeqAllFadeOut(1000);
    HuAudSStreamAllFadeOut(1000);
    /* The overlay history of a party started from the menus (boot -> modesel -> mentDll, which renames its
     * own entry to mstory3dll before calling the board), so the board returns alike on every peer. */
    omovlhisidx = 2;
    omOvlHisChg(2, DLL_bootdll, 1, 0);
    omOvlHisChg(1, DLL_modeseldll, 1, 0);
    omOvlHisChg(0, DLL_mstory3dll, 0, 0);
    if (match.minigame) {
        /* N52: and the board, which called instDll (KillBoardFunc) after mg_setup's white wipe-out. instDll renames its
         * own entry to resultdll, which returns to the board. */
        omovlhisidx++;
        omOvlHisChg(0, DLL_w01dll + match.m.board, 0, 0);
        WipeColorSet(0xFF, 0xFF, 0xFF);
        omOvlCallEx(DLL_instdll, 1, 0, 0);
    } else {
        omOvlCallEx(DLL_w01dll + match.m.board, 1, 0, 0);
    }
    match.switched = TRUE;
    match.switch_frame = netplay_frame();
    HuPrcEnd();
}

/*
 * N67: the overlay this frame's MatchStartProc called starts in the next frame on every peer, whatever the peer was
 * doing. A running overlay's omwatchproc waits on its children: the kill wakes it in this frame and its HuPrcSleep(0)
 * starts the board in the next. But a peer whose own overlay switch was under way when the match started had its
 * omwatchproc past that sleep (it started the board in this frame, a frame before the others: --match 0@994 and 0@1911
 * of mash-a), or asleep in the old switch's AUX change (it loaded the board in this frame under the old overlay's
 * setup, and the scrub waited for the board's next start after a minigame: 0@995). omwatchproc asks here after each
 * of those sleeps, and goes back to the top of its loop (a frame later) while this is TRUE.
 */
BOOL netplay_ovl_hold(void)
{
    return match.scrub && match.switched && netplay_frame() == match.switch_frame;
}

void netplay_match_tick(void)
{
    if (!match.pending) {
        return;
    }
    match.pending = FALSE;
    HuPadNetplayReset();
    game_stat_canned();
    if (match.resume) {
        resume_config(&match.m);
    } else {
        party_config(&match.m);
    }
    wipe_reset();
    /* HuAudDllSndGrpSet sleeps a frame when the AUX changes, which depends on the menu left (N26: from the title
     * screen the board started a frame later than from mode select). */
    HuAudNetplayAuxReset();
    GlobalCounter = 0;
    HuSysVWaitSet(1);
    /* The highest priority: it runs before any process of the menu overlay does in frame 0. */
    if (HuPrcCreate(MatchStartProc, 0xFFFF, 0x2000, 0) == NULL) {
        OSReport("netplay: match start: cannot create the start process\n");
        return;
    }
    match.scrub = TRUE;
    OSReport("netplay: match state set at frame %u, entering board %d\n", netplay_frame(), match.m.board);
}

typedef struct {
    u64 h;
    s32 heap;
    u32 used_blocks;
    size_t used_bytes;
    size_t scrubbed;
    BOOL scrub;
} HeapWalk;

static void fnv_word(u64 *h, u64 v)
{
    s32 i;
    for (i = 0; i < 8; i++) {
        *h ^= (u8)(v >> (i * 8));
        *h *= 0x100000001B3ULL;
    }
}

/*
 * A block's num as the same value in every run. Some nums are host pointers: hsfman.c tags a model's blocks
 * with (uintptr_t)hsf, an address in a game heap, which moves with the host's ASLR from run to run
 * (research/journal/2026-10-27-n15-determinism-run). A num inside a heap becomes (heap, offset in it).
 */
static u64 heap_num(uintptr_t num)
{
    s32 heap;
    for (heap = 0; heap < HEAP_MAX; heap++) {
        uintptr_t base = (uintptr_t)HuMemHeapPtrGet((HeapID)heap);
        if (base != 0 && num >= base && num - base < HuMemHeapSizeGet((HeapID)heap)) {
            return 0x8000000000000000ULL | ((u64)heap << 56) | (u64)(num - base);
        }
    }
    return (u64)num;
}

static void heap_block(void *user, uintptr_t offset, s32 size, BOOL used, uintptr_t num, void *data, size_t data_size)
{
    HeapWalk *w = user;
    if (used) {
        fnv_word(&w->h, (u64)w->heap);
        fnv_word(&w->h, (u64)offset);
        fnv_word(&w->h, (u64)(u32)size);
        fnv_word(&w->h, heap_num(num));
        w->used_blocks++;
        w->used_bytes += (size_t)size;
    } else if (w->scrub) {
        memset(data, 0, data_size);
        w->scrubbed += data_size;
    }
}

/* Hash of every heap's used blocks; zeroes the free blocks' data if `scrub`; logs each heap if `report`. */
static u64 heap_walk(BOOL scrub, BOOL report)
{
    u64 h = 0xCBF29CE484222325ULL;
    size_t scrubbed = 0;
    s32 heap;
    for (heap = 0; heap < HEAP_MAX; heap++) {
        void *ptr = HuMemHeapPtrGet((HeapID)heap);
        HeapWalk w;
        if (ptr == NULL) {
            continue;
        }
        memset(&w, 0, sizeof(w));
        w.h = 0xCBF29CE484222325ULL;
        w.heap = heap;
        w.scrub = scrub;
        HuMemHeapWalk(ptr, heap_block, &w);
        fnv_word(&h, w.h);
        scrubbed += w.scrubbed;
        if (report) {
            /* Per heap, so two peers' logs show which heap is laid out differently. */
            OSReport("netplay: heap %d: %u used blocks, %u bytes, layout %016llX\n", heap, w.used_blocks,
                (unsigned)w.used_bytes, (unsigned long long)w.h);
        }
    }
    if (scrub) {
        OSReport("netplay: zeroed %u free heap bytes\n", (unsigned)scrubbed);
    }
    return h;
}

/*
 * N66: the old board's statics that name its models, sprites, windows, objects and processes. The board frees those
 * itself, often a frame or more after its kill (an object's func that sees BoardIsKill or its kill flag, DestroyBoard's
 * kills), but MatchStartProc replaces it before that frame: they then name things omOvlKill has freed. A fresh peer has
 * their initial values, and so does every peer after this.
 */
static void board_statics_reset(void)
{
    BoardRollNetplayReset();
    BoardBasicSpaceNetplayReset();
    BoardBattleNetplayReset();
    BoardBlockNetplayReset();
    BoardBooNetplayReset();
    BoardBooHouseNetplayReset();
    BoardBowserNetplayReset();
    BoardCharWheelNetplayReset();
    BoardComNetplayReset();
    BoardFortuneNetplayReset();
    BoardItemNetplayReset();
    BoardLast5NetplayReset();
    BoardLotteryNetplayReset();
    BoardMainNetplayReset();
    BoardMGSetupNetplayReset();
    BoardModelNetplayReset();
    BoardMushroomNetplayReset();
    BoardPauseNetplayReset();
    BoardPlayerNetplayReset();
    BoardShopNetplayReset();
    BoardSpaceNetplayReset();
    BoardStarNetplayReset();
    BoardStartNetplayReset();
    BoardUiNetplayReset();
    BoardViewNetplayReset();
    BoardWarpNetplayReset();
    BoardWinNetplayReset();
}

/* A block whose allocating code is not loaded any more: the old overlay's own (its code is gone before the next one
 * loads). 0xCDCDCDCD and 0xA5A5A5A5 are memory.c's and process.c's marks, not code. */
static BOOL dead_code_block(uintptr_t retaddr, void *user)
{
    (void)user;
    return retaddr != 0 && retaddr != 0xCDCDCDCD && retaddr != 0xA5A5A5A5
        && code_module((void (*)(void))retaddr) == NULL;
}

/*
 * N57: what a peer that was mid-board or mid-minigame keeps over a reload and a fresh peer has not: chrman's particle
 * work (SYSTEM), the model slots' link tags (N74), the old board's directory reads (DVD: DestroyBoard closes them, but a
 * killed board never runs it), its data in ARAM (the board between minigames; read back into DVD) and the blocks of a
 * minigame left mid-play. Done here, where nothing of the old overlay runs any more (MatchStartProc runs before its
 * processes' teardown) and the new one is not loaded yet, so every peer allocates them again alike.
 */
static void leftovers_free(void)
{
    s32 links;
    s32 busy;
    s32 heap;

    CharEffectNetplayReset();
    links = Hu3DModelNetplayReset();
    if (links != 0) {
        OSReport("netplay: forgot %d model slots' link tags\n", (int)links);
    }
    busy = HuDataNetplayCloseAll();
    if (busy != 0) {
        OSReport("netplay: %d directory reads still running at the overlay start\n", (int)busy);
    }
    if (HuARDirCheck(DATADIR_BOARD)) {
        HuARDirFree(DATADIR_BOARD);
    }
    /* What the old overlay allocated and would have freed at its own end (N57: m427's screen copies, m440's objects,
     * m415's anims). Only where this platform can tell code modules apart. */
    if (code_module((void (*)(void))netplay_match_tick) != NULL) {
        for (heap = 0; heap < HEAP_MAX; heap++) {
            if (HuMemHeapPtrGet((HeapID)heap) != NULL) {
                s32 freed = HuMemMemoryFreeIf(HuMemHeapPtrGet((HeapID)heap), dead_code_block, NULL);
                if (freed != 0) {
                    OSReport("netplay: heap %d: freed %d blocks of an overlay that is gone\n", (int)heap, (int)freed);
                }
            }
        }
    }
}

u64 netplay_heap_hash(void)
{
    return heap_walk(FALSE, FALSE);
}

static BOOL menu_ovl(s32 ovl)
{
    return ovl == DLL_modeseldll || ovl == DLL_mentdll || ovl == DLL_bootdll;
}

void netplay_ovl_start(s32 ovl)
{
    s32 i;

    if (results_running) {
        /* N64: the results ran on this machine alone; the menu after them reads the player's own GWGameStat. */
        if (menu_ovl(ovl)) {
            results_running = FALSE;
            game_stat_own();
            OSReport("netplay: the results are over: overlay %d starts\n", ovl);
        }
        return;
    }
    if (match.board && !match.over && ovl == DLL_mstory3dll) {
        /*
         * N64: the party's results (mstory3dll) start: the board's outcome is final, and the match is over here, the
         * same frame on every peer. The results are not gameplay: two peers run them a few frames apart (N42: an
         * effect's motion started a frame later on the host), so the same lockstep pads did different things and the
         * guest left them while the host was still in them. Each peer plays them on its own pads after the session.
         */
        match.over = TRUE;
        match.results = TRUE;
        /* The results wait for every human seat's A on that seat's pad (result_seq.c fn_1_1442C, result.c): on this
         * machine they all play with its own controller now. */
        for (i = 0; i < 4; i++) {
            if (!GWPlayerCfg[i].iscom) {
                GWPlayerCfg[i].pad_idx = 0;
            }
        }
        OSReport("netplay: match over: the results (overlay %d) start at frame %u\n", ovl, netplay_frame());
        netplay_checkpoint_finish();
        return;
    }
    if (ovl >= DLL_m300dll && ovl <= DLL_m463dll) {
        netplay_point_reseed(NETPLAY_POINT_MINIGAME, ovl);
    } else if (ovl == DLL_resultdll) {
        netplay_point_reseed(NETPLAY_POINT_RESULTS, ovl);
    }
    if (match.board && !match.over && menu_ovl(ovl)) {
        /* The party is over (mstory3dll returns to modeseldll; the board itself does when flag (1,27) skips the
         * results). The menu reads GWGameStat as it sets up, so it gets the player's own back now. */
        match.over = TRUE;
        game_stat_own();
        OSReport("netplay: match over: overlay %d starts at frame %u\n", ovl, netplay_frame());
        netplay_checkpoint_finish();
        return;
    }
    if (match.board && !match.scrub && ovl == DLL_instdll) {
        netplay_minigame_point();
    }
    if (!match.scrub || ovl != (match.minigame ? DLL_instdll : DLL_w01dll + match.m.board)) {
        return;
    }
    match.scrub = FALSE;
    match.board = TRUE;
    /*
     * N38: a reload (rejoin, CPU stand-in, resync) can come while a board or minigame runs. The menu-process kill of
     * MatchStartProc leaves the board's own processes (dol code) running in frame 0, and their teardown writes
     * GWSystem/GWPlayer (flags, player_curr) on that peer only: a peer that came from a menu had none. The old overlay
     * is gone now, so the state is set again, the same on every peer.
     */
    game_stat_canned();
    if (match.resume) {
        resume_config(&match.m);
    } else {
        party_config(&match.m);
    }
    /* The old board's dice (N52: a resync from a turn point crashed at the next roll) and the rest (N66). */
    board_statics_reset();
    leftovers_free();
    netplay_reseed();
    OSReport("netplay: board overlay %d starts at frame %u, turn %d/%d, heaps %016llX\n", ovl, netplay_frame(),
        GWSystem.turn, GWSystem.max_turn, (unsigned long long)heap_walk(TRUE, TRUE));
}

BOOL netplay_match_over(void)
{
    return match.over;
}

BOOL netplay_match_results(void)
{
    return results_running;
}

void netplay_match_end(void)
{
    if (match.results) {
        results_running = TRUE;
    } else {
        game_stat_own();
    }
    memset(&match, 0, sizeof(match));
    netplay_checkpoint_set(NULL, 0);
}

/*
 * Leaving a match before its end (N34): the started lobby is gone (this peer left, the host saved and quit, the
 * connection closed). The session has ended (netplay_end); whatever overlay runs goes back to modeseldll, as the
 * party's own end does. The board leaves the way a reset does (omSysExitReq: BoardKill, DestroyBoard, then with flag
 * (1,27) KillBoardFunc returns past mstory3dll's results to modeseldll); any other overlay (a minigame, instDll, the
 * results) is switched to modeseldll as MatchStartProc switches a menu to the board.
 */
#define LEAVE_BOARD_FRAMES 600 /* the board's own exit that long without reaching modeseldll: switch it */

static struct {
    BOOL on;
    BOOL exit_req;   /* omSysExitReq set for this overlay */
    BOOL switching;  /* MatchLeaveProc created for this overlay */
    s32 ovl;         /* the overlay the steps above were for */
    u32 frames;      /* polls since the overlay started (or the leave did) */
    GameStat stat;   /* the player's own, back from netplay_end */
} leave;

/* The overlay's processes end, the pauses are lifted, the board's ARAM data goes, and modeseldll is next. */
static void MatchLeaveProc(void)
{
    s32 killed;

    match_pause_lift();
    killed = HuPrcKillIf(overlay_process, (void *)function_module((void (*)(void))netplay_match_tick));
    OSReport("netplay: match left: overlay %d, killed %d of its processes\n", (int)omcurovl, (int)killed);
    wipe_reset();
    HuAudFXAllStop();
    HuAudSeqAllFadeOut(500);
    HuAudSStreamAllFadeOut(500);
    /* What KillBoardFunc does when the party ends: the board between minigames keeps its data in ARAM. */
    if (HuARDirCheck(DATADIR_BOARD)) {
        HuARDirFree(DATADIR_BOARD);
    }
    _ClearFlag(FLAG_ID_MAKE(1, 0));
    omovlhisidx = 1;
    omOvlHisChg(1, DLL_bootdll, 1, 0);
    omOvlHisChg(0, DLL_modeseldll, 1, 0);
    omOvlGotoEx(DLL_modeseldll, 1, 1, 0);
    HuPrcEnd();
}

/* The minigame data the board (BoardMGSetup) or instDll read for the next minigame (HuDataDirReadAsync, not freed
 * with the overlay's memory) is closed by resultDll after the minigame. A read still in flight is left alone (closing
 * it panics). */
static void leave_data_close(void)
{
    const MgInfo *info = mgInfoTbl;
    s32 i;

    if (HuDataReadChk(DATADIR_INST) >= 0) {
        HuDataDirClose(DATADIR_INST);
    }
    for (i = 0; info->ovl != (u16)DLL_NONE && i < GWSystem.mg_next; i++) {
        info++;
    }
    if (info->ovl != (u16)DLL_NONE && HuDataReadChk(info->data_dir) >= 0) {
        HuDataDirClose(info->data_dir);
    }
}

void netplay_match_leave(void)
{
    memset(&leave, 0, sizeof(leave));
    leave.on = TRUE;
    leave.ovl = omcurovl;
    leave.stat = GWGameStat;
    OSReport("netplay: leaving the match from overlay %d\n", (int)omcurovl);
}

void netplay_match_leave_poll(void)
{
    BOOL board;

    if (!leave.on) {
        return;
    }
    if (omnextovl != DLL_NONE || omcurovl == DLL_NONE) {
        /* An overlay switch is under way. */
        return;
    }
    if (omcurovl == DLL_modeseldll) {
        /* The board's teardown (PauseExit) may have written the match's settings into GWGameStat. */
        GWGameStat = leave.stat;
        leave_data_close();
        leave.on = FALSE;
        OSReport("netplay: match left: back in modeseldll\n");
        return;
    }
    if (omcurovl != leave.ovl) {
        leave.ovl = omcurovl;
        leave.exit_req = FALSE;
        leave.switching = FALSE;
        leave.frames = 0;
    }
    leave.frames++;
    if (leave.switching) {
        return;
    }
    board = omcurovl >= DLL_w01dll && omcurovl <= DLL_w06dll;
    if (board && leave.frames < LEAVE_BOARD_FRAMES) {
        /* Until the board runs (1,16) and while it is ending itself (1,17: for a minigame next), it is left alone. */
        if (!leave.exit_req && _CheckFlag(FLAG_ID_MAKE(1, 16)) && !_CheckFlag(FLAG_ID_MAKE(1, 17))) {
            _SetFlag(FLAG_ID_MAKE(1, 27));
            omSysExitReq = TRUE;
            leave.exit_req = TRUE;
            OSReport("netplay: match left: the board exits\n");
        }
        return;
    }
    if (HuPrcCreate(MatchLeaveProc, 0xFFFF, 0x2000, 0) == NULL) {
        OSReport("netplay: match left: cannot create the leave process\n");
        return;
    }
    leave.switching = TRUE;
}

BOOL netplay_match_leaving(void)
{
    return leave.on;
}

/* A Room (net/src/lobby.rs) of the match for an online save when no lobby runs: --match debug runs. */
int netplay_match_room_json(char *buf, int cap)
{
    const NetplayMatch *m = &match.m;
    char seats[4][160];
    s32 i;

    if (!match.running) {
        if (buf != NULL && cap > 0) {
            buf[0] = '\0';
        }
        return 0;
    }
    for (i = 0; i < 4; i++) {
        const NetplaySeat *st = &m->seats[i];
        char name[16], key[16];
        /* Humans are this machine's players ("host"), keyed by seat; a CPU seat has no key. */
        snprintf(name, sizeof(name), st->cpu ? "CPU" : "Player %d", (int)i + 1);
        snprintf(key, sizeof(key), st->cpu ? "" : "local-%d", (int)i + 1);
        snprintf(seats[i], sizeof(seats[i]),
            "{\"peer\":\"%s\",\"name\":\"%s\",\"key\":\"%s\",\"character\":%u,\"cpu\":%s,\"difficulty\":%u,\"team\":%u}",
            st->cpu ? "cpu" : "host", name, key, st->character, st->cpu ? "true" : "false", st->difficulty, st->team);
    }
    return snprintf(buf, buf != NULL ? (size_t)cap : 0,
        "{\"id\":\"local-%016llX\",\"seats\":[%s,%s,%s,%s],\"board\":%u,\"turns\":%u,\"bonus\":%s,\"teams\":%s,"
        "\"handicaps\":[%u,%u,%u,%u],\"seed\":%llu,\"ready\":[],\"ver\":0,\"started\":true}",
        (unsigned long long)m->seed, seats[0], seats[1], seats[2], seats[3], m->board, m->turns, m->bonus ? "true" : "false",
        m->teams ? "true" : "false", m->seats[0].handicap, m->seats[1].handicap, m->seats[2].handicap, m->seats[3].handicap,
        (unsigned long long)m->seed);
}

BOOL netplay_match_parse_debug(const char *spec, NetplayMatch *m, u32 *at_frame)
{
    char *end;
    unsigned long board, at;
    s32 i;

    board = strtoul(spec, &end, 0);
    if (end == spec || *end != '@') {
        return FALSE;
    }
    spec = end + 1;
    at = strtoul(spec, &end, 0);
    if (end == spec || *end != '\0' || at == 0 || board >= NETPLAY_BOARDS) {
        return FALSE;
    }
    memset(m, 0, sizeof(*m));
    m->seed = 0x50424E54;
    m->board = (u8)board;
    m->turns = 10;
    m->bonus = 1;
    for (i = 0; i < 4; i++) {
        m->seats[i].character = (u8)i;
        m->seats[i].cpu = i != 0;
        m->seats[i].difficulty = 1;
        m->seats[i].team = (u8)(i / 2);
    }
    *at_frame = (u32)at;
    return TRUE;
}
