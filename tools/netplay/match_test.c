/* The match start (src/port/netplay_match.c) against stub game functions and the real heap code
 * (src/game/memory.c); the continue and the checkpoint (N9, src/port/netplay_save.c) against a fake SLSaveBoard/
 * SLLoadBoard and a fake pbnet_save_write. Run with tools/netplay/match_test.sh. */
#include "dolphin.h"
#include "game/gamework_data.h"
#include "game/armem.h"
#include "game/data.h"
#include "game/flag.h"
#include "game/memory.h"
#include "game/object.h"
#include "game/objsub.h"
#include "game/process.h"
#include "game/saveload.h"
#include "game/wipe.h"
#include "port/netplay.h"
#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

GameStat GWGameStatDefault, GWGameStat; SystemState GWSystem; PlayerState GWPlayer[4]; PlayerConfig GWPlayerCfg[4];
u32 GlobalCounter = 1234; WipeState wipeData; s32 omovlhisidx = 7;
void OSReport(const char *fmt, ...) { va_list a; va_start(a, fmt); vprintf(fmt, a); va_end(a); }

static int begun, scripted; static u32 begun_seed;
void netplay_begin(u32 seed) { begun++; begun_seed = seed; }
u32 netplay_frame(void) { return 0; }
BOOL netplay_debug_scripted(void) { return scripted; }
static int reseeded;
void netplay_reseed(void) { reseeded++; }
void netplay_turn_resume_arm(void) {}

static s32 party_args[8], party_calls, save_board = -1, vwait, pad_resets, motions[4], nmotions, ended;
static u8 wipe_rgb[3];
void BoardPartyConfigSet(s32 a, s32 b, s32 c, s32 d, s32 e, s32 f, s32 g, s32 h)
{
    s32 v[8] = { a, b, c, d, e, f, g, h };
    memcpy(party_args, v, sizeof(v));
    party_calls++;
    GWSystem.party = 1;
    GWSystem.team = a;
    GWSystem.max_turn = d;
}
void BoardSaveInit(s32 board) { save_board = board; GWSystem.board = board; }
void HuSysVWaitSet(s16 v) { vwait = v; }
void HuPadNetplayReset(void) { pad_resets++; }
void WipeColorSet(u8 r, u8 g, u8 b) { wipe_rgb[0] = r; wipe_rgb[1] = g; wipe_rgb[2] = b; }
void CharModelKill(s16 c) { (void)c; }
void MGSeqKillAll(void) {}
/* bootDll's one-time init (N41): a match started before it does it. */
s32 SystemInitF; static s32 sys_inits;
void CharInit(void) { sys_inits++; }
void HuWindowInit(void) { sys_inits += 10; }
void MGSeqInit(void) { sys_inits += 100; }
void CharMotionInit(s16 c) { motions[nmotions++ % 4] = c; }
void HuAudSeqAllFadeOut(s32 s) { (void)s; }
void HuAudSStreamAllFadeOut(s32 s) { (void)s; }
static s32 aux_resets;
void HuAudNetplayAuxReset(void) { aux_resets++; }
static struct { OMOVL ovl; s32 event; } his[16];
void omOvlHisChg(s32 level, OMOVL o, s32 e, s32 st) { (void)st; his[omovlhisidx - level].ovl = o; his[omovlhisidx - level].event = e; }
static OMOVL called = DLL_NONE;
void omOvlCallEx(OMOVL o, s16 a, s32 e, s32 st) { (void)a; (void)e; (void)st; called = o; his[++omovlhisidx].ovl = o; }
void HuPrcEnd(void) { ended++; }
/* A process of this program's code (libdol's stand-in) is kept, one of libc's (another module: an overlay's) killed. */
static void dol_proc(void) {}
static s32 overlay_kills;
s32 HuPrcKillIf(BOOL (*pred)(const Process *, void *), void *user)
{
    Process dol = { 0 }, ovl = { 0 }, ovl_dtor = { 0 };
    dol.func = dol_proc;
    ovl.func = (void (*)(void))abort;
    ovl_dtor.func = dol_proc;
    ovl_dtor.dtor = (void (*)(void))abort;
    overlay_kills = !pred(&dol, user) + 2 * pred(&ovl, user) + 4 * pred(&ovl_dtor, user);
    return 2;
}
static void (*proc)(void); static u16 proc_prio;
Process *HuPrcCreate(void (*f)(void), u16 prio, u32 stack, s32 extra)
{
    static Process p;
    (void)stack; (void)extra;
    proc = f;
    proc_prio = prio;
    return &p;
}

/* N34: the leave (netplay_match_leave): the overlay switch, the board's exit request, its flags, the pauses lifted and
 * the minigame data closed. */
