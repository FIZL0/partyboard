#ifndef PORT_AUDIO_H_
#define PORT_AUDIO_H_

#include <dolphin/dvd.h>
#include <dolphin/types.h>

#ifdef __cplusplus
extern "C" {
#endif

void partyboard_audio_lock(void);
void partyboard_audio_unlock(void);
void partyboard_audio_pre_init(void);
void partyboard_audio_start(void);
void partyboard_audio_shutdown(void);
BOOL partyboard_audio_read_async(DVDFileInfo *fileInfo, void *addr, s32 length, s32 offset, DVDCallback callback);
/* N112: once per game frame (msmSysRegularProc): services the browser's stream reads and keeps MusyX's clock moving
 * while no device pulls audio (none opened, paused, browser before the first gesture). */
void partyboard_audio_frame(void);
/* N112: netplay_begin: the sound handles the game sees during a match start again from 0 (src/port/msm.c). N122: and
 * MusyX's frame clock starts from a state every peer shares. */
void partyboard_audio_net_reset(void);

/* N122: how msm answers during netplay (src/port/msm.c). OFF: offline, or PB_AUDIO_REAL_STATUS (device clock). */
enum { PB_AUDIO_NET_OFF, PB_AUDIO_NET_LOCKED, PB_AUDIO_NET_STUBS };
s32 partyboard_audio_net_mode(void);
/* N122 (src/port/audio.cpp), clock_begin (not under msm's lock): MusyX runs on game frames from now on (`locked`) or on
 * the device again; the stream reads still out are done and the cycle under way finished. FALSE if MusyX isn't
 * running yet. clock_settle: once everything is stopped, 40 cycles (the stops' fades) into nothing, MusyX's random
 * seed back to 1, frame count 0. clock_cycle: one more 5 ms cycle now, into nothing (SNDGRP_WAIT's polls). */
BOOL partyboard_audio_net_clock_begin(BOOL locked);
void partyboard_audio_net_clock_settle(void);
void partyboard_audio_net_clock_cycle(void);

#ifdef __cplusplus
}
#endif

#endif
