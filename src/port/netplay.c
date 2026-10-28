/*
 * Netplay, C side: deterministic time and seeds, the state hash, and the --script/--hash-log debug
 * mode used to check that two runs of the same input stay identical. See plan/NET.md.
 */

#include "dolphin.h"
#include "game/gamework_data.h"
#include "game/object.h"
#include "game/pad.h"
#include "port/main.h"
#include "port/netplay.h"
#include "port/pbnet.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern s32 rnd_seed;
extern u32 boardRandSeed;

/* Clock the game sees during a session: a fixed epoch shifted by the seed, plus 1/60 s per frame. */
#define NETPLAY_TICKS_PER_FRAME (OS_TIMER_CLOCK / 60)
/* Each call within a frame advances 25 us, so busy-wait timeouts (audio.c SNDGRP_WAIT) still end. */
#define NETPLAY_TICKS_PER_CALL OSMicrosecondsToTicks(25)
/* 2026-01-01 00:00:00, in ticks since the GameCube epoch (2000-01-01). */
#define NETPLAY_EPOCH_SECONDS (9497LL * 86400)
#define NETPLAY_SEED_SPREAD_SECONDS (365LL * 86400)

static BOOL active;
static u32 session_seed;
static u32 frame;
static OSTime time_base;
static OSTime time_last;

/* Lockstep (netplay_tick_begin): pads of the frame the last READY pbnet_step returned, which PADRead hands
 * the game during that tick; the last frame hashed; the frame whose local pads pbnet took; buttons pressed
 * while it did not take pads (sent with the next pads it takes, so a tap during a WAIT is not lost). */
static PADStatus step_pads[4];
static BOOL step_pads_set;
static BOOL step_hash_set;
static u32 step_hash_frame;
static BOOL step_taken_set;
static u32 step_taken_frame;
static u16 step_latched[4];

static u32 mix32(u32 x)
{
    x ^= x >> 16;
    x *= 0x7FEB352DU;
    x ^= x >> 15;
    x *= 0x846CA68BU;
    x ^= x >> 16;
    return x;
}

BOOL netplay_active(void)
{
    return active;
}

u32 netplay_session_seed(void)
{
    return session_seed;
}

u32 netplay_frame(void)
{
    return frame;
}

u32 netplay_seed(u32 salt)
{
    u32 seed = mix32(session_seed ^ mix32(salt ^ mix32(frame)));
    return seed != 0 ? seed : 1;
}

void netplay_begin(u32 seed)
{
    session_seed = seed;
    frame = 0;
    time_base = OSSecondsToTicks(NETPLAY_EPOCH_SECONDS + (s64)(seed % NETPLAY_SEED_SPREAD_SECONDS));
    time_last = 0;
    active = TRUE;
    step_pads_set = FALSE;
    step_hash_set = FALSE;
    step_taken_set = FALSE;
    memset(step_latched, 0, sizeof(step_latched));
    netplay_reseed();
    OSReport("netplay: session start, seed %08X\n", seed);
}

void netplay_reseed(void)
{
    frandSeedSet(netplay_seed(NETPLAY_SALT_FRAND));
    rnd_seed = (s32)netplay_seed(NETPLAY_SALT_RAND8);
    boardRandSeed = netplay_seed(NETPLAY_SALT_BOARD);
}

void netplay_end(void)
{
    active = FALSE;
    netplay_match_end();
    netplay_still_drop();
}

OSTime netplay_time(void)
{
    OSTime t = time_base + (OSTime)frame * NETPLAY_TICKS_PER_FRAME;
    if (t <= time_last) {
        t = time_last + NETPLAY_TICKS_PER_CALL;
    }
    time_last = t;
    return t;
}

typedef struct {
    u64 h;
} Fnv64;

static void fnv_bytes(Fnv64 *f, const void *data, size_t len)
{
    const u8 *p = data;
    size_t i;
    for (i = 0; i < len; i++) {
        f->h ^= p[i];
        f->h *= 0x100000001B3ULL;
    }
}

static void fnv_u32(Fnv64 *f, u32 v)
{
    /* Little-endian bytes, so the hash does not depend on the host. */
    u8 b[4] = { (u8)v, (u8)(v >> 8), (u8)(v >> 16), (u8)(v >> 24) };
    fnv_bytes(f, b, sizeof(b));
}

/* The pads every peer's tick read (HuPadRead's output): the same on every peer by construction. */
static void fnv_pads(Fnv64 *f)
{
    fnv_bytes(f, HuPadBtn, sizeof(HuPadBtn));
    fnv_bytes(f, HuPadBtnDown, sizeof(HuPadBtnDown));
    fnv_bytes(f, HuPadBtnRep, sizeof(HuPadBtnRep));
    fnv_bytes(f, HuPadStkX, sizeof(HuPadStkX));
    fnv_bytes(f, HuPadStkY, sizeof(HuPadStkY));
    fnv_bytes(f, HuPadSubStkX, sizeof(HuPadSubStkX));
    fnv_bytes(f, HuPadSubStkY, sizeof(HuPadSubStkY));
    fnv_bytes(f, HuPadTrigL, sizeof(HuPadTrigL));
    fnv_bytes(f, HuPadTrigR, sizeof(HuPadTrigR));
    fnv_bytes(f, HuPadDStk, sizeof(HuPadDStk));
    fnv_bytes(f, HuPadDStkRep, sizeof(HuPadDStkRep));
    fnv_bytes(f, HuPadErr, sizeof(HuPadErr));
}

/*
 * N64: gameplay state only. The random generators are left out: effects roll them too (UpdateNpcDust's frand in the
 * party results, N42), so a cosmetic roll count would raise a desync. What a roll decides lands in GWSystem/GWPlayer,
 * and the generators are reseeded at every gameplay point (netplay_point_reseed), so extra cosmetic rolls never reach
 * later dice or minigames.
 */
u64 netplay_hash(void)
{
    Fnv64 f = { 0xCBF29CE484222325ULL };
    /* Raw bytes: these structs hold no pointers, and every peer runs the same build. */
    fnv_bytes(&f, &GWSystem, sizeof(GWSystem));
    fnv_bytes(&f, GWPlayer, sizeof(GWPlayer));
    fnv_bytes(&f, GWPlayerCfg, sizeof(GWPlayerCfg));
    fnv_pads(&f);
    return f.h;
}

