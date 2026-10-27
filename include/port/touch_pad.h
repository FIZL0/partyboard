#ifndef PORT_TOUCH_PAD_H
#define PORT_TOUCH_PAD_H

/*
 * N70: on-screen game pad for touch screens (src/port/ui/touch_pad.cpp). A translucent stick, A/B/X/Y, L/R/Z and
 * Start drawn over the game; its touches go to controller port 1 through aurora's virtual pad (PADSetVirtualStatus),
 * merged into what PADRead returns for that port. Shown from the first touch, hidden again by a controller or
 * keyboard press; Settings > Input > Touch Controls turns it off.
 */

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* The pad is on screen (the last input was a touch): the touch family of button icons applies to its port. */
bool touch_pad_shown(void);

/* The controller port the pad plays (PAD_CHAN0). */
int touch_pad_port(void);

#ifdef __cplusplus
}
#endif

#endif
