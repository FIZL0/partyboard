#pragma once

/*
 * N110: the app's lifecycle (src/port/lifecycle.cpp). iOS takes the focus (Control Center, a permission prompt) or puts
 * the app in the background, Android pauses it, a desktop window is minimised or hidden: the game holds its frames
 * (no game tick, no GPU work) until it is back. A netplay match holds like a lockstep WAIT, and the peers are told why
 * (pbnet_keeper_hidden, net/src/keeper.rs) instead of hearing nothing.
 */

#ifdef __cplusplus
extern "C" {
#endif
#include <stdbool.h>

/* Watches SDL's app and window events (call once, after aurora_initialize). */
void port_lifecycle_init(void);
/*
 * Once per loop, after aurora_update: true while the game must hold (run no tick, begin no aurora frame; pace the loop
 * with frame_limiter and go on). Tells pbnet when a hold starts and ends, and keeps the match's peers waiting.
 */
bool port_lifecycle_hold(void);

#ifdef __cplusplus
}
#endif