/* The pre-N64 hash: gameplay state plus the three generators (the --hash-log debug column full=). */
u64 netplay_full_hash(void)
{
    Fnv64 f = { 0xCBF29CE484222325ULL };
    fnv_bytes(&f, &GWSystem, sizeof(GWSystem));
    fnv_bytes(&f, GWPlayer, sizeof(GWPlayer));
    fnv_bytes(&f, GWPlayerCfg, sizeof(GWPlayerCfg));
    fnv_u32(&f, frandSeedGet());
    fnv_u32(&f, (u32)rnd_seed);
    fnv_u32(&f, boardRandSeed);
    fnv_pads(&f);
    return f.h;
}

BOOL netplay_gameplay_ovl(s32 ovl)
{
    return (ovl >= DLL_w01dll && ovl <= DLL_w06dll) || (ovl >= DLL_m300dll && ovl <= DLL_m463dll);
}

void netplay_point_reseed(u32 point, s32 ovl)
{
    u32 key = mix32(point ^ mix32((u32)GWSystem.turn ^ mix32((u32)(GWSystem.player_curr & 0xFF) ^ mix32((u32)ovl))));
    u32 seed;

    seed = mix32(session_seed ^ mix32(NETPLAY_SALT_FRAND ^ key));
    frandSeedSet(seed != 0 ? seed : 1);
    seed = mix32(session_seed ^ mix32(NETPLAY_SALT_RAND8 ^ key));
    rnd_seed = (s32)(seed != 0 ? seed : 1);
    seed = mix32(session_seed ^ mix32(NETPLAY_SALT_BOARD ^ key));
    boardRandSeed = seed != 0 ? seed : 1;
    OSReport("netplay: reseed at frame %u: point %u, turn %d, player %d, overlay %d\n", frame, (unsigned)point,
        (int)GWSystem.turn, (int)GWSystem.player_curr, (int)ovl);
}

static u64 fnv_once(const void *data, size_t len)
{
    Fnv64 f = { 0xCBF29CE484222325ULL };
    fnv_bytes(&f, data, len);
    return f.h;
}

/*
 * One hash-log line: frame, netplay_hash(), then what the hash covers, split up so a diff of two logs
 * shows which state diverged first (tools/netplay/hashdiff.py). ovl is omcurovl, which shows whether a
 * script reached a board or a minigame. Seeds are raw values; the rest are per-section FNV-1a hashes.
 * heap is netplay_heap_hash() (the heaps' layout): not part of the state hash, it shows whether two runs
 * that reach the same state also allocate alike.
 */
static void hash_log_line(FILE *out, u32 at)
{
    Fnv64 pad = { 0xCBF29CE484222325ULL };
    fnv_pads(&pad);
    fprintf(out, "%u %016llX ovl=%d sys=%016llX plr0=%016llX plr1=%016llX plr2=%016llX plr3=%016llX cfg=%016llX "
                 "frand=%08X rand8=%08X brand=%08X pad=%016llX full=%016llX gp=%d heap=%016llX\n",
        at, (unsigned long long)netplay_hash(), (int)omcurovl,
        (unsigned long long)fnv_once(&GWSystem, sizeof(GWSystem)),
        (unsigned long long)fnv_once(&GWPlayer[0], sizeof(GWPlayer[0])),
        (unsigned long long)fnv_once(&GWPlayer[1], sizeof(GWPlayer[1])),
        (unsigned long long)fnv_once(&GWPlayer[2], sizeof(GWPlayer[2])),
        (unsigned long long)fnv_once(&GWPlayer[3], sizeof(GWPlayer[3])),
        (unsigned long long)fnv_once(GWPlayerCfg, sizeof(GWPlayerCfg)), (unsigned)frandSeedGet(), (unsigned)rnd_seed,
        (unsigned)boardRandSeed, (unsigned long long)pad.h, (unsigned long long)netplay_full_hash(),
        netplay_gameplay_ovl(omcurovl) ? 1 : 0, (unsigned long long)netplay_heap_hash());
}

/*
 * Input script (--script). Text, one command per line, '#' starts a comment:
 *
 *   seed <n>                 session seed (default 0x50424E54)
 *   end <frame>              quit after this many frames
 *   <frame> <port> <buttons> [stickX stickY substickX substickY trigL trigR]
 *   ovl <ovl> <event> <port> <buttons>   while overlay <ovl> runs with event <event> (omcurovl,
 *                            omovlevtno), every press on <port> is <buttons> instead of the script's: a
 *                            screen that wants another button than the rest of the run (mstory3's party
 *                            results, event 1, leave on START alone; A turns their pages)
 *   mg-explain off           a --match party without the minigame explanations (instDll then needs no
 *                            START), as modeseldll's option sets it
 *   lottery-stick on         in the board lottery's stick games, a human current player's stick is the
 *                            script's own instead of the pad line's (N79): it spins the ball game's handle
 *                            and sweeps the scratch ticket's hand over the whole ticket
 *
 * <buttons> is a number (0x... allowed) or names joined by '+': A B X Y START Z L R UP DOWN LEFT
 * RIGHT, or 0/none. A pad line sets that port's state from <frame> on; it holds until the next line
 * for the same port. Pad lines must be in frame order. All four ports are connected and idle unless
 * the script says otherwise; real controllers are ignored.
 */

typedef struct {
    u32 frame;
    s32 port;
    PADStatus status;
} ScriptEvent;

static struct {
    BOOL loaded;
    u32 seed;
    u32 end;
    ScriptEvent *events;
    size_t count;
    size_t next;
    PADStatus pads[4];
    /* ovl lines: per port, the overlay (-1: none), its event and the buttons pressed while it runs. */
    s16 alt_ovl[4];
    s16 alt_event[4];
    u16 alt_buttons[4];
    BOOL mg_explain_off;
    BOOL lottery_stick;
    /* lottery_stick: the stick game last seen (BoardLotteryNetplayStickGame) and the frames it has run. */
    s32 lottery_game;
    u32 lottery_t;
} script;

static FILE *hash_log;

/* --match <board>@<frame>: start a debug match after that frame of a --script run. */
static NetplayMatch debug_match;
static u32 debug_match_at;
/* --desync-at: the frame (of a lockstep session) at which this peer flips its frand seed and a coin of player 0 once, to
 * test a resync (N46). The coin is gameplay state (N64: the seeds are not in the lockstep hash). */
static u32 debug_desync_at;
static BOOL debug_desync_done;
/* --leave-at: the frame at which a running match is left as if its lobby had gone (N34). */
static u32 debug_leave_at;
/* --continue: the online save's checkpoint the --match continues from (N9). */
static u8 debug_continue[NETPLAY_BLOB_MAX];
static u32 debug_continue_size;
/* --turbo: a --script run skips the 60 Hz frame limiter (N50); netplay_debug_turbo. */
static BOOL debug_turbo;

