/* netplay_tick_begin / netplay_pad_read (src/port/netplay.c) against a fake pbnet and fake controllers.
 * Run with tools/netplay/tick_gate_test.sh. */
#include "dolphin.h"
#include "game/gamework_data.h"
#include "game/object.h"
#include "port/netplay.h"
#include "port/pbnet.h"
#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>

SystemState GWSystem; PlayerState GWPlayer[4]; PlayerConfig GWPlayerCfg[4];
u16 HuPadBtn[4], HuPadBtnDown[4], HuPadBtnRep[4]; s8 HuPadStkX[4], HuPadStkY[4], HuPadSubStkX[4], HuPadSubStkY[4];
u8 HuPadTrigL[4], HuPadTrigR[4], HuPadDStk[4], HuPadDStkRep[4]; s8 HuPadErr[4];
OMOVL omcurovl; s32 rnd_seed; u32 boardRandSeed; BOOL PartyBoard_IsRunning = TRUE; static u32 fseed;
u32 frandSeedGet(void) { return fseed; } void frandSeedSet(u32 s) { fseed = s; }
/* netplay_match.c is not linked here (tools/netplay/match_test.c tests it). */
static int over, match_ends, still_drops;
void netplay_match_tick(void) {} u64 netplay_heap_hash(void) { return 0; }
void netplay_match_end(void) { match_ends++; over = 0; }
/* N64: the match's results still run here (the lobby waits for them). */
static int results;
BOOL netplay_match_results(void) { return results; }
BOOL netplay_match_over(void) { return over; }
void netplay_still_drop(void) { still_drops++; }
/* N34: the match running, its leave and the leave's per-loop poll. */
static int running, leaves, leave_polls;
BOOL netplay_match_running(void) { return running; }
void netplay_match_leave(void) { leaves++; running = 0; }
void netplay_match_leave_poll(void) { leave_polls++; }
static NetplayMatch started; static int start_calls;
BOOL netplay_match_start(const NetplayMatch *m) { started = *m; start_calls++; return TRUE; }
const char *netplay_match_invalid(const NetplayMatch *m) { return m->teams && m->seats[0].team == m->seats[1].team && m->seats[0].team == m->seats[2].team ? "teams" : NULL; }
static int continue_calls; static u32 continued_size;
BOOL netplay_match_continue(const NetplayMatch *m, const void *b, u32 n) { started = *m; (void)b; continue_calls++; continued_size = n; return TRUE; }
int pbnet_save_blob(const char *p, uint8_t *b, int cap) { (void)p; (void)b; (void)cap; return 0; }
BOOL netplay_match_parse_debug(const char *s, NetplayMatch *m, u32 *at) { (void)s; (void)m; (void)at; return FALSE; }
void OSReport(const char *fmt, ...) { va_list a; va_start(a, fmt); vprintf(fmt, a); va_end(a); }

static PADStatus phys[4]; static int padread_calls;
u32 PADRead(PADStatus *s) { padread_calls++; memcpy(s, phys, sizeof(phys)); return 0; }

static int on, wait_now, hash_calls; static u32 fr, taken_for = 0xFFFFFFFF, hashed_frame;
static PADStatus taken[4];
int pbnet_lockstep_active(void) { return on; }
uint32_t pbnet_frame(void) { return fr; }
void pbnet_set_hash(uint32_t f, uint64_t h) { (void)h; hash_calls++; hashed_frame = f; }
/* N11: queued mismatches, each handed to pbnet_desync_recover. */
static int desyncs_queued, recover_calls; static u32 recovered_frame; static int recovered_peer;
int pbnet_poll_desync(uint32_t *a, int *b, uint64_t *c, uint64_t *d)
{
    if (desyncs_queued == 0) return 0;
    desyncs_queued--;
    *a = 30; *b = 1; *c = 5; *d = 6;
    return 1;
}
int pbnet_desync_recover(uint32_t f, int p, uint64_t o, uint64_t t)
{
    assert(o == 5 && t == 6);
    recover_calls++; recovered_frame = f; recovered_peer = p;
    return 1;
}
int pbnet_step(const struct PADStatus *local, int n, struct PADStatus *out)
{
    if (!on) return PBNET_OFF;
    assert(n == 4);
    if (taken_for != fr) { memcpy(taken, local, sizeof(taken)); taken_for = fr; }
    if (wait_now) return PBNET_WAIT;
    memset(out, 0, 4 * sizeof(*out));
    out[0] = taken[0];           /* our seat 0 */
    out[2].button = 0x1234;      /* a remote seat */
    fr++;
    return PBNET_READY;
}

