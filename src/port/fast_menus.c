/*
 * N131: Fast Menus (port/fast_menus.h). The menu overlays run two (four while A or START is held) game frames for
 * each one shown: the others are not presented (aurora_set_present_skip, as --turbo's N120 throttle) and main.c's
 * loop skips their frame_limiter, so the group takes one display frame. Nothing the game computes changes, only how many of its
 * frames are shown: a skipped frame still uploads its textures, and no menu uses a one-shot EFB copy
 * (WIPE_TYPE_CROSS is not used by these overlays; the shadow map is drawn again on every shown frame).
 */

#include "dolphin.h"
#include "game/object.h"
#include "game/pad.h"
#include "port/fast_menus.h"
#include "port/netplay.h"
#include "port/settings.h"

#include <aurora/aurora.h>

#define PAD_DIRECTIONS (PAD_BUTTON_LEFT | PAD_BUTTON_RIGHT | PAD_BUTTON_UP | PAD_BUTTON_DOWN)

bool port_fast_menus(void)
{
    if (!partyboard_settings_fastMenus()) {
        return false;
    }
    /* A --script run has a netplay session from its first frame (for its hash log), and its pads are tuned to the
     * menus' original frames: only `fast-menus on` asks for it, and never once its --match runs. */
    if (netplay_debug_scripted()) {
        return netplay_debug_fast_menus() && !netplay_match_running() && !netplay_lockstep();
    }
    return !netplay_active();
}

static bool fast_menus_overlay(OMOVL ovl)
{
    return ovl == DLL_bootdll || ovl == DLL_modeseldll || ovl == DLL_mentdll || ovl == DLL_mpexdll
        || ovl == DLL_subchrseldll;
}

/*
 * Game frames per shown frame. A held direction repeats per game frame (HuPadDStkRep, HuPadBtnRep): normal speed
 * keeps the cursor's pace. A held A or START (its press was taken on the first frame, the menus take presses, not
 * holds) fast-forwards what that press started: 4.
 */
static s32 fast_menus_speed(void)
{
    s32 speed = 2;
    s32 i;

    for (i = 0; i < 4; i++) {
        if (HuPadDStk[i] != 0 || (HuPadBtn[i] & PAD_DIRECTIONS) != 0) {
            return 1;
        }
        if ((HuPadBtn[i] & (PAD_BUTTON_A | PAD_BUTTON_START)) != 0) {
            speed = 4;
        }
    }
    return speed;
}

bool port_fast_menus_frame(void)
{
    static bool skipping;
    static s32 phase;
    bool skip = false;
    s32 speed;

    if (port_fast_menus() && fast_menus_overlay(omcurovl) && (speed = fast_menus_speed()) > 1) {
        /* the last game frame of each group of `speed` is shown */
        phase = (phase + 1) % speed;
        skip = phase != 0;
    }
    else {
        phase = 0;
    }
    if (skip != skipping) {
        skipping = skip;
        aurora_set_present_skip(skip);
    }
    return skip;
}