static const struct {
    const char *name;
    u16 mask;
} button_names[] = {
    { "A", PAD_BUTTON_A }, { "B", PAD_BUTTON_B }, { "X", PAD_BUTTON_X }, { "Y", PAD_BUTTON_Y },
    { "START", PAD_BUTTON_START }, { "Z", PAD_TRIGGER_Z }, { "L", PAD_TRIGGER_L }, { "R", PAD_TRIGGER_R },
    { "UP", PAD_BUTTON_UP }, { "DOWN", PAD_BUTTON_DOWN }, { "LEFT", PAD_BUTTON_LEFT },
    { "RIGHT", PAD_BUTTON_RIGHT }, { "NONE", 0 },
};

static BOOL parse_buttons(const char *s, u16 *out)
{
    char name[16];
    size_t i, n;
    char *end;
    unsigned long v;

    if (isdigit((unsigned char)s[0])) {
        v = strtoul(s, &end, 0);
        if (*end != '\0' || v > 0xFFFF) {
            return FALSE;
        }
        *out = (u16)v;
        return TRUE;
    }
    *out = 0;
    while (*s != '\0') {
        n = strcspn(s, "+");
        if (n == 0 || n >= sizeof(name)) {
            return FALSE;
        }
        for (i = 0; i < n; i++) {
            name[i] = (char)toupper((unsigned char)s[i]);
        }
        name[n] = '\0';
        for (i = 0; i < sizeof(button_names) / sizeof(button_names[0]); i++) {
            if (strcmp(name, button_names[i].name) == 0) {
                break;
            }
        }
        if (i == sizeof(button_names) / sizeof(button_names[0])) {
            return FALSE;
        }
        *out |= button_names[i].mask;
        s += n;
        if (*s == '+') {
            s++;
        }
    }
    return TRUE;
}

static BOOL parse_long(const char *s, long lo, long hi, long *out)
{
    char *end;
    long v = strtol(s, &end, 0);
    if (*s == '\0' || *end != '\0' || v < lo || v > hi) {
        return FALSE;
    }
    *out = v;
    return TRUE;
}

static BOOL script_parse_line(char *line, const char *path, int lineno)
{
    char *tok[10];
    int n = 0;
    char *p;
    long v[8];
    int i;
    ScriptEvent *ev;

    p = strchr(line, '#');
    if (p != NULL) {
        *p = '\0';
    }
    for (p = strtok(line, " \t\r\n"); p != NULL && n < 10; p = strtok(NULL, " \t\r\n")) {
        tok[n++] = p;
    }
    if (n == 0) {
        return TRUE;
    }
    if (strcmp(tok[0], "seed") == 0 && n == 2) {
        unsigned long long s;
        char *end;
        s = strtoull(tok[1], &end, 0);
        if (*end != '\0' || s > 0xFFFFFFFFULL) {
            goto bad;
        }
        script.seed = (u32)s;
        return TRUE;
    }
    if (strcmp(tok[0], "ovl") == 0 && n == 5) {
        u16 b;
        if (!parse_long(tok[1], 0, 0x7FFF, &v[0]) || !parse_long(tok[2], 0, 0x7FFF, &v[1])
            || !parse_long(tok[3], 0, 3, &v[2]) || !parse_buttons(tok[4], &b)) {
            goto bad;
        }
        script.alt_ovl[v[2]] = (s16)v[0];
        script.alt_event[v[2]] = (s16)v[1];
        script.alt_buttons[v[2]] = b;
        return TRUE;
    }
    if (strcmp(tok[0], "mg-explain") == 0 && n == 2 && strcmp(tok[1], "off") == 0) {
        script.mg_explain_off = TRUE;
        return TRUE;
    }
    if (strcmp(tok[0], "lottery-stick") == 0 && n == 2 && strcmp(tok[1], "on") == 0) {
        script.lottery_stick = TRUE;
        return TRUE;
    }
    if (strcmp(tok[0], "end") == 0 && n == 2) {
        if (!parse_long(tok[1], 1, 0x7FFFFFFF, &v[0])) {
            goto bad;
        }
        script.end = (u32)v[0];
        return TRUE;
    }
    if (n != 3 && n != 9) {
        goto bad;
    }
    if (!parse_long(tok[0], 0, 0x7FFFFFFF, &v[0]) || !parse_long(tok[1], 0, 3, &v[1])) {
        goto bad;
    }
    if (script.count > 0 && script.events[script.count - 1].frame > (u32)v[0]) {
        OSReport("netplay: %s:%d: pad lines must be in frame order\n", path, lineno);
        return FALSE;
    }
    ev = realloc(script.events, (script.count + 1) * sizeof(*ev));
    if (ev == NULL) {
        return FALSE;
    }
    script.events = ev;
    ev = &script.events[script.count];
    memset(ev, 0, sizeof(*ev));
    ev->frame = (u32)v[0];
    ev->port = (s32)v[1];
    ev->status.err = PAD_ERR_NONE;
    if (!parse_buttons(tok[2], &ev->status.button)) {
        goto bad;
    }
    if (n == 9) {
        for (i = 0; i < 4; i++) {
            if (!parse_long(tok[3 + i], -128, 127, &v[2 + i])) {
                goto bad;
            }
        }
        for (i = 0; i < 2; i++) {
            if (!parse_long(tok[7 + i], 0, 255, &v[6 + i])) {
                goto bad;
            }
        }
        ev->status.stickX = (s8)v[2];
        ev->status.stickY = (s8)v[3];
        ev->status.substickX = (s8)v[4];
        ev->status.substickY = (s8)v[5];
        ev->status.triggerLeft = (u8)v[6];
        ev->status.triggerRight = (u8)v[7];
    }
    script.count++;
    return TRUE;

bad:
    OSReport("netplay: %s:%d: cannot parse script line\n", path, lineno);
    return FALSE;
}

static BOOL script_load(const char *path)
{
    char line[256];
    int lineno = 0;
    int i;
    FILE *f = fopen(path, "r");
    if (f == NULL) {
        OSReport("netplay: cannot open script %s\n", path);
        return FALSE;
    }
    free(script.events);
    memset(&script, 0, sizeof(script));
    script.seed = 0x50424E54; /* "PBNT" */
    for (i = 0; i < 4; i++) {
        script.alt_ovl[i] = -1;
    }
    while (fgets(line, sizeof(line), f) != NULL) {
        lineno++;
        if (!script_parse_line(line, path, lineno)) {
            fclose(f);
            free(script.events);
            memset(&script, 0, sizeof(script));
            return FALSE;
        }
    }
    fclose(f);
    script.loaded = TRUE;
    OSReport("netplay: script %s: %u pad lines, seed %08X, end %u\n", path, (unsigned)script.count, script.seed,
        script.end);
    return TRUE;
}