OMOVL omcurovl = DLL_NONE, omnextovl = DLL_NONE; s16 omSysExitReq;
static u8 flags[3][64];
s32 _CheckFlag(u32 f) { return flags[f >> 16][f & 0x3F]; }
void _SetFlag(u32 f) { flags[f >> 16][f & 0x3F] = 1; }
void _ClearFlag(u32 f) { flags[f >> 16][f & 0x3F] = 0; }
static s32 unpaused, fx_stops, ar_frees, ar_board;
void HuPrcAllPause(s32 f) { unpaused += f == 0; }
void HuPrcAllUPause(s32 f) { unpaused += (f == 0) * 10; }
void Hu3DPauseSet(BOOL f) { unpaused += (f == 0) * 100; }
void HuSprPauseSet(BOOL f) { unpaused += (f == 0) * 1000; }
void HuAudFXPauseAll(BOOL f) { unpaused += (f == 0) * 10000; }
void HuAudSeqPauseAll(BOOL f) { unpaused += (f == 0) * 100000; }
void HuAudFXAllStop(void) { fx_stops++; }
AMEM_PTR HuARDirCheck(u32 dir) { return dir == DATADIR_BOARD ? (AMEM_PTR)ar_board : 0; }
void HuARDirFree(u32 dir) { ar_frees += dir == DATADIR_BOARD; }
static struct { OMOVL ovl; s32 event; } gone_to = { DLL_NONE, 0 };
void omOvlGotoEx(OMOVL o, s16 a, s32 e, s32 st) { (void)a; (void)st; gone_to.ovl = o; gone_to.event = e; }
MgInfo mgInfoTbl[] = { { .ovl = DLL_m401dll, .data_dir = DATADIR_M401 }, { .ovl = DLL_m402dll, .data_dir = DATADIR_M402 },
    { .ovl = (u16)DLL_NONE } };
static u32 open_dirs[4], closed_dirs[4]; static s32 nclosed;
s32 HuDataReadChk(s32 dir)
{
    s32 i;
    for (i = 0; i < 4; i++) {
        if (open_dirs[i] == (u32)dir) {
            return i;
        }
    }
    return -1;
}
void HuDataDirClose(s32 dir) { closed_dirs[nclosed++ % 4] = (u32)dir; }

/* N9: the card buffer and SLSaveBoard/SLLoadBoard as saveload.c has them, without the byte swap. */
SaveBufAll saveBuf; s32 saveExecF = 1; s32 rnd_seed = 0x1234; u32 boardRandSeed = 0x5678;
u32 frandSeedGet(void) { return 0x9ABC; }
u32 netplay_session_seed(void) { return 0xDEF0; }
const char *netplay_user_dir(void) { return "/tmp/user"; }
static s32 play_clears;
void BoardMGSetupPlayClear(void) { play_clears++; }
/* N52: the dice of an old board are forgotten at the board's (or instDll's) start; instDll's start takes a point. */
static s32 roll_resets, mg_points;
void BoardRollNetplayReset(void) { roll_resets++; }
void netplay_minigame_point(void) { mg_points++; }
/* N57: the leftovers of an old board (particle work, directory reads) go at the same start. */
static s32 effect_resets, dir_close_alls;
void CharEffectNetplayReset(void) { effect_resets++; }
s32 HuDataNetplayCloseAll(void) { dir_close_alls++; return 0; }
#define SAVE_PLAYER(i) &saveBuf.buf[(i) * sizeof(PlayerState) + offsetof(SaveBufData, player)]
void SLSaveBoard(void)
{
    s32 i;
    memcpy(&saveBuf.data.system, &GWSystem, sizeof(SystemState));
    for (i = 0; i < 4; i++) {
        memcpy(SAVE_PLAYER(i), &GWPlayer[i], sizeof(PlayerState));
    }
}
void SLLoadBoard(void)
{
    s32 i;
    memcpy(&GWSystem, &saveBuf.data.system, sizeof(SystemState));
    for (i = 0; i < 4; i++) {
        memcpy(&GWPlayer[i], SAVE_PLAYER(i), sizeof(PlayerState));
        GWPlayerCfg[i].character = GWPlayer[i].character;
        GWPlayerCfg[i].pad_idx = GWPlayer[i].port;
        GWPlayerCfg[i].diff = GWPlayer[i].diff;
        GWPlayerCfg[i].iscom = GWPlayer[i].com;
        GWPlayerCfg[i].group = GWPlayer[i].team;
    }
}
static struct { s32 calls, ok; char dir[64], room[2048]; u8 blob[NETPLAY_BLOB_MAX]; int len; u32 frame, seeds[4]; } saved;
int pbnet_save_write(const char *dir, const char *room, const uint8_t *blob, int len, uint32_t frame, const uint32_t *seeds,
    char *path, int cap)
{
    saved.calls++;
    snprintf(saved.dir, sizeof(saved.dir), "%s", dir);
    snprintf(saved.room, sizeof(saved.room), "%s", room != NULL ? room : "");
    memcpy(saved.blob, blob, (size_t)len);
    saved.len = len;
    saved.frame = frame;
    memcpy(saved.seeds, seeds, sizeof(saved.seeds));
    snprintf(path, (size_t)cap, "%s/online/x.json", dir);
    return saved.ok;
}

