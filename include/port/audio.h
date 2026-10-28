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
/* N124: output gain in percent (0-100, clamped), applied live to the mixed output. */
void partyboard_audio_set_master_volume(s32 volume);
void partyboard_audio_start(void);
void partyboard_audio_shutdown(void);
BOOL partyboard_audio_read_async(DVDFileInfo *fileInfo, void *addr, s32 length, s32 offset, DVDCallback callback);
/* N112: once per game frame (msmSysRegularProc): services the browser's stream reads and keeps MusyX's clock moving
 * while no device pulls audio (none opened, paused, browser before the first gesture). */
void partyboard_audio_frame(void);
/* N112: netplay_begin: the sound handles the game sees during a match start again from 0 (src/port/msm.c). */
void partyboard_audio_net_reset(void);

#ifdef __cplusplus
}
#endif

#endif