s32 BoardLotteryNetplayStickGame(void);

/* `lottery-stick on`: the stick for frame t of a lottery stick game (1 ball, 2 scratch). Values stay under 127 (the
 * ball game ends at once on a COM's 0x7F) and over PADClamp's dead zone. */
static void lottery_stick_at(s32 game, u32 t, s8 *x, s8 *y)
{
    /* The ball game: 20 degrees a frame clockwise (its angle is atan2d(x, y), and only rising steps under 30 count). */
    static const s8 spin[18][2] = { { 0, 120 }, { 41, 113 }, { 77, 92 }, { 104, 60 }, { 118, 21 }, { 118, -21 },
        { 104, -60 }, { 77, -92 }, { 41, -113 }, { 0, -120 }, { -41, -113 }, { -77, -92 }, { -104, -60 },
        { -118, -21 }, { -118, 21 }, { -104, 60 }, { -77, 92 }, { -41, 113 } };
    /* The scratch (TicketUpdate): the hand moves 8 px a frame in the stick's direction inside x 192..384, y 192..288
     * (screen y down, stick y up), and starts at (320, 240) after a 60-frame descent. Wait out the descent, go to the
     * top left corner, then rows: across (26 frames > 192 px), down one frame (8 px), back. 13 rows cover the 96 px,
     * then it starts over at the corner until the ticket counts as scratched. */
    enum { WAIT = 64, LEFT = 30, UP = 15, ACROSS = 26, ROWS = 13 };
    u32 c;

    *x = *y = 0;
    if (game == 1) {
        *x = spin[t % 18][0];
        *y = spin[t % 18][1];
        return;
    }
    if (t < WAIT) {
        return;
    }
    c = (t - WAIT) % (LEFT + UP + ROWS * (ACROSS + 1));
    if (c < LEFT) {
        *x = -120;
    } else if (c < LEFT + UP) {
        *y = 120;
    } else {
        c -= LEFT + UP;
        if (c % (ACROSS + 1) == ACROSS) {
            *y = -120;
        } else {
            *x = (c / (ACROSS + 1)) % 2 == 0 ? 120 : -120;
        }
    }
}

/* Pads for the current frame: every script line up to and including this frame applied. */
static void script_pads(PADStatus *status)
{
    s32 i;

    while (script.next < script.count && script.events[script.next].frame <= frame) {
        const ScriptEvent *ev = &script.events[script.next++];
        script.pads[ev->port] = ev->status;
    }
    memcpy(status, script.pads, sizeof(script.pads));
    for (i = 0; i < 4; i++) {
        if (script.alt_ovl[i] >= 0 && omcurovl == script.alt_ovl[i] && omovlevtno == script.alt_event[i]
            && status[i].button != 0) {
            status[i].button = script.alt_buttons[i];
        }
    }
    if (script.lottery_stick) {
        s32 game = BoardLotteryNetplayStickGame();
        s32 cur = GWSystem.player_curr;
        if (game != script.lottery_game) {
            if (game != 0) {
                OSReport("netplay: script: lottery %s at frame %u (player %d)\n", game == 1 ? "ball game" : "scratch",
                    (unsigned)frame, (int)cur);
            }
            script.lottery_game = game;
            script.lottery_t = 0;
        }
        if (game != 0 && cur >= 0 && cur < 4 && !GWPlayer[cur].com && GWPlayer[cur].port >= 0
            && GWPlayer[cur].port < 4) {
            PADStatus *st = &status[GWPlayer[cur].port];
            lottery_stick_at(game, script.lottery_t++, &st->stickX, &st->stickY);
        }
    }
}

BOOL netplay_debug_init(int argc, char *argv[])
{
    const char *script_path = NULL;
    const char *hash_path = NULL;
    int i;

    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--script") == 0 && i + 1 < argc) {
            script_path = argv[++i];
        } else if (strcmp(argv[i], "--hash-log") == 0 && i + 1 < argc) {
            hash_path = argv[++i];
        } else if (strcmp(argv[i], "--match") == 0 && i + 1 < argc) {
            if (!netplay_match_parse_debug(argv[++i], &debug_match, &debug_match_at)) {
                OSReport("netplay: --match wants <board 0..5>@<frame>, e.g. 0@900\n");
                return FALSE;
            }
        } else if (strcmp(argv[i], "--leave-at") == 0 && i + 1 < argc) {
            debug_leave_at = (u32)strtoul(argv[++i], NULL, 0);
        } else if (strcmp(argv[i], "--desync-at") == 0 && i + 1 < argc) {
            debug_desync_at = (u32)strtoul(argv[++i], NULL, 0);
        } else if (strcmp(argv[i], "--continue") == 0 && i + 1 < argc) {
            int n = pbnet_save_blob(argv[++i], debug_continue, (int)sizeof(debug_continue));
            if (n <= 0 || n > (int)sizeof(debug_continue)) {
                OSReport("netplay: --continue: %s is not an online save\n", argv[i]);
                return FALSE;
            }
            debug_continue_size = (u32)n;
        } else if (strcmp(argv[i], "--turbo") == 0) {
            debug_turbo = TRUE;
        }
    }
    if (debug_continue_size != 0 && debug_match_at == 0) {
        OSReport("netplay: --continue needs --match (its frame; the board is the save's)\n");
        return FALSE;
    }
    if (script_path != NULL && !script_load(script_path)) {
        return FALSE;
    }
    if (debug_match_at != 0 && script_path == NULL) {
        OSReport("netplay: --match needs --script\n");
        return FALSE;
    }
    if (debug_turbo && script_path == NULL) {
        OSReport("netplay: --turbo needs --script, ignored\n");
        debug_turbo = FALSE;
    }
    if (hash_path != NULL) {
        hash_log = fopen(hash_path, "w");
        if (hash_log == NULL) {
            OSReport("netplay: cannot write hash log %s\n", hash_path);
            return FALSE;
        }
        setvbuf(hash_log, NULL, _IOLBF, 0);
        if (script_path == NULL) {
            OSReport("netplay: --hash-log without --script: logging an unscripted session\n");
        }
    }
    if (script_path != NULL || hash_path != NULL) {
        netplay_begin(script_path != NULL ? script.seed : 0x50424E54);
    }
    return TRUE;
}

BOOL netplay_debug_scripted(void)
{
    return script.loaded;
}

BOOL netplay_debug_mg_explain_off(void)
{
    return script.loaded && script.mg_explain_off;
}