/* N31: the match end deletes its save, by the match's room, else by the path its last checkpoint wrote. */
static struct { s32 calls, ok; char dir[64], room[2048]; } finished;
static struct { s32 calls, ok; char path[256]; } deleted;
int pbnet_save_finish(const char *dir, const char *room)
{
    finished.calls++;
    snprintf(finished.dir, sizeof(finished.dir), "%s", dir);
    snprintf(finished.room, sizeof(finished.room), "%s", room != NULL ? room : "");
    return finished.ok;
}
int pbnet_save_delete(const char *path)
{
    deleted.calls++;
    snprintf(deleted.path, sizeof(deleted.path), "%s", path);
    return deleted.ok;
}

static u8 heap_mem[2][0x4000];
static void *heaps[HEAP_MAX];
void *HuMemHeapPtrGet(HeapID h) { return heaps[h]; }
size_t HuMemHeapSizeGet(HeapID h) { return heaps[h] != NULL ? sizeof(heap_mem[0]) : 0; }
static u8 heap_moved[sizeof(heap_mem[1])];
void HuMemDirectFree(void *p) { HuMemMemoryFree(p, 0); }

static NetplayMatch base(void)
{
    NetplayMatch m;
    s32 i;
    memset(&m, 0, sizeof(m));
    m.seed = 0x1111111122222222ULL;
    m.board = 2;
    m.turns = 25;
    m.bonus = 1;
    for (i = 0; i < 4; i++) {
        m.seats[i].character = (u8)(7 - i);
        m.seats[i].cpu = i >= 2;
        m.seats[i].difficulty = (u8)i;
        m.seats[i].team = (u8)(i % 2);
        m.seats[i].handicap = (u8)(i * 3);
    }
    return m;
}