/* Fake lobby for netplay_lobby_poll. */
static int lobby_state, leave_calls; static const char *lobby_text;
int pbnet_lobby_state(void) { return lobby_state; }
int pbnet_lobby_json(char *buf, int cap) { return snprintf(buf, buf != NULL && cap > 0 ? (size_t)cap : 0, "%s", lobby_text); }
void pbnet_lobby_leave(void) { leave_calls++; lobby_state = PBNET_LOBBY_NONE; }
/* N10: the reload epoch, the room's checkpoint and the lockstep release. */
static u32 epoch_now, went = 0xFFFFFFFF; static int go_calls, blob_len;
uint32_t pbnet_recover_epoch(void) { return epoch_now; }
int pbnet_recover_go(uint32_t e) { go_calls++; went = e; return 1; }
/* N46: a held-back resync is polled on every loop of a started lobby. */
static int resync_polls;
int pbnet_resync_poll(void) { resync_polls++; return 0; }
int pbnet_lobby_checkpoint(uint8_t *buf, int cap) { if (buf != NULL) memset(buf, 7, (size_t)(blob_len < cap ? blob_len : cap)); return blob_len; }
static int finish_calls, back_calls;
void pbnet_lockstep_finish(void) { finish_calls++; on = 0; }
int pbnet_lobby_back(void) { back_calls++; if (lobby_state != PBNET_LOBBY_STARTED) return 0; lobby_state = PBNET_LOBBY_OPEN; return 1; }
static int stop_calls;
void pbnet_lockstep_stop(void) { stop_calls++; on = 0; }
/* N39: the rejoin record is forgotten when the match is over (never when it is left mid-way). */
static int forget_calls;
void pbnet_rejoin_forget(void) { forget_calls++; }

/* N25: the match is over (netplay_match_over) at the end of a frame: the lockstep finishes, the session ends and the
 * lobby goes back to open, once, at the end of that frame; the UI hears it once. */
static void match_end_checks(void)
{
    u32 at;
    netplay_begin(9);
    on = 1;
    lobby_state = PBNET_LOBBY_STARTED;
    finish_calls = back_calls = match_ends = still_drops = forget_calls = 0;
    netplay_frame_end();
    assert(netplay_active() && finish_calls == 0 && forget_calls == 0 && !netplay_lobby_returned());
    over = 1;
    at = netplay_frame();
    netplay_frame_end();
    assert(netplay_frame() == at + 1);
    assert(!netplay_active() && !netplay_lockstep() && finish_calls == 1 && match_ends == 1 && still_drops == 1);
    assert(back_calls == 1 && lobby_state == PBNET_LOBBY_OPEN && forget_calls == 1);
    /* N64: not while this machine's results run; then once. */
    results = 1;
    assert(!netplay_lobby_returned());
    results = 0;
    assert(netplay_lobby_returned() && !netplay_lobby_returned());
    /* Back on local controllers. */
    phys[0].button = PAD_BUTTON_START;
    assert(netplay_tick_begin());
    {
        PADStatus got[4];
        netplay_pad_read(got);
        assert(got[0].button == PAD_BUTTON_START);
    }
    /* Without a lockstep (a local match with no lobby): the session ends, no lobby to go back to. */
    netplay_begin(9);
    lobby_state = PBNET_LOBBY_NONE;
    over = 1;
    netplay_frame_end();
    assert(!netplay_active() && back_calls == 1 && !netplay_lobby_returned());
    assert(forget_calls == 2); /* the lobby closed meanwhile: still forgotten */
}