BOOL netplay_debug_turbo_arg(int argc, char *argv[])
{
    BOOL turbo = FALSE;
    BOOL scripted = FALSE;
    int i;

    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--turbo") == 0) {
            turbo = TRUE;
        } else if (strcmp(argv[i], "--script") == 0 && i + 1 < argc) {
            scripted = TRUE;
            i++;
        }
    }
    return turbo && scripted;
}

BOOL netplay_debug_turbo(void)
{
    return debug_turbo && script.loaded && !netplay_lockstep();
}

/* An online match ended and its lobby is back (or closed meanwhile): the UI shows it (netplay_lobby_returned). */
static BOOL lobby_returned;

/*
 * The match is over (netplay_match_over), at the end of the frame that saw it: the same frame on every peer. The
 * lockstep stops (pbnet drains what the others still need), the session ends (netplay_end gives the player's
 * GWGameStat back) and the lobby goes back to open for the next match; the menu that started runs on this machine's
 * own pads from the next frame.
 */
static void match_finish(void)
{
    BOOL lockstep = pbnet_lockstep_active() != 0;

    if (script.loaded) {
        /* A --script run keeps its session: its frames, script lines and hash log go on. */
        OSReport("netplay: match over at frame %u (scripted run, the session goes on)\n", frame);
        netplay_match_end();
        return;
    }
    OSReport("netplay: match over at frame %u, hash %016llX: session ends\n", frame, (unsigned long long)netplay_hash());
    pbnet_lockstep_finish();
    netplay_end();
    /* N39: a guest never offers to rejoin a finished match (also when its lobby closed meanwhile). */
    pbnet_rejoin_forget();
    if (lockstep && pbnet_lobby_state() != PBNET_LOBBY_NONE) {
        pbnet_lobby_back();
        lobby_returned = TRUE;
    }
}

/*
 * N34: the started lobby is gone while its match runs (this peer left, the host saved and quit, the connection
 * closed). No frame is common to the peers any more: each stops its lockstep, ends its session (netplay_end gives the
 * player's GWGameStat back) and goes back to modeseldll on its own. A --script run keeps its session.
 */
static void match_left(const char *why)
{
    if (script.loaded) {
        OSReport("netplay: match left at frame %u (%s; scripted run, the session goes on)\n", frame, why);
        netplay_match_end();
    } else {
        OSReport("netplay: match left at frame %u (%s): session ends\n", frame, why);
        pbnet_lockstep_stop();
        netplay_end();
    }
    netplay_match_leave();
}

BOOL netplay_lobby_returned(void)
{
    BOOL r;
    /* N64: the lobby shows once this machine's party results are over, not on top of them. */
    if (netplay_match_results()) {
        return FALSE;
    }
    r = lobby_returned;
    lobby_returned = FALSE;
    return r;
}

void netplay_frame_end(void)
{
    if (hash_log != NULL) {
        hash_log_line(hash_log, frame);
    }
    frame++;
    if (debug_desync_at != 0 && !debug_desync_done && frame == debug_desync_at && pbnet_lockstep_active()) {
        /* Once per process: a resync restarts the frames, and the same flip again would desync forever. */
        debug_desync_done = TRUE;
        frandSeedSet(frandSeedGet() ^ 1);
        GWPlayer[0].coins ^= 1;
        OSReport("netplay: --desync-at %u: frand seed and player 0's coins flipped on this peer\n", frame);
    }
    if (netplay_match_over()) {
        match_finish();
    }
    if (debug_leave_at != 0 && frame == debug_leave_at && netplay_match_running()) {
        match_left("--leave-at");
    }
    if (debug_match_at != 0 && frame == debug_match_at && debug_continue_size != 0) {
        netplay_match_continue(&debug_match, debug_continue, debug_continue_size);
    } else if (debug_match_at != 0 && frame == debug_match_at) {
        netplay_match_start(&debug_match);
    }
    if (script.loaded && script.end != 0 && frame >= script.end) {
        OSReport("netplay: script ended at frame %u, hash %016llX\n", frame, (unsigned long long)netplay_hash());
        if (hash_log != NULL) {
            fclose(hash_log);
            hash_log = NULL;
        }
        PartyBoard_IsRunning = FALSE;
    }
}

#ifdef PB_NETPLAY_WRAP
#ifdef PB_NETPLAY_DEFINE
/* PADRead is renamed to __wrap_PADRead in this file too (cmake/netplay.cmake): aurora's keeps its name. */
#undef PADRead
u32 PADRead(PADStatus *status);
#define __real_PADRead PADRead
#else
u32 __real_PADRead(PADStatus *status);
#endif
#endif

/* This machine's controllers, bypassing the PADRead wrap. */
static u32 local_pad_read(PADStatus *status)
{
#ifdef PB_NETPLAY_WRAP
    return __real_PADRead(status);
#else
    return PADRead(status);
#endif
}

BOOL netplay_lockstep(void)
{
    return pbnet_lockstep_active() != 0;
}

BOOL netplay_tick_begin(void)
{
    PADStatus local[4];
    u32 next;
    u32 desync_frame;
    int peer;
    u64 ours, theirs;
    int i;

    /* A started match sets its state first: frame 0's hash and pads see it on every peer. */
    netplay_match_tick();
    if (script.loaded || !pbnet_lockstep_active()) {
        return TRUE;
    }
    /*
     * N56: the lockstep's frame 0 is the match's frame 0 on every peer. A debug session (--hash-log) has netplay on
     * in the menus too: it stepped a started lobby's lockstep there until netplay_lobby_poll applied the start, so
     * its match's frame 0 was pbnet's frame 1 or 2 (a race with the lobby thread), not the other peers'. The board's
     * intro hashes alike a frame apart, but a message that got ready a frame later missed an A press there (BoardRand
     * diverged at the turn-order dice, frame ~1080 on w01).
     */
    if (!netplay_match_running()) {
        return TRUE;
    }
    next = pbnet_frame();
    /* Once per frame number: a WAIT repeats the call for the same frame with the same state. */
    /* N64: only gameplay is checked. Menus, instDll and the results may run a frame apart on two peers (an effect's
     * timing), and raise no desync. */
    if (next % 30 == 0 && (!step_hash_set || step_hash_frame != next) && netplay_gameplay_ovl(omcurovl)) {
        pbnet_set_hash(next, netplay_hash());
        step_hash_frame = next;
        step_hash_set = TRUE;
    }
    while (pbnet_poll_desync(&desync_frame, &peer, &ours, &theirs)) {
        OSReport("netplay: desync at frame %u with peer %d: ours %016llX, theirs %016llX\n", desync_frame, peer,
            (unsigned long long)ours, (unsigned long long)theirs);
        /* N11: the input log goes to desync-*.json; the host has every peer reload the last checkpoint. */
        if (pbnet_desync_recover(desync_frame, peer, ours, theirs)) {
            OSReport("netplay: reloading the last checkpoint\n");
        }
    }
    /*
     * Local controller i plays this machine's i-th seat (pbnet ignores pads beyond its seats). A seat is
     * connected for the whole match, so a local pad error is sent as an idle, connected pad.
     */
    local_pad_read(local);
    for (i = 0; i < 4; i++) {
        local[i].extButton = 0;
        if (local[i].err != PAD_ERR_NONE) {
            memset(&local[i], 0, sizeof(local[i]));
            local[i].err = PAD_ERR_NONE;
        }
    }
    /* pbnet takes the local pads at the first call for a frame only. */
    if (!step_taken_set || step_taken_frame != next) {
        for (i = 0; i < 4; i++) {
            local[i].button |= step_latched[i];
            step_latched[i] = 0;
        }
        step_taken_frame = next;
        step_taken_set = TRUE;
    } else {
        for (i = 0; i < 4; i++) {
            step_latched[i] |= local[i].button;
        }
    }
    switch (pbnet_step(local, 4, step_pads)) {
        case PBNET_READY:
            step_pads_set = TRUE;
            return TRUE;
        case PBNET_WAIT:
            return FALSE;
        default:
            /* The session ended between the two calls: run on local pads. */
            return TRUE;
    }
}

