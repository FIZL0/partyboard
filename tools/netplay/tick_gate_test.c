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

SystemState GWSystem; PlayerState GWPlayer[4]; PlayerConfig GWPlayerCfg[4];
u16 HuPadBtn[4], HuPadBtnDown[4], HuPadBtnRep[4]; s8 HuPadStkX[4], HuPadStkY[4], HuPadSubStkX[4], HuPadSubStkY[4];
u8 HuPadTrigL[4], HuPadTrigR[4], HuPadDStk[4], HuPadDStkRep[4]; s8 HuPadErr[4];
OMOVL omcurovl; s32 rnd_seed; u32 boardRandSeed; BOOL PartyBoard_IsRunning = TRUE; static u32 fseed;
u32 frandSeedGet(void) { return fseed; } void frandSeedSet(u32 s) { fseed = s; }
void OSReport(const char *fmt, ...) { va_list a; va_start(a, fmt); vprintf(fmt, a); va_end(a); }

static PADStatus phys[4]; static int padread_calls;
u32 PADRead(PADStatus *s) { padread_calls++; memcpy(s, phys, sizeof(phys)); return 0; }

static int on, wait_now, hash_calls; static u32 fr, taken_for = 0xFFFFFFFF, hashed_frame;
static PADStatus taken[4];
int pbnet_lockstep_active(void) { return on; }
uint32_t pbnet_frame(void) { return fr; }
void pbnet_set_hash(uint32_t f, uint64_t h) { (void)h; hash_calls++; hashed_frame = f; }
int pbnet_poll_desync(uint32_t *a, int *b, uint64_t *c, uint64_t *d) { return 0; }
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

int main(void)
{
    PADStatus got[4];
    /* No lockstep: gate open, PADRead passes through to the controllers. */
    phys[0].button = PAD_BUTTON_B;
    assert(netplay_tick_begin());
    assert(netplay_pad_read(got) == 0 && got[0].button == PAD_BUTTON_B);

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
    /* Session gone: back to local controllers. */
    on = 0; wait_now = 0; phys[0].button = PAD_BUTTON_Y;
    assert(netplay_tick_begin());
    netplay_pad_read(got);
    assert(got[0].button == PAD_BUTTON_Y);
    puts("harness: all checks passed");
    return 0;
}
