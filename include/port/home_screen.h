/*
 * N141: the Party Board menu (the launcher, src/port/ui/prelaunch.cpp) as a home screen. With a disc the game boots
 * behind it; its first boot waits in bootDll while the menu is open (no logos or title music under it), and the
 * card/file-select music (msm song 43, modeseldll filesel.c) plays from the disc meanwhile, until Play, Continue or
 * Online closes the menu. src/port/home_screen.c.
 */
#ifndef PORT_HOME_SCREEN_H
#define PORT_HOME_SCREEN_H

#include "dolphin/types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* bootDll, first boot (QuickContinueHold): returns once the menu is closed (at once without one: --script runs, the
 * browser, skipPreLaunchUI). The music plays while it waits and fades out as it returns. */
void port_home_screen_hold(void);
/* Whether the menu's music is playing (the home screen's test hook). */
BOOL port_home_screen_music_playing(void);

#ifdef __cplusplus
}
#endif

#endif
