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
    frandSeedSet(netplay_seed(NETPLAY_SALT_FRAND));
    rnd_seed = (s32)netplay_seed(NETPLAY_SALT_RAND8);
    boardRandSeed = netplay_seed(NETPLAY_SALT_BOARD);
    OSReport("netplay: session start, seed %08X\n", seed);
}

void netplay_end(void)
{
    active = FALSE;
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

u64 netplay_hash(void)
{
    Fnv64 f = { 0xCBF29CE484222325ULL };
    /* Raw bytes: these structs hold no pointers, and every peer runs the same build. */
    fnv_bytes(&f, &GWSystem, sizeof(GWSystem));
    fnv_bytes(&f, GWPlayer, sizeof(GWPlayer));
    fnv_bytes(&f, GWPlayerCfg, sizeof(GWPlayerCfg));
    fnv_u32(&f, frandSeedGet());
    fnv_u32(&f, (u32)rnd_seed);
    fnv_u32(&f, boardRandSeed);
    fnv_bytes(&f, HuPadBtn, sizeof(HuPadBtn));
    fnv_bytes(&f, HuPadBtnDown, sizeof(HuPadBtnDown));
    fnv_bytes(&f, HuPadBtnRep, sizeof(HuPadBtnRep));
    fnv_bytes(&f, HuPadStkX, sizeof(HuPadStkX));
    fnv_bytes(&f, HuPadStkY, sizeof(HuPadStkY));
    fnv_bytes(&f, HuPadSubStkX, sizeof(HuPadSubStkX));
    fnv_bytes(&f, HuPadSubStkY, sizeof(HuPadSubStkY));
    fnv_bytes(&f, HuPadTrigL, sizeof(HuPadTrigL));
    fnv_bytes(&f, HuPadTrigR, sizeof(HuPadTrigR));
    fnv_bytes(&f, HuPadDStk, sizeof(HuPadDStk));
    fnv_bytes(&f, HuPadDStkRep, sizeof(HuPadDStkRep));
    fnv_bytes(&f, HuPadErr, sizeof(HuPadErr));
    return f.h;
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
 */
static void hash_log_line(FILE *out, u32 at)
{
    Fnv64 pad = { 0xCBF29CE484222325ULL };
    fnv_bytes(&pad, HuPadBtn, sizeof(HuPadBtn));
    fnv_bytes(&pad, HuPadBtnDown, sizeof(HuPadBtnDown));
    fnv_bytes(&pad, HuPadBtnRep, sizeof(HuPadBtnRep));
    fnv_bytes(&pad, HuPadStkX, sizeof(HuPadStkX));
    fnv_bytes(&pad, HuPadStkY, sizeof(HuPadStkY));
    fnv_bytes(&pad, HuPadSubStkX, sizeof(HuPadSubStkX));
    fnv_bytes(&pad, HuPadSubStkY, sizeof(HuPadSubStkY));
    fnv_bytes(&pad, HuPadTrigL, sizeof(HuPadTrigL));
    fnv_bytes(&pad, HuPadTrigR, sizeof(HuPadTrigR));
    fnv_bytes(&pad, HuPadDStk, sizeof(HuPadDStk));
    fnv_bytes(&pad, HuPadDStkRep, sizeof(HuPadDStkRep));
    fnv_bytes(&pad, HuPadErr, sizeof(HuPadErr));
    fprintf(out, "%u %016llX ovl=%d sys=%016llX plr0=%016llX plr1=%016llX plr2=%016llX plr3=%016llX cfg=%016llX "
                 "frand=%08X rand8=%08X brand=%08X pad=%016llX\n",
        at, (unsigned long long)netplay_hash(), (int)omcurovl,
        (unsigned long long)fnv_once(&GWSystem, sizeof(GWSystem)),
        (unsigned long long)fnv_once(&GWPlayer[0], sizeof(GWPlayer[0])),
        (unsigned long long)fnv_once(&GWPlayer[1], sizeof(GWPlayer[1])),
        (unsigned long long)fnv_once(&GWPlayer[2], sizeof(GWPlayer[2])),
        (unsigned long long)fnv_once(&GWPlayer[3], sizeof(GWPlayer[3])),
        (unsigned long long)fnv_once(GWPlayerCfg, sizeof(GWPlayerCfg)), (unsigned)frandSeedGet(), (unsigned)rnd_seed,
        (unsigned)boardRandSeed, (unsigned long long)pad.h);
}

/*
 * Input script (--script). Text, one command per line, '#' starts a comment:
 *
 *   seed <n>                 session seed (default 0x50424E54)
 *   end <frame>              quit after this many frames
 *   <frame> <port> <buttons> [stickX stickY substickX substickY trigL trigR]
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
} script;

static FILE *hash_log;

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
    FILE *f = fopen(path, "r");
    if (f == NULL) {
        OSReport("netplay: cannot open script %s\n", path);
        return FALSE;
    }
    memset(&script, 0, sizeof(script));
    script.seed = 0x50424E54; /* "PBNT" */
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

/* Pads for the current frame: every script line up to and including this frame applied. */
static void script_pads(PADStatus *status)
{
    while (script.next < script.count && script.events[script.next].frame <= frame) {
        const ScriptEvent *ev = &script.events[script.next++];
        script.pads[ev->port] = ev->status;
    }
    memcpy(status, script.pads, sizeof(script.pads));
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
        }
    }
    if (script_path != NULL && !script_load(script_path)) {
        return FALSE;
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

void netplay_frame_end(void)
{
    if (hash_log != NULL) {
        hash_log_line(hash_log, frame);
    }
    frame++;
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
/*
 * Linker wraps (cmake/netplay.cmake, ELF only): every OSGetTime/OSGetTick/PADRead call in dol and the
 * RELs lands here. Offline they pass straight through to aurora.
 */
OSTime __real_OSGetTime(void);
OSTick __real_OSGetTick(void);
u32 __real_PADRead(PADStatus *status);

OSTime __wrap_OSGetTime(void)
{
    if (active) {
        return netplay_time();
    }
    return __real_OSGetTime();
}

OSTick __wrap_OSGetTick(void)
{
    if (active) {
        return (OSTick)(netplay_time() & 0xFFFFFFFF);
    }
    return __real_OSGetTick();
}

u32 __wrap_PADRead(PADStatus *status)
{
    if (active && script.loaded) {
        script_pads(status);
        return 0;
    }
    return __real_PADRead(status);
}
#endif
