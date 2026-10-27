/* The match start (src/port/netplay_match.c) against stub game functions and the real heap code
 * (src/game/memory.c). Run with tools/netplay/match_test.sh. */
#include "dolphin.h"
#include "game/gamework_data.h"
#include "game/memory.h"
#include "game/object.h"
#include "game/process.h"
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
void CharMotionInit(s16 c) { motions[nmotions++ % 4] = c; }
void HuAudSeqAllFadeOut(s32 s) { (void)s; }
void HuAudSStreamAllFadeOut(s32 s) { (void)s; }
static struct { OMOVL ovl; s32 event; } his[16];
void omOvlHisChg(s32 level, OMOVL o, s32 e, s32 st) { (void)st; his[omovlhisidx - level].ovl = o; his[omovlhisidx - level].event = e; }
static OMOVL called = DLL_NONE;
void omOvlCallEx(OMOVL o, s16 a, s32 e, s32 st) { (void)a; (void)e; (void)st; called = o; his[++omovlhisidx].ovl = o; }
void HuPrcEnd(void) { ended++; }
static void (*proc)(void); static u16 proc_prio;
Process *HuPrcCreate(void (*f)(void), u16 prio, u32 stack, s32 extra)
{
    static Process p;
    (void)stack; (void)extra;
    proc = f;
    proc_prio = prio;
    return &p;
}

static u8 heap_mem[2][0x4000];
static void *heaps[HEAP_MAX];
void *HuMemHeapPtrGet(HeapID h) { return heaps[h]; }
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
    u8 *used, *freed, *other;
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
    netplay_match_tick(); /* once only */
    assert(party_calls == 1);

    /* Frame 0: the process calls the board with the menus' history and ends. */
    proc();
    assert(called == DLL_w01dll + 2 && omovlhisidx == 3);
    assert(his[0].ovl == DLL_bootdll && his[1].ovl == DLL_modeseldll && his[1].event == 1 && his[2].ovl == DLL_mstory3dll);
    assert(nmotions == 4 && motions[0] == 7 && motions[3] == 4 && ended == 1);

    /* Another overlay first: no scrub. The board: free memory zeroed, used blocks kept, layout unchanged. */
    netplay_ovl_start(DLL_w01dll + 3);
    assert(freed[50] == 0xBB);
    netplay_ovl_start(DLL_w01dll + 2);
    for (i = 0; i < 200; i++) {
        assert(freed[i] == 0);
    }
    assert(((u8 *)heaps[0])[0x3F00] == 0 && used[99] == 0x11);
    assert(netplay_heap_hash() == h1);
    memset(freed, 0xCC, 10);
    netplay_ovl_start(DLL_w01dll + 2); /* once only */
    assert(freed[0] == 0xCC);

    /* End: the player's own GWGameStat is back. */
    netplay_match_end();
    assert(!netplay_match_running());
    assert(GWGameStat.language == 3 && GWGameStat.mg_custom[0] == 5 && GWGameStat.total_stars == 77);

    /* Teams and Bowser's board: groups from the seats, black wipe. A --script run keeps its session. */
    m.teams = 1;
    m.board = 5;
    scripted = 1;
    proc = NULL;
    assert(netplay_match_start(&m) && begun == 1);
    netplay_match_tick();
    assert(party_args[0] == 1 && wipe_rgb[0] == 0);
    for (i = 0; i < 4; i++) {
        assert(GWPlayerCfg[i].group == i % 2);
    }
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

    printf("match_test: all checks passed\n");
    return 0;
}
