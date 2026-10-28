#ifndef _BOARD_BOWSER_H
#define _BOARD_BOWSER_H

#include "game/process.h"

#ifdef __EMSCRIPTEN__
/* bowser.c's definition; wasm traps on a call whose signature isn't the definition's */
s32 BoardBowserExec(s32 player, s32 space);
#else
void BoardBowserExec(s32 player, s32 space);
#endif

#endif