/* N64: the lockstep hash is gameplay state; the seeds are only in the full hash. Gameplay points reseed from the
 * session seed, turn, player, point and overlay: not from the frame or the seeds the effects left. */
static void hash_checks(void)
{
    u64 h = netplay_hash(), full = netplay_full_hash();
    u32 f, r, b;
    fseed ^= 1;
    assert(netplay_hash() == h && netplay_full_hash() != full);
    GWPlayer[2].coins++;
    assert(netplay_hash() != h);
    GWPlayer[2].coins--;
    assert(netplay_gameplay_ovl(DLL_w06dll) && netplay_gameplay_ovl(DLL_m463dll) && netplay_gameplay_ovl(DLL_m300dll));
    assert(!netplay_gameplay_ovl(DLL_mstory3dll) && !netplay_gameplay_ovl(DLL_instdll) && !netplay_gameplay_ovl(DLL_resultdll));
    assert(!netplay_gameplay_ovl(DLL_modeseldll) && !netplay_gameplay_ovl(DLL_NONE));
    GWSystem.turn = 4; GWSystem.player_curr = 2;
    fseed = 11; rnd_seed = 12; boardRandSeed = 13;
    netplay_point_reseed(NETPLAY_POINT_TURN, DLL_w01dll);
    f = fseed; r = (u32)rnd_seed; b = boardRandSeed;
    assert(f != 11 && r != 12 && b != 13 && f != r && r != b && f != 0 && r != 0 && b != 0);
    fseed = 99; rnd_seed = 98; boardRandSeed = 97;
    netplay_frame_end(); /* another frame, other seeds before: the same point gives the same seeds */
    netplay_point_reseed(NETPLAY_POINT_TURN, DLL_w01dll);
    assert(fseed == f && (u32)rnd_seed == r && boardRandSeed == b);
    netplay_point_reseed(NETPLAY_POINT_MINIGAME, DLL_m401dll);
    assert(fseed != f);
    netplay_point_reseed(NETPLAY_POINT_MINIGAME, DLL_m402dll);
    assert(fseed != f);
    GWSystem.player_curr = 3;
    netplay_point_reseed(NETPLAY_POINT_TURN, DLL_w01dll);
    assert(fseed != f);
    memset(&GWSystem, 0, sizeof(GWSystem));
}

static char fixture[4096];

/* The fixture with the first `from` replaced by `to`. */
static const char *edited(const char *from, const char *to)
{
    static char out[4096];
    const char *at = strstr(fixture, from);
    assert(at != NULL);
    snprintf(out, sizeof(out), "%.*s%s%s", (int)(at - fixture), fixture, to, at + strlen(from));
    return out;
}