int main(void)
{
    NetplayMatch m = base(), bad;
    u8 *used, *freed, *other, *gone, *kept;
    u64 h1, h2;
    u32 at;
    s32 i;

    /* Validation. */
    assert(netplay_match_invalid(&m) == NULL);
    bad = m; bad.board = 6; assert(netplay_match_invalid(&bad));
    bad = m; bad.turns = 12; assert(netplay_match_invalid(&bad));
    bad = m; bad.turns = 55; assert(netplay_match_invalid(&bad));
    bad = m; bad.seats[1].character = bad.seats[0].character; assert(netplay_match_invalid(&bad));
    bad = m; bad.seats[3].character = 8; assert(netplay_match_invalid(&bad));
    bad = m; bad.seats[0].difficulty = 4; assert(netplay_match_invalid(&bad));
    bad = m; bad.seats[0].handicap = 10; assert(netplay_match_invalid(&bad));
    bad = m; bad.teams = 1; bad.seats[1].team = 0; assert(netplay_match_invalid(&bad));
    bad = m; bad.teams = 1; assert(netplay_match_invalid(&bad) == NULL);
    bad = m; bad.board = 9; assert(!netplay_match_start(&bad));
    assert(!netplay_match_running());

    /* Two heaps with the same history; a free block full of old bytes, the used one keeps its data. */
    for (i = 0; i < 2; i++) {
        heaps[i] = HuMemHeapInit(heap_mem[i], sizeof(heap_mem[i]));
    }
    memset((u8 *)heaps[0] + 0x100, 0xAA, 0x3000);
    used = HuMemMemoryAllocNum(heaps[0], 100, 5, 0);
    freed = HuMemMemoryAllocNum(heaps[0], 200, MEMORY_DEFAULT_NUM, 0);
    other = HuMemMemoryAllocNum(heaps[1], 100, 5, 0);
    memset(used, 0x11, 100);
    memset(freed, 0xBB, 200);
    HuMemMemoryFree(freed, 0);
    h1 = netplay_heap_hash();
    (void)other;
    /* Same used blocks in heap 1 as in heap 0, but they are in different heaps: different hash. */
    heaps[1] = NULL;
    h2 = netplay_heap_hash();
    assert(h1 != h2);
    heaps[1] = heap_mem[1];
    assert(netplay_heap_hash() == h1);
    /* A block tagged with a pointer into its heap (hsfman.c's (uintptr_t)hsf) hashes alike wherever the heap is. */
    {
        u8 *bufs[2] = { heap_mem[1], heap_moved };
        u64 tagged[2];
        for (i = 0; i < 2; i++) {
            heaps[1] = HuMemHeapInit(bufs[i], sizeof(heap_mem[1]));
            HuMemMemoryAllocNum(heaps[1], 100, (uintptr_t)heaps[1] + 0x200, 0);
            tagged[i] = netplay_heap_hash();
        }
        assert(tagged[0] == tagged[1] && tagged[0] != h1);
        heaps[1] = HuMemHeapInit(heap_mem[1], sizeof(heap_mem[1]));
        other = HuMemMemoryAllocNum(heaps[1], 100, 5, 0);
        assert(netplay_heap_hash() == h1);
    }

    /* Start: the session begins with the folded seed; nothing else until the tick. */
    GWSystem.turn = 9;
    GWSystem.flag[1][0] = 0xFF;
    GWPlayer[1].coins = 50;
    GWGameStat.language = 3;
    GWGameStat.mg_custom[0] = 5;
    GWGameStat.total_stars = 77;
    wipeData.stat = 1;
    wipeData.mode = WIPE_MODE_OUT;
    assert(netplay_match_start(&m));
    assert(netplay_match_running());
    assert(begun == 1 && begun_seed == (0x11111111u ^ 0x22222222u));
    assert(!netplay_match_start(&m)); /* already starting */
    assert(party_calls == 0 && proc == NULL);
    netplay_ovl_start(DLL_w01dll + m.board); /* before the tick: nothing to scrub yet */
    assert(freed[50] == 0xBB);

    /* Tick: the state, then the start process at the top priority. */
    netplay_match_tick();
    assert(pad_resets == 1 && vwait == 1 && GlobalCounter == 0);
    assert(party_calls == 1);
    {
        s32 want[8] = { 0, 1, 0, 25, 0, 3, 6, 9 };
        assert(memcmp(party_args, want, sizeof(want)) == 0);
    }
    assert(save_board == 2 && GWSystem.board == 2 && GWSystem.turn == 0 && GWSystem.flag[1][0] == 0);
    assert(GWPlayer[1].coins == 0);
    assert(GWSystem.save_mode == 1 && GWSystem.mess_speed == 1 && GWSystem.mg_list == 0 && GWSystem.explain_mg == 1
        && GWSystem.show_com_mg == 1);
    for (i = 0; i < 4; i++) {
        assert(GWPlayerCfg[i].character == 7 - i && GWPlayerCfg[i].pad_idx == i && GWPlayerCfg[i].diff == i);
        assert(GWPlayerCfg[i].iscom == (i >= 2) && GWPlayerCfg[i].group == 0);
    }
    assert(GWGameStat.language == 1 && GWGameStat.mg_custom[0] == 0 && GWGameStat.total_stars == 0);
    assert(GWGameStat.mg_avail[0] == 0xFFFFFFFF && GWGameStat.open_w06 && GWGameStat.veryHardUnlock);
    assert(wipeData.stat == 0 && wipeData.mode == WIPE_MODE_BLANK && wipe_rgb[0] == 0xFF);
    assert(proc != NULL && proc_prio == 0xFFFF);
    assert(aux_resets == 1);
    netplay_match_tick(); /* once only */
    assert(party_calls == 1);

    /* Frame 0: the process calls the board with the menus' history and ends. */
    proc();
    assert(called == DLL_w01dll + 2 && omovlhisidx == 3);
    assert(his[0].ovl == DLL_bootdll && his[1].ovl == DLL_modeseldll && his[1].event == 1 && his[2].ovl == DLL_mstory3dll);
    assert(nmotions == 4 && motions[0] == 7 && motions[3] == 4 && ended == 1);
    assert(overlay_kills == 7); /* the menu's own processes are killed, libdol's kept */
    assert(sys_inits == 111 && SystemInitF); /* started before bootDll's init: done here */

    /* N38: the old board's processes (a reload mid-board) ran on in frame 0 and wrote the state on this peer only. */
    GWSystem.player_curr = 3;
    GWSystem.flag[1][0] = 0x7C;
    GWPlayer[1].coins = 99;
    reseeded = 0;
    ar_board = 0x1000; /* N57: the old board's data in ARAM (it had been to a minigame) */
    /* N57: a block of code that is gone (a minigame's, unloaded), and one of loaded code. */
    gone = HuMemMemoryAllocNum(heaps[1], 300, -256, 0x10);
    kept = HuMemMemoryAllocNum(heaps[1], 300, -256, (uintptr_t)netplay_match_tick);

    /* Another overlay first: no scrub. The board: free memory zeroed, used blocks kept, layout unchanged. */
    netplay_ovl_start(DLL_w01dll + 3);
    assert(freed[50] == 0xBB);
    assert(GWPlayer[1].coins == 99 && reseeded == 0);
    assert(effect_resets == 0 && dir_close_alls == 0 && ar_frees == 0);
    netplay_ovl_start(DLL_w01dll + 2);
    /* ... and the board's start sets the match state and seeds again, as a peer that came from a menu has them. */
    assert(party_calls == 2 && reseeded == 1);
    /* N57: and drops what the old board left, as a peer that came from a menu has none of it. */
    assert(effect_resets == 1 && dir_close_alls == 1 && ar_frees == 1);
    ar_board = 0;
    ar_frees = 0;
    assert(netplay_heap_hash() != h1); /* the block of loaded code stays, the other went */
    HuMemMemoryFree(kept, 0);
    (void)gone;
    assert(GWSystem.flag[1][0] == 0 && GWPlayer[1].coins == 0 && save_board == 2 && GWSystem.board == 2);
    reseeded = 0;
    for (i = 0; i < 200; i++) {
        assert(freed[i] == 0);
    }
    assert(((u8 *)heaps[0])[0x3F00] == 0 && used[99] == 0x11);
    assert(netplay_heap_hash() == h1);
    memset(freed, 0xCC, 10);
    netplay_ovl_start(DLL_w01dll + 2); /* once only */
    assert(freed[0] == 0xCC);

    /* N25: the party is over when a menu starts after the board (minigames and the results are not menus); the
     * menu gets the player's own GWGameStat before it sets up. */
    assert(!netplay_match_over());
    netplay_ovl_start(DLL_m401dll);
    netplay_ovl_start(DLL_resultdll);
    netplay_ovl_start(DLL_w01dll + 2);
    netplay_ovl_start(DLL_mstory3dll);
    assert(!netplay_match_over() && GWGameStat.language == 1);
    finished.ok = 1;
    netplay_ovl_start(DLL_modeseldll);
    assert(netplay_match_over() && netplay_match_running());
    /* N31: its online save goes, by the match's room; no checkpoint was written, so no path to fall back on. */
    assert(finished.calls == 1 && strcmp(finished.dir, "/tmp/user") == 0 && strstr(finished.room, "\"started\":true"));
    assert(deleted.calls == 0);
    assert(GWGameStat.language == 3 && GWGameStat.mg_custom[0] == 5 && GWGameStat.total_stars == 77);
    GWGameStat.total_stars = 78; /* the menu changes it: not taken back again */
    netplay_ovl_start(DLL_modeseldll);
    assert(netplay_match_over() && finished.calls == 1); /* once */

    /* End: the player's own GWGameStat stays; the match and its "over" are gone. */
    netplay_match_end();
    assert(!netplay_match_running() && !netplay_match_over());
    assert(GWGameStat.language == 3 && GWGameStat.mg_custom[0] == 5 && GWGameStat.total_stars == 78);
    GWGameStat.total_stars = 77;

    /* Teams and Bowser's board: groups from the seats, black wipe. A --script run keeps its session. */
    m.teams = 1;
    m.board = 5;
    scripted = 1;
    proc = NULL;
    assert(netplay_match_start(&m) && begun == 1 && reseeded == 1); /* the seeds again, the frame count kept */
    netplay_match_tick();
    assert(party_args[0] == 1 && wipe_rgb[0] == 0);
    for (i = 0; i < 4; i++) {
        assert(GWPlayerCfg[i].group == i % 2);
    }
    /* A menu before the board started (the match start itself leaves one) is not the end. */
    netplay_ovl_start(DLL_modeseldll);
    assert(!netplay_match_over());
    netplay_match_end();
    assert(GWGameStat.language == 3);

    /* --match. */
    assert(netplay_match_parse_debug("3@900", &m, &at) && at == 900 && m.board == 3 && m.turns == 10);
    assert(netplay_match_invalid(&m) == NULL && !m.seats[0].cpu && m.seats[1].cpu);
    assert(!netplay_match_parse_debug("6@900", &m, &at));
    assert(!netplay_match_parse_debug("3", &m, &at));
    assert(!netplay_match_parse_debug("3@", &m, &at));
    assert(!netplay_match_parse_debug("3@0", &m, &at));
    assert(!netplay_match_parse_debug("3@9x", &m, &at));

    /* N9 checkpoint: during a match, the SLSaveBoard blob of the board state goes to pbnet with the match's room and
     * the seeds; the card buffer is as it was. */
    {
        static u8 card[sizeof(saveBuf)], blob[NETPLAY_BLOB_MAX], back[NETPLAY_BLOB_MAX];
        u32 size = netplay_board_blob_size();
        NetplayMatch c;
        const char *room_path = getenv("MATCH_TEST_ROOM");

        assert(size == sizeof(SystemState) + 4 * sizeof(PlayerState));
        for (i = 0; i < (s32)sizeof(saveBuf); i++) {
            saveBuf.buf[i] = (u8)(i * 7);
        }
        memcpy(card, &saveBuf, sizeof(saveBuf));
        netplay_checkpoint(); /* no match: nothing */
        assert(saved.calls == 0 && netplay_checkpoint_last(back, sizeof(back)) == 0);

        m = base();
        m.board = 3;
        proc = NULL;
        assert(netplay_match_start(&m));
        netplay_match_tick();
        /* the match is played up to a checkpoint */
        GWSystem.turn = 7;
        GWSystem.max_turn = 25;
        GWSystem.flag[1][0] = 0x01;
        for (i = 0; i < 4; i++) {
            GWPlayer[i].character = 7 - i;
            GWPlayer[i].port = i;
            GWPlayer[i].com = i >= 2;
            GWPlayer[i].diff = i;
            GWPlayer[i].coins = (s16)(10 * i + 3);
            GWPlayer[i].team = i % 2;
        }
        saved.ok = 1;
        netplay_checkpoint();
        assert(saved.calls == 1 && saved.len == (int)size && strcmp(saved.dir, "/tmp/user") == 0);
        assert(memcmp(saved.blob, &GWSystem, sizeof(SystemState)) == 0);
        assert(memcmp(saved.blob + sizeof(SystemState) + 2 * sizeof(PlayerState), &GWPlayer[2], sizeof(PlayerState)) == 0);
        assert(saved.seeds[0] == 0xDEF0 && saved.seeds[1] == 0x9ABC && saved.seeds[2] == 0x1234 && saved.seeds[3] == 0x5678);
        assert(strstr(saved.room, "\"id\":\"local-1111111122222222\"") && strstr(saved.room, "\"started\":true"));
        assert(strstr(saved.room, "\"board\":3,\"turns\":25") && strstr(saved.room, "\"key\":\"local-1\""));
        assert(strstr(saved.room, "\"peer\":\"cpu\",\"name\":\"CPU\",\"key\":\"\""));
        if (room_path != NULL) { /* match_test.sh checks it is a Room pbnet reads */
            FILE *f = fopen(room_path, "w");
            assert(f != NULL);
            fputs(saved.room, f);
            fclose(f);
        }
        assert(memcmp(card, &saveBuf, sizeof(saveBuf)) == 0);
        assert(netplay_checkpoint_last(back, sizeof(back)) == size && memcmp(back, saved.blob, size) == 0);
        saved.ok = 0; /* a failed write: the match goes on, the blob is still kept */
        netplay_checkpoint();
        assert(saved.calls == 2 && netplay_checkpoint_last(back, sizeof(back)) == size);
        memcpy(blob, saved.blob, size);
        /* N31: the party ends. No started room by the match's id (finish 0): the file the last good checkpoint wrote
         * goes. */
        netplay_ovl_start(DLL_w01dll + 3);
        finished.calls = 0;
        finished.ok = 0;
        deleted.ok = 1;
        netplay_ovl_start(DLL_modeseldll);
        assert(netplay_match_over() && finished.calls == 1);
        assert(strstr(finished.room, "\"id\":\"local-1111111122222222\""));
        assert(deleted.calls == 1 && strcmp(deleted.path, "/tmp/user/online/x.json") == 0);
        netplay_match_end();
        assert(netplay_checkpoint_last(back, sizeof(back)) == 0);

        /* Continue: refused for a blob of another size or board; nothing started. */
        c = base();
        assert(!netplay_match_continue(&c, blob, size - 1));
        GWSystem.board = 6;
        netplay_board_blob_get(back, sizeof(back));
        assert(netplay_board_blob_board(back, size) == 6 && GWSystem.board == 6);
        assert(!netplay_match_continue(&c, back, size));
        assert(!netplay_match_running());

        /* Continue on another peer (stale state, a different card): seat 1 is the CPU's now (its player didn't come
         * back), seat 2 a person's. */
        memset(&GWSystem, 0x77, sizeof(GWSystem));
        memset(GWPlayer, 0x66, sizeof(GWPlayer));
        memset(GWPlayerCfg, 0x55, sizeof(GWPlayerCfg));
        for (i = 0; i < (s32)sizeof(saveBuf); i++) {
            saveBuf.buf[i] = (u8)(i * 13);
        }
        memcpy(card, &saveBuf, sizeof(saveBuf));
        c.board = 0; /* ignored: the save's */
        c.seats[1].cpu = 1;
        c.seats[1].difficulty = 3;
        c.seats[2].cpu = 0;
        proc = NULL;
        saveExecF = 1;
        play_clears = 0;
        wipe_rgb[0] = 0;
        assert(netplay_match_continue(&c, blob, size));
        assert(netplay_match_running() && GWSystem.turn != 7); /* nothing before the tick */
        assert(!netplay_match_continue(&c, blob, size)); /* already starting */
        assert(netplay_checkpoint_last(back, sizeof(back)) == size && memcmp(back, blob, size) == 0);
        party_calls = 0;
        netplay_match_tick();
        assert(party_calls == 0 && play_clears == 1 && saveExecF == 0);
        assert(GWSystem.turn == 7 && GWSystem.max_turn == 25 && GWSystem.board == 3 && GWSystem.flag[1][0] == 0x01);
        assert(GWSystem.save_mode == 1);
        for (i = 0; i < 4; i++) {
            BOOL cpu = i == 1 || i == 3;
            assert(GWPlayer[i].coins == 10 * i + 3 && GWPlayer[i].character == 7 - i && GWPlayerCfg[i].character == 7 - i);
            assert(GWPlayerCfg[i].pad_idx == i && GWPlayer[i].port == i);
            assert(GWPlayerCfg[i].iscom == cpu && GWPlayer[i].com == cpu);
            assert(GWPlayerCfg[i].group == i % 2);
        }
        assert(GWPlayerCfg[1].diff == 3 && GWPlayer[1].diff == 3 && GWPlayerCfg[2].diff == 2);
        assert(memcmp(card, &saveBuf, sizeof(saveBuf)) == 0);
        assert(wipe_rgb[0] == 0xFF && proc != NULL);
        proc();
        assert(called == DLL_w01dll + 3);
        assert(sys_inits == 111); /* the system was up: not again */
        /* N38: frame 0's teardown of an old board changed the state; the board's start loads the checkpoint again. */
        GWSystem.turn = 1;
        GWSystem.flag[1][0] = 0x7C;
        GWPlayer[2].coins = 0;
        GWPlayer[1].com = 0;
        netplay_ovl_start(DLL_w01dll + 3);
        assert(GWSystem.turn == 7 && GWSystem.flag[1][0] == 0x01 && GWPlayer[2].coins == 23);
        assert(GWPlayer[1].com == 1 && GWPlayerCfg[1].diff == 3 && play_clears == 2 && saveExecF == 0);
        /* N31: the continued match ends before this peer wrote a checkpoint: its save goes by the match's id, and
         * the last match's path is not deleted again. */
        finished.calls = deleted.calls = 0;
        finished.ok = 1;
        netplay_ovl_start(DLL_modeseldll);
        assert(finished.calls == 1 && deleted.calls == 0);
        finished.ok = 0;
        netplay_match_end();
        netplay_match_start(&c);
        netplay_match_tick();
        netplay_ovl_start(DLL_w01dll + c.board);
        netplay_ovl_start(DLL_modeseldll);
        assert(finished.calls == 2 && deleted.calls == 0);
        netplay_match_end();

        /* N52: a minigame's resync point (the blob, the mark, mg_setup's sides) resumes into instDll, with the board
         * under it in the history (instDll -> resultDll -> the board). */
        {
            static u8 mg[NETPLAY_BLOB_MAX];
            static const u8 sides[4] = { 1, 0, 1, 1 };
            u8 groups[4];
            u32 mg_size = size + NETPLAY_MG_TRAILER_SIZE;

            memcpy(mg, blob, size);
            memcpy(mg + size, NETPLAY_MG_MARK, NETPLAY_MG_MARK_SIZE);
            memcpy(mg + size + NETPLAY_MG_MARK_SIZE, sides, 4);
            assert(netplay_minigame_blob(mg, mg_size, groups) == size && memcmp(groups, sides, 4) == 0);
            assert(netplay_minigame_blob(blob, size, groups) == 0); /* a turn's or a save's */
            mg[size] ^= 1;
            assert(netplay_minigame_blob(mg, mg_size, groups) == 0 && !netplay_match_continue(&c, mg, mg_size));
            mg[size] ^= 1;
            assert(!netplay_match_continue(&c, mg, size + NETPLAY_MG_MARK_SIZE) && !netplay_match_running());

            proc = NULL;
            roll_resets = mg_points = 0;
            assert(netplay_match_continue(&c, mg, mg_size));
            assert(netplay_checkpoint_last(back, sizeof(back)) == size && memcmp(back, blob, size) == 0);
            netplay_match_tick();
            assert(GWSystem.turn == 7 && GWPlayer[2].coins == 23);
            for (i = 0; i < 4; i++) {
                assert(GWPlayerCfg[i].group == sides[i]);
            }
            wipe_rgb[0] = 0;
            omovlhisidx = 7;
            proc();
            assert(called == DLL_instdll && omovlhisidx == 4 && wipe_rgb[0] == 0xFF);
            assert(his[2].ovl == DLL_mstory3dll && his[3].ovl == DLL_w01dll + 3 && his[3].event == 0);
            /* The board's start is not the one waited for: instDll's sets the state again (N38), and it takes no point. */
            GWSystem.turn = 1;
            GWPlayerCfg[0].group = 0;
            netplay_ovl_start(DLL_w01dll + 3);
            assert(GWSystem.turn == 1 && roll_resets == 0);
            netplay_ovl_start(DLL_instdll);
            assert(GWSystem.turn == 7 && GWPlayerCfg[0].group == 1 && roll_resets == 1 && mg_points == 0);
            /* The minigame, the results, the board: the next round's instDll takes a point. */
            netplay_ovl_start(DLL_m401dll);
            netplay_ovl_start(DLL_resultdll);
            netplay_ovl_start(DLL_w01dll + 3);
            assert(!netplay_match_over() && mg_points == 0 && roll_resets == 1);
            netplay_ovl_start(DLL_instdll);
            assert(mg_points == 1);
            netplay_match_end();
        }
    }

    /* N34: a match left before its end goes back to modeseldll. The board leaves through its own exit once it runs
     * (omSysExitReq, flag (1,27): past the results); the player's own GWGameStat comes back at netplay_end and again
     * once modeseldll runs (the board's teardown may write the match's settings into it), with the minigame data
     * closed. */
    {
        NetplayMatch m = base();
        s32 i;

        GWGameStat.language = 5;
        assert(netplay_match_start(&m));
        netplay_match_tick();
        assert(GWGameStat.language == 1);
        proc();
        omcurovl = DLL_w01dll + m.board;
        netplay_ovl_start(omcurovl);
        netplay_match_end();
        assert(GWGameStat.language == 5 && !netplay_match_leaving());
        netplay_match_leave_poll(); /* nothing to leave */
        netplay_match_leave();
        assert(netplay_match_leaving());
        proc = NULL;
        netplay_match_leave_poll(); /* the board does not run yet (1,16) */
        assert(!omSysExitReq && proc == NULL);
        _SetFlag(FLAG_ID_MAKE(1, 16));
        _SetFlag(FLAG_ID_MAKE(1, 17)); /* ending itself (a minigame next): left alone */
        netplay_match_leave_poll();
        assert(!omSysExitReq);
        _ClearFlag(FLAG_ID_MAKE(1, 17));
        netplay_match_leave_poll();
        assert(omSysExitReq && _CheckFlag(FLAG_ID_MAKE(1, 27)) && proc == NULL);
        omSysExitReq = 0;
        netplay_match_leave_poll(); /* asked once */
        assert(!omSysExitReq && proc == NULL);
        /* BoardKill, then KillBoardFunc's omOvlReturnEx: an overlay switch is waited for. */
        GWGameStat.language = 9;
        omcurovl = DLL_NONE;
        omnextovl = DLL_modeseldll;
        netplay_match_leave_poll();
        assert(netplay_match_leaving() && proc == NULL);
        omnextovl = DLL_NONE;
        omcurovl = DLL_modeseldll;
        GWSystem.mg_next = 1;
        open_dirs[0] = DATADIR_INST;
        open_dirs[1] = DATADIR_M402;
        netplay_match_leave_poll();
        assert(!netplay_match_leaving() && GWGameStat.language == 5);
        assert(nclosed == 2 && closed_dirs[0] == DATADIR_INST && closed_dirs[1] == DATADIR_M402);

        /* From a minigame (or instDll, the results): the leave process switches to modeseldll as the party's end
         * does, with the pauses lifted and the board's ARAM data freed. */
        memset(open_dirs, 0, sizeof(open_dirs));
        nclosed = 0;
        omcurovl = DLL_m401dll;
        _SetFlag(FLAG_ID_MAKE(1, 0));
        ar_board = 0x1000;
        netplay_match_leave();
        netplay_match_leave_poll();
        assert(proc != NULL && proc_prio == 0xFFFF);
        ended = 0;
        proc();
        assert(unpaused == 111111 && fx_stops == 1 && ar_frees == 1 && !_CheckFlag(FLAG_ID_MAKE(1, 0)) && ended == 1);
        assert(gone_to.ovl == DLL_modeseldll && gone_to.event == 1);
        assert(omovlhisidx == 1 && his[0].ovl == DLL_bootdll && his[1].ovl == DLL_modeseldll && his[1].event == 1);
        proc = NULL;
        netplay_match_leave_poll(); /* one process per overlay */
        assert(proc == NULL);
        omcurovl = DLL_modeseldll;
        netplay_match_leave_poll();
        assert(!netplay_match_leaving() && nclosed == 0);

        /* A board that never gets to run (or to its exit) is switched too, after LEAVE_BOARD_FRAMES. */
        _ClearFlag(FLAG_ID_MAKE(1, 16));
        omcurovl = DLL_w01dll;
        netplay_match_leave();
        for (i = 0; i < 599 && proc == NULL; i++) {
            netplay_match_leave_poll();
        }
        assert(proc == NULL);
        netplay_match_leave_poll();
        assert(proc != NULL);
        omcurovl = DLL_modeseldll;
        netplay_match_leave_poll();
        assert(!netplay_match_leaving());
    }

    printf("match_test: all checks passed\n");
    return 0;
}