u32 netplay_pad_read(PADStatus *status)
{
    if (script.loaded) {
        script_pads(status);
        return 0;
    }
    if (pbnet_lockstep_active()) {
        if (step_pads_set) {
            memcpy(status, step_pads, sizeof(step_pads));
        } else {
            /* Before the first READY frame (boot-time PADRead calls): idle, connected pads. */
            memset(status, 0, 4 * sizeof(*status));
        }
        return 0;
    }
    return local_pad_read(status);
}

/*
 * Lobby to match. The lobby's JSON (serde_json from net/src/lobby_c.rs) is read with a small reader that knows
 * only what that JSON holds: objects, arrays, strings, unsigned integers, true/false/null. The seed is a full
 * u64, which a double-based JSON reader would round.
 */

typedef struct {
    const char *p;
    BOOL ok;
} JsonIn;

#define JSON_DEPTH_MAX 16

static void json_ws(JsonIn *j)
{
    while (*j->p == ' ' || *j->p == '\t' || *j->p == '\n' || *j->p == '\r') {
        j->p++;
    }
}

/* Eats `c` (after white space) if it is next. */
static BOOL json_eat(JsonIn *j, char c)
{
    json_ws(j);
    if (*j->p == c) {
        j->p++;
        return TRUE;
    }
    return FALSE;
}

static BOOL json_fail(JsonIn *j)
{
    j->ok = FALSE;
    return FALSE;
}

/* A string into out (cut to cap-1 bytes; escapes other than \" and \\ kept as the escaped char or '?'). */
static BOOL json_string(JsonIn *j, char *out, size_t cap)
{
    size_t n = 0;
    char c;

    if (!json_eat(j, '"')) {
        return json_fail(j);
    }
    while ((c = *j->p) != '"') {
        if (c == '\0') {
            return json_fail(j);
        }
        j->p++;
        if (c == '\\') {
            c = *j->p;
            if (c == '\0') {
                return json_fail(j);
            }
            j->p++;
            if (c == 'u') {
                if (strspn(j->p, "0123456789abcdefABCDEF") < 4) {
                    return json_fail(j);
                }
                j->p += 4;
                c = '?';
            }
        }
        if (n + 1 < cap) {
            out[n++] = c;
        }
    }
    j->p++;
    if (cap > 0) {
        out[n] = '\0';
    }
    return TRUE;
}

static BOOL json_u64(JsonIn *j, u64 *out)
{
    u64 v = 0;
    json_ws(j);
    if (!isdigit((unsigned char)*j->p)) {
        return json_fail(j);
    }
    while (isdigit((unsigned char)*j->p)) {
        u32 d = (u32)(*j->p++ - '0');
        if (v > (0xFFFFFFFFFFFFFFFFULL - d) / 10) {
            return json_fail(j);
        }
        v = v * 10 + d;
    }
    if (*j->p == '.' || *j->p == 'e' || *j->p == 'E') {
        return json_fail(j);
    }
    *out = v;
    return TRUE;
}

/* An integer in lo..hi. */
static BOOL json_u8(JsonIn *j, u8 lo, u8 hi, u8 *out)
{
    u64 v;
    if (!json_u64(j, &v) || v < lo || v > hi) {
        return json_fail(j);
    }
    *out = (u8)v;
    return TRUE;
}

static BOOL json_word(JsonIn *j, const char *word)
{
    size_t n = strlen(word);
    json_ws(j);
    if (strncmp(j->p, word, n) != 0) {
        return FALSE;
    }
    j->p += n;
    return TRUE;
}

static BOOL json_bool(JsonIn *j, u8 *out)
{
    if (json_word(j, "true")) {
        *out = 1;
    } else if (json_word(j, "false")) {
        *out = 0;
    } else {
        return json_fail(j);
    }
    return TRUE;
}

static BOOL json_skip(JsonIn *j, int depth)
{
    char key[2];
    json_ws(j);
    if (depth > JSON_DEPTH_MAX) {
        return json_fail(j);
    }
    switch (*j->p) {
        case '"':
            return json_string(j, key, sizeof(key));
        case '{':
            j->p++;
            if (json_eat(j, '}')) {
                return TRUE;
            }
            do {
                if (!json_string(j, key, sizeof(key)) || !json_eat(j, ':') || !json_skip(j, depth + 1)) {
                    return json_fail(j);
                }
            } while (json_eat(j, ','));
            return json_eat(j, '}') || json_fail(j);
        case '[':
            j->p++;
            if (json_eat(j, ']')) {
                return TRUE;
            }
            do {
                if (!json_skip(j, depth + 1)) {
                    return FALSE;
                }
            } while (json_eat(j, ','));
            return json_eat(j, ']') || json_fail(j);
        case '-':
            j->p++;
            /* fall through */
        default:
            if (isdigit((unsigned char)*j->p)) {
                while (isdigit((unsigned char)*j->p) || strchr(".eE+-", *j->p) != NULL) {
                    j->p++;
                }
                return TRUE;
            }
            if (json_word(j, "true") || json_word(j, "false") || json_word(j, "null")) {
                return TRUE;
            }
            return json_fail(j);
    }
}

