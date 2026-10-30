#pragma once

/*
 * N143: controllers connected (or reconnected) at any time (src/port/controller_watch.cpp). Logs every controller SDL
 * and aurora add or remove, with the port aurora gave it, so partyboard.log shows what happened; hands aurora a gamepad
 * SDL knows but aurora never added; on iOS, re-scans SDL's controllers when Apple's GameController framework knows
 * one SDL doesn't (src/port/ios/ControllerWatch.m), which is what a relaunch did.
 */

#ifdef __cplusplus
extern "C" {
#endif

/* Watches SDL's controller events (call once, after aurora_initialize). */
void port_controller_watch_init(void);
/* Once per loop (from port_lifecycle_hold): the checks above, about once a second. */
void port_controller_watch_frame(void);
/* Re-scan SDL's controllers at the next port_controller_watch_frame (any thread); why goes in the log. */
void port_controller_watch_rescan(const char *why);
/* A line for the log (partyboard::controllers), from any thread. */
void port_controller_watch_note(const char *line);

/* iOS (src/port/tap_resume.c): a screenshot took event focus from GameController. With a
 * controller connected, the overlay asks for a tap until a touch or a pad button press comes through. Draw only. */
void port_tap_resume_focus_lost(const char *why, int controllers);
void port_tap_resume_clear(void);
int port_tap_resume_wanted(void);

#ifdef __APPLE__
/* src/port/ios/ControllerWatch.m (iOS): observes GCController connects/disconnects (logged) from now on. */
void PartyBoard_iOS_ControllerWatchStart(void);
/* The game controllers GameController.framework has connected now (what SDL's MFi driver would add). */
int PartyBoard_iOS_ControllerCount(void);
/* src/port/ios/TapResume.m (iOS): shows or hides the tap-to-resume view over the whole window (any thread). */
void PartyBoard_iOS_TapResumeShow(int show);
#endif

#ifdef __cplusplus
}
#endif