/* netplay_match_from_lobby on the started lobby JSON pbnet gives (net/src/lobby_c.rs started_json_fixture). */
static void lobby_checks(void)
{
    NetplayMatch m;
    u8 local = 0;
    FILE *f = fopen("tools/netplay/lobby_started.json", "r");
    size_t n;
    assert(f != NULL);
    n = fread(fixture, 1, sizeof(fixture) - 1, f);
    fclose(f);
    fixture[n] = '\0';

    assert(netplay_match_from_lobby(fixture, &m, &local) == NULL);
    assert(local == 1);
    assert(m.seed == 0xFEDCBA9876543210ULL && m.board == 3 && m.turns == 25 && !m.bonus && m.teams);
    assert(m.seats[0].character == 0 && m.seats[1].character == 2 && m.seats[2].character == 1 && m.seats[3].character == 3);
    assert(!m.seats[0].cpu && m.seats[1].cpu && m.seats[2].cpu && m.seats[3].cpu);
    assert(m.seats[0].difficulty == 1 && m.seats[2].difficulty == 3);
    assert(m.seats[0].team == 0 && m.seats[1].team == 0 && m.seats[2].team == 1 && m.seats[3].team == 1);
    assert(m.seats[0].handicap == 0 && m.seats[1].handicap == 2 && m.seats[2].handicap == 9 && m.seats[3].handicap == 1);
    /* Other machines' seats; white space anywhere; handicaps before seats. */
    assert(netplay_match_from_lobby(edited("\"seats\":[0]", "\"seats\" : [ 1 , 3 ]"), &m, &local) == NULL && local == 10);
    assert(netplay_match_from_lobby(edited("\"id\":\"m1\",", "\"id\":\"m1\",\"handicaps\":[4,4,4,4],"), &m, NULL) == NULL);
    assert(m.seats[2].handicap == 9); /* the later one wins */

    memset(&m, 0xAB, sizeof(m));
    assert(netplay_match_from_lobby(edited("\"state\":\"started\"", "\"state\":\"open\""), &m, NULL) != NULL);
    assert(m.board == 0xAB); /* untouched on failure */
    assert(netplay_match_from_lobby(edited("\"started\":true", "\"started\":false"), &m, NULL) != NULL);
    assert(netplay_match_from_lobby(edited("\"room\":{", "\"room\":null,\"x\":{"), &m, NULL) != NULL);
    assert(netplay_match_from_lobby(edited("\"seats\":[0]", "\"seats\":[]"), &m, NULL) != NULL);
    assert(netplay_match_from_lobby(edited("{\"peer\":\"host\",\"name\":\"A \\\"quoted\\\" name\",\"key\":\"k\",\"character\":0,\"cpu\":false,\"difficulty\":1,\"team\":0}", "null"), &m, NULL) != NULL);
    assert(netplay_match_from_lobby(edited("\"character\":3", "\"character\":8"), &m, NULL) != NULL);
    assert(netplay_match_from_lobby(edited("\"turns\":25", "\"turns\":51"), &m, NULL) != NULL);
    assert(netplay_match_from_lobby(edited("\"seed\":18364758544493064720", "\"seed\":18446744073709551616"), &m, NULL) != NULL);
    assert(netplay_match_from_lobby(edited("\"seed\":18364758544493064720", "\"seed\":1.5"), &m, NULL) != NULL);
    assert(netplay_match_from_lobby(edited("\"team\":1}", "\"team\":0}"), &m, NULL) != NULL); /* 3 vs 1 */
    assert(netplay_match_from_lobby(edited("\"gen\":9}", "\"gen\":9"), &m, NULL) != NULL);
    assert(netplay_match_from_lobby(edited("\"gen\":9}", "\"gen\":9}x"), &m, NULL) != NULL);
    assert(netplay_match_from_lobby("{\"state\":\"none\"}", &m, NULL) != NULL);
    assert(netplay_match_from_lobby(NULL, &m, NULL) != NULL);

    /* The poll: one start per started lobby, none before; a bad room is left. */
    lobby_text = fixture;
    lobby_state = PBNET_LOBBY_OPEN;
    netplay_lobby_poll();
    assert(start_calls == 0 && resync_polls == 0);
    lobby_state = PBNET_LOBBY_STARTED;
    netplay_lobby_poll(); netplay_lobby_poll();
    assert(start_calls == 1 && started.seed == 0xFEDCBA9876543210ULL && leave_calls == 0);
    assert(resync_polls == 2); /* N46: every loop, also once the start is handled */
    lobby_state = PBNET_LOBBY_NONE;
    netplay_lobby_poll();
    lobby_state = PBNET_LOBBY_STARTED;
    netplay_lobby_poll();
    assert(start_calls == 2);
    lobby_state = PBNET_LOBBY_CLOSED;
    netplay_lobby_poll();
    lobby_text = edited("\"started\":true", "\"started\":false");
    lobby_state = PBNET_LOBBY_STARTED;
    netplay_lobby_poll();
    assert(start_calls == 2 && leave_calls == 1 && lobby_state == PBNET_LOBBY_NONE);

    /* N10: every start releases its epoch's lockstep; a new epoch (a reload) starts the room again, from its
     * checkpoint when it has one; a reload's room without a seat for this machine is waited out, never left. */
    netplay_lobby_poll(); /* the left lobby (NONE) */
    lobby_text = fixture;
    lobby_state = PBNET_LOBBY_STARTED;
    netplay_lobby_poll();
    assert(start_calls == 3 && go_calls == 3 && went == 0); /* the two starts above released theirs too */
    netplay_lobby_poll();
    assert(start_calls == 3 && go_calls == 3);
    epoch_now = 1;
    blob_len = 0x19C;
    netplay_lobby_poll();
    assert(start_calls == 3 && continue_calls == 1 && continued_size == 0x19C && go_calls == 4 && went == 1);
    netplay_lobby_poll();
    assert(continue_calls == 1 && go_calls == 4);
    epoch_now = 2;
    lobby_text = edited("\"seats\":[0]", "\"seats\":[]");
    netplay_lobby_poll(); netplay_lobby_poll();
    assert(continue_calls == 1 && go_calls == 4 && leave_calls == 1 && lobby_state == PBNET_LOBBY_STARTED);
    epoch_now = 3;
    lobby_text = fixture;
    netplay_lobby_poll();
    assert(continue_calls == 2 && go_calls == 5 && went == 3);
    blob_len = 0;
    epoch_now = 0;
}

