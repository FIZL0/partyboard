/* Scratch code that was in src/port/netplay.c for the runs (removed before commit). n42_pads(local, next) was called
 * in netplay_tick_begin right before pbnet takes the local pads; n64_probe() at the top of netplay_frame_end, with
 * '#define _GNU_SOURCE' first in the file. */
/* N42 scratch: this machine's seat mashes A (START in instDll and the party results). */
static void n42_pads(PADStatus *local, u32 at)
{
    static int last_ovl = -1;
    static u32 since;
    if (getenv("PB_N42") == NULL) {
        return;
    }
    if ((int)omcurovl != last_ovl) {
        last_ovl = (int)omcurovl;
        since = at;
    }
    memset(local, 0, 4 * sizeof(*local));
    if (at % 24 < 4 && !(omcurovl == 3 && at - since < 120)) {
        /* ovl 78: the party's end wants A first (ceremony), then START alone (results pages): take turns */
        local[0].button = (omcurovl == 3) ? PAD_BUTTON_START
            : (omcurovl == 78 && (at - since) / 600 % 2 == 1) ? (PAD_BUTTON_START | PAD_BUTTON_B) /* B: out of a page's details */
                                                              : PAD_BUTTON_A;
    }
}

/* N64 scratch: outside a session (the party results after the match), port 0 presses like n42_pads. */
void n64_local_pads(PADStatus *status)
{
    static u32 n;
    if (getenv("PB_N42") == NULL || netplay_active() || omcurovl != 78) {
        return;
    }
    n++;
    memset(status, 0, 4 * sizeof(*status));
    if (n % 24 < 4) {
        status[0].button = (n / 600 % 2 == 1) ? (PAD_BUTTON_START | PAD_BUTTON_B) : PAD_BUTTON_A;
    }
}

/* N64 TMP probe: processes and model motions per frame */
#include "game/process.h"
#include "game/hu3d.h"
#include <dlfcn.h>
static void n64_probe(void)
{
    static FILE *out;
    static int lo = -1, hi;
    Process *p;
    s32 i;
    if (lo == -1) {
        const char *e = getenv("PB_N64_PROBE");
        lo = -2;
        if (e && sscanf(e, "%d-%d", &lo, &hi) == 2) {
            char path[512];
            snprintf(path, sizeof(path), "%s/probe.%u.txt", getenv("HOME"), (unsigned)session_seed);
            out = fopen(path, "w");
        }
    }
    if (out == NULL || !pbnet_lockstep_active() || (int)frame < lo || (int)frame > hi) {
        return;
    }
    {
        extern Process *n64_prc_top(void);
        for (p = n64_prc_top(); p != NULL; p = p->next) {
            Dl_info di;
            const char *mod = "?";
            unsigned long off = 0;
            if (dladdr((void *)(uintptr_t)p->func, &di) && di.dli_fname) {
                mod = strrchr(di.dli_fname, '/') ? strrchr(di.dli_fname, '/') + 1 : di.dli_fname;
                off = (unsigned long)((char *)(uintptr_t)p->func - (char *)di.dli_fbase);
            }
            fprintf(out, "%u P %s+%lx prio %u stat %u exec %u sleep %d\n", frame, mod, off, p->prio, p->stat, p->exec, (int)p->sleep_time);
        }
    }
    for (i = 0; i < HU3D_MODEL_MAX; i++) {
        if (Hu3DData[i].hsf != NULL) {
            fprintf(out, "%u M %d mot %d shift %d time %.3f\n", frame, (int)i, (int)Hu3DMotionIDGet(i), (int)Hu3DMotionShiftIDGet(i),
                (double)Hu3DMotionTimeGet(i));
        }
    }
    fprintf(out, "%u R brand %08X frand %08X rand8 %08X\n", frame, (unsigned)boardRandSeed, (unsigned)frandSeedGet(), (unsigned)rnd_seed);
}