/*
 * Walks an object: for each member, `member(j, key, user)` reads its value and returns TRUE, or returns FALSE to
 * have it skipped. FALSE if the object is malformed or a member's reader failed (j->ok).
 */
static BOOL json_object(JsonIn *j, BOOL (*member)(JsonIn *j, const char *key, void *user), void *user)
{
    char key[32];
    if (!json_eat(j, '{')) {
        return json_fail(j);
    }
    if (json_eat(j, '}')) {
        return TRUE;
    }
    do {
        if (!json_string(j, key, sizeof(key)) || !json_eat(j, ':')) {
            return json_fail(j);
        }
        if (!member(j, key, user) && (!j->ok || !json_skip(j, 1))) {
            return json_fail(j);
        }
        if (!j->ok) {
            return FALSE;
        }
    } while (json_eat(j, ','));
    return json_eat(j, '}') || json_fail(j);
}

/* Fields a started room must have, as bits of LobbyRoom.seen. */
enum {
    ROOM_BOARD = 1 << 0,
    ROOM_TURNS = 1 << 1,
    ROOM_BONUS = 1 << 2,
    ROOM_TEAMS = 1 << 3,
    ROOM_HANDICAPS = 1 << 4,
    ROOM_SEED = 1 << 5,
    ROOM_SEATS = 1 << 6,
    ROOM_STARTED = 1 << 7,
    ROOM_ALL = (1 << 8) - 1,
};

enum {
    SEAT_CHARACTER = 1 << 0,
    SEAT_CPU = 1 << 1,
    SEAT_DIFFICULTY = 1 << 2,
    SEAT_TEAM = 1 << 3,
    SEAT_ALL = (1 << 4) - 1,
};

typedef struct {
    NetplayMatch *m;
    u32 seen;
    u8 started;
    const char *why;
} LobbyRoom;

typedef struct {
    NetplaySeat *seat;
    u32 seen;
} LobbySeat;

typedef struct {
    LobbyRoom room;
    BOOL has_room;
    char state[16];
    u8 local_seats;
} LobbyTop;

static BOOL lobby_seat_member(JsonIn *j, const char *key, void *user)
{
    LobbySeat *s = user;
    if (strcmp(key, "character") == 0) {
        json_u8(j, 0, 7, &s->seat->character);
        s->seen |= SEAT_CHARACTER;
    } else if (strcmp(key, "cpu") == 0) {
        json_bool(j, &s->seat->cpu);
        s->seen |= SEAT_CPU;
    } else if (strcmp(key, "difficulty") == 0) {
        json_u8(j, 0, 3, &s->seat->difficulty);
        s->seen |= SEAT_DIFFICULTY;
    } else if (strcmp(key, "team") == 0) {
        json_u8(j, 0, 1, &s->seat->team);
        s->seen |= SEAT_TEAM;
    } else {
        return FALSE;
    }
    return TRUE;
}

static BOOL lobby_room_member(JsonIn *j, const char *key, void *user)
{
    LobbyRoom *r = user;
    NetplayMatch *m = r->m;
    s32 i;

    if (strcmp(key, "board") == 0) {
        json_u8(j, 0, NETPLAY_BOARDS - 1, &m->board);
        r->seen |= ROOM_BOARD;
    } else if (strcmp(key, "turns") == 0) {
        json_u8(j, 10, 50, &m->turns);
        r->seen |= ROOM_TURNS;
    } else if (strcmp(key, "bonus") == 0) {
        json_bool(j, &m->bonus);
        r->seen |= ROOM_BONUS;
    } else if (strcmp(key, "teams") == 0) {
        json_bool(j, &m->teams);
        r->seen |= ROOM_TEAMS;
    } else if (strcmp(key, "seed") == 0) {
        json_u64(j, &m->seed);
        r->seen |= ROOM_SEED;
    } else if (strcmp(key, "started") == 0) {
        json_bool(j, &r->started);
        r->seen |= ROOM_STARTED;
    } else if (strcmp(key, "handicaps") == 0) {
        if (!json_eat(j, '[')) {
            return json_fail(j);
        }
        for (i = 0; i < 4; i++) {
            if ((i > 0 && !json_eat(j, ',')) || !json_u8(j, 0, 9, &m->seats[i].handicap)) {
                return json_fail(j);
            }
        }
        if (!json_eat(j, ']')) {
            return json_fail(j);
        }
        r->seen |= ROOM_HANDICAPS;
    } else if (strcmp(key, "seats") == 0) {
        if (!json_eat(j, '[')) {
            return json_fail(j);
        }
        for (i = 0; i < 4; i++) {
            LobbySeat s;
            NetplaySeat seat;
            if (i > 0 && !json_eat(j, ',')) {
                return json_fail(j);
            }
            if (json_word(j, "null")) {
                /* Open seats become CPUs at the start: a started room has none. */
                r->why = "the room has an open seat";
                continue;
            }
            s.seat = &seat;
            s.seen = 0;
            if (!json_object(j, lobby_seat_member, &s)) {
                return FALSE;
            }
            if (s.seen != SEAT_ALL) {
                r->why = "a seat is missing a field";
                continue;
            }
            /* The handicap comes from the room's handicaps (either order). */
            seat.handicap = m->seats[i].handicap;
            m->seats[i] = seat;
        }
        if (!json_eat(j, ']')) {
            return json_fail(j);
        }
        r->seen |= ROOM_SEATS;
    } else {
        return FALSE;
    }
    return TRUE;
}

static BOOL lobby_top_member(JsonIn *j, const char *key, void *user)
{
    LobbyTop *t = user;
    u8 seat;

    if (strcmp(key, "state") == 0) {
        json_string(j, t->state, sizeof(t->state));
    } else if (strcmp(key, "seats") == 0) {
        if (!json_eat(j, '[')) {
            return json_fail(j);
        }
        if (json_eat(j, ']')) {
            return TRUE;
        }
        do {
            if (!json_u8(j, 0, 3, &seat)) {
                return FALSE;
            }
            t->local_seats |= (u8)(1u << seat);
        } while (json_eat(j, ','));
        if (!json_eat(j, ']')) {
            return json_fail(j);
        }
    } else if (strcmp(key, "room") == 0) {
        if (json_word(j, "null")) {
            return TRUE;
        }
        t->has_room = TRUE;
        json_object(j, lobby_room_member, &t->room);
    } else {
        return FALSE;
    }
    return TRUE;
}

