#ifndef PORT_TOUCH_HAPTICS_H
#define PORT_TOUCH_HAPTICS_H

/*
 * N111: the on-screen game pad's vibration (src/port/ui/touch_haptics.cpp). Only the touch pad calls it: presses on
 * physical controllers never vibrate. Settings > Input > Vibrate on Touch Buttons (game.touchHaptics) turns it off.
 * Android: Vibrator, EFFECT_CLICK / EFFECT_TICK (API 29+, a 20 / 8 ms one-shot below). iOS: UIImpactFeedbackGenerator
 * medium / light. Browser: navigator.vibrate (Android browsers; iOS Safari has none, nothing happens). Other
 * platforms: nothing.
 */

#ifdef __cplusplus
extern "C" {
#endif

typedef enum TouchHapticKind {
    TOUCH_HAPTIC_PRESS = 0, /* a button went down */
    TOUCH_HAPTIC_STICK = 1, /* the stick moved into a new direction: lighter */
} TouchHapticKind;

/* Vibrates once if the setting is on. */
void touch_haptic(TouchHapticKind kind);

#ifdef __cplusplus
}
#endif

#endif