/* N34: a started lobby that is no longer started while its match runs (this peer left, the host saved and quit, the
 * lobby closed): the lockstep stops, the session ends and the match is left (back to modeseldll), once. A lobby whose
 * match is not running (over, refused) is not. */
static void match_left_checks(void)
{
    static const int gone[] = { PBNET_LOBBY_NONE, PBNET_LOBBY_CLOSED, PBNET_LOBBY_OPEN };
    int i, polls;

    lobby_text = fixture;
    for (i = 0; i < 3; i++) {
        lobby_state = PBNET_LOBBY_STARTED;
        netplay_lobby_poll();
        netplay_begin(9);
        on = 1;
        running = 1;
        stop_calls = leaves = match_ends = still_drops = forget_calls = 0;
        netplay_lobby_poll();
        assert(stop_calls == 0 && leaves == 0 && netplay_active());
        lobby_state = gone[i];
        polls = leave_polls;
        netplay_lobby_poll();
        assert(leave_polls == polls + 1);
        assert(stop_calls == 1 && leaves == 1 && match_ends == 1 && still_drops == 1);
        assert(forget_calls == 0); /* left mid-way: the guest may still rejoin */
        assert(!netplay_active() && !netplay_lockstep());
        netplay_lobby_poll();
        assert(stop_calls == 1 && leaves == 1);
    }
    /* The match ended (N25: running is 0 by then) or was never started: nothing to leave. */
    lobby_state = PBNET_LOBBY_STARTED;
    netplay_lobby_poll();
    lobby_state = PBNET_LOBBY_OPEN;
    netplay_lobby_poll();
    assert(stop_calls == 1 && leaves == 1);
    lobby_state = PBNET_LOBBY_NONE;
}

/* N50: --turbo skips the frame limiter only for a --script run, and never under lockstep. */
static void turbo_checks(void)
{
    char path[] = "/tmp/pb-turbo-XXXXXX";
    char *bare[] = {"pb", "--turbo"};
    char *noflag[] = {"pb", "--script", path};
    char *both[] = {"pb", "--turbo", "--script", path};
    char *scriptarg[] = {"pb", "--script", "--turbo"};
    int fd = mkstemp(path);
    assert(fd >= 0);
    assert(write(fd, "end 10\n", 7) == 7);
    close(fd);
    assert(!netplay_debug_turbo_arg(2, bare));
    assert(!netplay_debug_turbo_arg(3, noflag));
    assert(netplay_debug_turbo_arg(4, both));
    assert(!netplay_debug_turbo_arg(3, scriptarg)); /* "--turbo" is the script's path there */
    on = 0;
    assert(netplay_debug_init(2, bare) && !netplay_debug_turbo());
    assert(netplay_debug_init(4, both) && netplay_debug_turbo());
    on = 1;
    assert(!netplay_debug_turbo());
    on = 0;
    unlink(path);
}