const char *netplay_match_from_lobby(const char *json, NetplayMatch *m, u8 *local_seats)
{
    JsonIn j;
    LobbyTop t;
    NetplayMatch out;

    memset(&out, 0, sizeof(out));
    memset(&t, 0, sizeof(t));
    t.room.m = &out;
    j.p = json;
    j.ok = TRUE;
    if (json == NULL || !json_object(&j, lobby_top_member, &t) || !j.ok) {
        return "the lobby's JSON cannot be read";
    }
    json_ws(&j);
    if (*j.p != '\0') {
        return "the lobby's JSON cannot be read";
    }
    if (strcmp(t.state, "started") != 0) {
        return "the lobby has not started";
    }
    if (!t.has_room || t.room.seen != ROOM_ALL) {
        return "the room is missing a field";
    }
    if (!t.room.started) {
        return "the room has not started";
    }
    if (t.room.why != NULL) {
        return t.room.why;
    }
    if (t.local_seats == 0) {
        return "this machine has no seat";
    }
    {
        const char *why = netplay_match_invalid(&out);
        if (why != NULL) {
            return why;
        }
    }
    *m = out;
    if (local_seats != NULL) {
        *local_seats = t.local_seats;
    }
    return NULL;
}

/* The started lobby whose match netplay_lobby_poll started (or gave up on); cleared when it is no longer started. */
static BOOL lobby_handled;
/* N10: the epoch of the room it started (pbnet_recover_epoch: one more per reload). */
static u32 lobby_epoch;
static u8 lobby_blob[NETPLAY_BLOB_MAX];

void netplay_lobby_poll(void)
{
    NetplayMatch m;
    const char *why;
    char *json;
    int len, cap = 0;
    int blob_size;
    u8 local = 0;
    u32 epoch;
    int state;
    s32 i;

    netplay_match_leave_poll();
    state = pbnet_lobby_state();
    if (state != PBNET_LOBBY_STARTED) {
        if (lobby_handled && netplay_match_running()) {
            match_left(state == PBNET_LOBBY_CLOSED ? "the lobby closed" : "the lobby was left");
        }
        lobby_handled = FALSE;
        return;
    }
    /* N46: a resync the host held back (the same point desynced again) goes when its wait is over. */
    pbnet_resync_poll();
    /* N10: a reload (a rejoin, a CPU stand-in) is a new epoch of the same started lobby: start its room again. */
    epoch = pbnet_recover_epoch();
    if (lobby_handled && epoch == lobby_epoch) {
        return;
    }
    lobby_handled = TRUE;
    lobby_epoch = epoch;
    len = pbnet_lobby_json(NULL, 0);
    json = NULL;
    /* The JSON may grow between the calls (gen moves): retry with the new length. */
    for (i = 0; i < 4 && len >= 0; i++) {
        char *grown;
        cap = len + 1;
        grown = realloc(json, (size_t)cap);
        if (grown == NULL) {
            break;
        }
        json = grown;
        len = pbnet_lobby_json(json, cap);
        if (len < cap) {
            break;
        }
    }
    why = json != NULL && len >= 0 && len < cap ? netplay_match_from_lobby(json, &m, &local)
                                                 : "the lobby's JSON cannot be read";
    free(json);
    if (why == NULL) {
        OSReport("netplay: lobby started (epoch %u): seed %016llX, this machine plays seats%s%s%s%s\n", (unsigned)epoch,
            (unsigned long long)m.seed, (local & 1) ? " 1P" : "", (local & 2) ? " 2P" : "", (local & 4) ? " 3P" : "",
            (local & 8) ? " 4P" : "");
        /* A continue, or a reload after a checkpoint: the board state is the room's checkpoint (N9). */
        blob_size = pbnet_lobby_checkpoint(lobby_blob, sizeof(lobby_blob));
        if (blob_size > (int)sizeof(lobby_blob)) {
            blob_size = 0;
            why = "the room's checkpoint is too big";
        }
        else if (blob_size > 0 ? netplay_match_continue(&m, lobby_blob, (u32)blob_size) : netplay_match_start(&m)) {
            /* The lockstep of this epoch runs from here (a reload held it until the state is set). */
            pbnet_recover_go(epoch);
            return;
        }
        else if (epoch != 0) {
            /* A reload right after a start: the start is still pending until the next tick applies it. */
            lobby_handled = FALSE;
            return;
        }
        else {
            why = "netplay_match_start refused it";
        }
    }
    if (epoch != 0) {
        /*
         * A reload whose room has no seat for this machine: the computer stands in for it, and the lobby has
         * already asked the host for the seats back (another reload follows). Never leave a running match.
         */
        OSReport("netplay: reload %u: %s; waiting for the host\n", (unsigned)epoch, why);
        return;
    }
    /* Every other peer waits on this one's pads from frame 0: leave so the lobby says why instead. */
    OSReport("netplay: cannot play the started lobby: %s; leaving it\n", why);
    pbnet_lobby_leave();
}

#ifdef PB_NETPLAY_WRAP
/*
 * Linker wraps or preprocessor renames (cmake/netplay.cmake): every OSGetTime/OSGetTick/PADRead call in
 * dol and the RELs lands here. Offline they pass straight through to aurora.
 */
#ifdef PB_NETPLAY_DEFINE
/* The calls were renamed by -DOSGetTime=__wrap_OSGetTime etc.; aurora's own functions keep their names. */
#undef OSGetTime
#undef OSGetTick
#undef PADRead
OSTime OSGetTime(void);
OSTick OSGetTick(void);
u32 PADRead(PADStatus *status);
#define __real_OSGetTime OSGetTime
#define __real_OSGetTick OSGetTick
#define __real_PADRead PADRead
#else
OSTime __real_OSGetTime(void);
OSTick __real_OSGetTick(void);
#endif

/* The RELs import __wrap_OSGetTime/OSGetTick from dol; on Windows dol.def does not list them. */
#ifdef _WIN32
#define NETPLAY_WRAP_EXPORT __declspec(dllexport)
#else
#define NETPLAY_WRAP_EXPORT
#endif

NETPLAY_WRAP_EXPORT OSTime __wrap_OSGetTime(void)
{
    if (active) {
        return netplay_time();
    }
    return __real_OSGetTime();
}

NETPLAY_WRAP_EXPORT OSTick __wrap_OSGetTick(void)
{
    if (active) {
        return (OSTick)(netplay_time() & 0xFFFFFFFF);
    }
    return __real_OSGetTick();
}

NETPLAY_WRAP_EXPORT u32 __wrap_PADRead(PADStatus *status)
{
    if (active) {
        return netplay_pad_read(status);
    }
    return __real_PADRead(status);
}
#endif
