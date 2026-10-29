/*
 * N141: the home screen's hold and music (include/port/home_screen.h).
 *
 * The game boots behind the Party Board menu once a disc is set (Prelaunch::update). Before N141 bootDll went on to the
 * logos and the title under the menu, with their sounds and the title stream; now its first boot waits here while the
 * menu is open, as N132's Continue hold did, and plays the file-select music through the game's msm. The rest of the
 * boot is the usual one, run after the menu closes.
 *
 * Nothing here runs in --script runs, the browser or with skipPreLaunchUI (no menu), nor during netplay. An online
 * match starts long after (lobby, mode select) on a MusyX the session settles (partyboard_audio_net_clock_settle), and
 * msm's handles are netplay's own then (src/port/msm.c), so a peer that heard the menu's music does not differ from
 * one that did not.
 */

#include "dolphin.h"
#include "game/audio.h"
#include "game/memory.h"
#include "game/msm.h"
#include "game/process.h"
#include "port/home_screen.h"
#include "port/netplay.h"
#include "port/ui.h"

#include <stdlib.h>

/* modeseldll filesel.c: HuAudSeqPlay(43) as the card (file) select opens */
#define HOME_MUSIC_ID 43
/* fade out as the menu fades (prelaunch.rcss: 1 s, 0.25 s with Fast Menus) */
#define HOME_MUSIC_FADE_MS 500

static s32 sMusicNo = -1;

/* PB_HOME_MUSIC=0 turns the music off (test runs); the hold stays. */
static BOOL music_enabled(void)
{
    const char *env = getenv("PB_HOME_MUSIC");
    return env == NULL || env[0] != '0';
}

static void music_start(void)
{
    s32 result;
    void *samples;

    if (!music_enabled()) {
        return;
    }
    result = msmMusPlay(HOME_MUSIC_ID, NULL);
    if (result == MSM_ERR_GRP_NOTLOADED || result == MSM_ERR_MUSGRP_NOTLOADED) {
        /* the base group and group set 0 (MSM_GRPSET_INIT, mode select's), loaded as bootDll loads them after the logos
         * (BootInitForSkippedSequence): its own loads are then no-ops, and the boot ends with the same groups */
        samples = HuMemDirectMalloc(HEAP_DATA, msmSysGetSampSize(0));
        msmSysLoadGroup(0, samples, 0);
        HuMemDirectFree(samples);
        HuAudSndGrpSetSet(0);
        result = msmMusPlay(HOME_MUSIC_ID, NULL);
    }
    if (result < 0) {
        OSReport("home: music %d failed (%d)\n", HOME_MUSIC_ID, result);
        return;
    }
    sMusicNo = result;
    OSReport("home: music %d on channel %d\n", HOME_MUSIC_ID, result);
}

static void music_stop(void)
{
    if (sMusicNo < 0) {
        return;
    }
    msmMusStop(sMusicNo, HOME_MUSIC_FADE_MS);
    OSReport("home: music fading out\n");
    sMusicNo = -1;
}

void port_home_screen_hold(void)
{
    if (netplay_active() || !ui_is_prelaunch_open()) {
        return;
    }
    music_start();
    while (ui_is_prelaunch_open()) {
        HuPrcVSleep();
    }
    music_stop();
}

BOOL port_home_screen_music_playing(void)
{
    return sMusicNo >= 0 && msmMusGetStatus(sMusicNo) == MSM_MUS_PLAY;
}