int main(void)
{
    PADStatus got[4];
    /* No lockstep: gate open, PADRead passes through to the controllers. */
    phys[0].button = PAD_BUTTON_B;
    assert(netplay_tick_begin());
    assert(netplay_pad_read(got) == 0 && got[0].button == PAD_BUTTON_B);

    omcurovl = DLL_w01dll;
    netplay_begin(7);
    on = 1;
    /* Before the first READY: idle, connected. */
    netplay_pad_read(got);
    assert(got[0].button == 0 && got[0].err == PAD_ERR_NONE);
    /* Frame 0 ready, hash for frame 0 sent once. */
    phys[0].button = PAD_BUTTON_X;
    phys[1].err = PAD_ERR_NO_CONTROLLER; phys[1].button = 0x55;
    assert(netplay_tick_begin());
    assert(hash_calls == 1 && hashed_frame == 0);
    assert(taken[0].button == PAD_BUTTON_X && taken[1].err == PAD_ERR_NONE && taken[1].button == 0);
    netplay_pad_read(got);
    assert(got[0].button == PAD_BUTTON_X && got[2].button == 0x1234);
    /* Frame 1: first call takes pads (no A), then waits; A tapped only during the wait. */
    phys[0].button = 0; wait_now = 1;
    assert(!netplay_tick_begin());
    phys[0].button = PAD_BUTTON_A;
    assert(!netplay_tick_begin());
    phys[0].button = 0;
    assert(!netplay_tick_begin());
    netplay_pad_read(got); /* still frame 0's pads */
    assert(got[0].button == PAD_BUTTON_X);
    wait_now = 0;
    assert(netplay_tick_begin()); /* frame 1 runs */
    /* Frame 2's pads carry the latched A. */
    assert(netplay_tick_begin());
    assert(taken[0].button == PAD_BUTTON_A);
    /* Frame 3: no latch left. */
    assert(netplay_tick_begin());
    assert(taken[0].button == 0);
    /* Hash once per 30th frame despite waits. */
    while (fr < 30) assert(netplay_tick_begin());
    hash_calls = 0; wait_now = 1;
    netplay_tick_begin(); netplay_tick_begin(); netplay_tick_begin();
    assert(hash_calls == 1 && hashed_frame == 30);
    /* N64: no hash (so no desync) outside gameplay: the party's results, a menu, instDll. Minigames are checked. */
    wait_now = 0;
    omcurovl = DLL_mstory3dll;
    while (fr < 61) assert(netplay_tick_begin());
    assert(hash_calls == 1);
    omcurovl = DLL_m401dll;
    while (fr < 91) assert(netplay_tick_begin());
    assert(hash_calls == 2 && hashed_frame == 90);
    omcurovl = DLL_w01dll;
    wait_now = 1;
    /* N11: every mismatch goes to pbnet_desync_recover (it logs and reloads once per session). */
    desyncs_queued = 2;
    netplay_tick_begin();
    assert(desyncs_queued == 0 && recover_calls == 2 && recovered_frame == 30 && recovered_peer == 1);
    /* Session gone: back to local controllers. */
    on = 0; wait_now = 0; phys[0].button = PAD_BUTTON_Y;
    assert(netplay_tick_begin());
    netplay_pad_read(got);
    assert(got[0].button == PAD_BUTTON_Y);
    hash_checks();
    lobby_checks();
    match_end_checks();
    match_left_checks();
    turbo_checks();
    puts("harness: all checks passed");
    return 0;
}
