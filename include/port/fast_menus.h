#pragma once

/*
 * N131: Settings > Gameplay > Fast Menus (game.fastMenus, on by default). The menus before a board (bootDll's title,
 * modeseldll's file and mode select, mentDll's party setup, mpexDll, subchrselDll) run two game frames for each one
 * shown: their animations, fades, camera moves and text take half the time. Holding A or START fast-forwards (four
 * frames per shown one). While a player holds a direction they run at normal speed, so cursor repeats keep their pace. bootDll also skips the Nintendo/Hudson logos (their setup
 * still runs) and shortens the title's fades (src/REL/bootDll/main.c).
 *
 * Never during netplay (netplay_active: a match starts on the board, and its frames are paced by lockstep), and a
 * --script run gets it only with the script line `fast-menus on`, so the scripts tuned to the menus' frames
 * (mash-a.script, party.script, ...) keep them. Boards and minigames are never sped up.
 */

#ifdef __cplusplus
extern "C" {
#endif
#include <stdbool.h>

/* Fast Menus applies to this run: the setting is on, no netplay session, and a --script run asked for it. */
bool port_fast_menus(void);
/*
 * Once per game frame, before aurora_begin_frame (src/game/main.c): true when this frame is one Fast Menus runs
 * without showing it. The frame is then not presented (aurora_set_present_skip) and the caller skips frame_limiter.
 */
bool port_fast_menus_frame(void);

#ifdef __cplusplus
}
#endif
