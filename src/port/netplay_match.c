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

#include "dolphin.h"
#include "game/audio.h"
#include "game/board/main.h"
#include "game/chrman.h"
#include "game/gamework_data.h"
#include "game/memory.h"
#include "game/minigame_seq.h"
#include "game/object.h"
#include "game/pad.h"
#include "game/process.h"
#include "game/wipe.h"
#include "port/netplay.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

SHARED_SYM extern GameStat GWGameStatDefault;
SHARED_SYM extern s32 saveExecF;
void BoardMGSetupPlayClear(void);
SHARED_SYM extern u32 GlobalCounter;
void HuSysVWaitSet(s16 vcount);

/* Board 5 is Bowser's Gnarly Party (w06): mentDll wipes to black for it, to white for the others. */
#define BOARD_BOWSER 5

static struct {
    BOOL running; /* netplay_match_start .. netplay_end */
    BOOL pending; /* started; netplay_match_tick has not applied it yet */
    BOOL scrub;   /* applied; the board overlay has not started yet */
    BOOL resume;  /* a continue: the board state is `blob` */
    BOOL board;   /* the board overlay started */
    BOOL over;    /* a menu overlay started after the board: the match is over */
    NetplayMatch m;
    u8 blob[NETPLAY_BLOB_MAX];
    u32 blob_size;
} match;

static GameStat own_stat;
static BOOL own_stat_saved;

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
    match.running = TRUE;
    match.pending = TRUE;
    match.scrub = FALSE;
    match.resume = FALSE;
    match.blob_size = 0;
    netplay_checkpoint_set(NULL, 0);
    /* A --script run keeps its session (and frame numbers); its seed already makes two runs alike. */
    if (!netplay_debug_scripted()) {
        netplay_begin((u32)m->seed ^ (u32)(m->seed >> 32));
    }
    OSReport("netplay: match start: board %d, %d turns, bonus %d, teams %d\n", m->board, m->turns, m->bonus, m->teams);
    return TRUE;
}

BOOL netplay_match_continue(const NetplayMatch *m, const void *blob, u32 size)
{
    NetplayMatch c;
    s32 board;

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
    OSReport("netplay: the match continues from a checkpoint\n");
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
    GWMGExplainSet(1);
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
}

/* Frame 0, first process to run: mentDll fn_1_C174(3) without its wipe-out. */
static void MatchStartProc(void)
{
    s32 i;

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
    omOvlCallEx(DLL_w01dll + match.m.board, 1, 0, 0);
    HuPrcEnd();
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

static void heap_block(void *user, uintptr_t offset, s32 size, BOOL used, uintptr_t num, void *data, size_t data_size)
{
    HeapWalk *w = user;
    if (used) {
        fnv_word(&w->h, (u64)w->heap);
        fnv_word(&w->h, (u64)offset);
        fnv_word(&w->h, (u64)(u32)size);
        fnv_word(&w->h, (u64)num);
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

u64 netplay_heap_hash(void)
{
    return heap_walk(FALSE, FALSE);
}

void netplay_ovl_start(s32 ovl)
{
    if (match.board && !match.over && (ovl == DLL_modeseldll || ovl == DLL_mentdll || ovl == DLL_bootdll)) {
        /* The party is over (mstory3dll returns to modeseldll; the board itself does when flag (1,27) skips the
         * results). The menu reads GWGameStat as it sets up, so it gets the player's own back now. */
        match.over = TRUE;
        game_stat_own();
        OSReport("netplay: match over: overlay %d starts at frame %u\n", ovl, netplay_frame());
        return;
    }
    if (!match.scrub || ovl != DLL_w01dll + match.m.board) {
        return;
    }
    match.scrub = FALSE;
    match.board = TRUE;
    OSReport("netplay: board overlay %d starts at frame %u, heaps %016llX\n", ovl, netplay_frame(),
        (unsigned long long)heap_walk(TRUE, TRUE));
}

BOOL netplay_match_over(void)
{
    return match.over;
}

void netplay_match_end(void)
{
    game_stat_own();
    memset(&match, 0, sizeof(match));
    netplay_checkpoint_set(NULL, 0);
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
