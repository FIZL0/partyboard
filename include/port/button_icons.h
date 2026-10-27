#ifndef PORT_BUTTON_ICONS_H
#define PORT_BUTTON_ICONS_H

/* N49: per-controller button hints. The game's message icons (win/icon.anm banks 0-12: stick, d-pad, A, B, X, Y,
 * R, R pressed, L, L pressed, C stick, Z, Start) are swapped for the buttons of the controller a window's player is
 * using, by the Button icons setting (game.buttonIcons). The glyphs are drawn here at runtime (our own art). */

#include "dolphin/types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* game.buttonIcons values (the setting). */
enum {
    PB_ICONS_AUTO = 0,
    PB_ICONS_PLAYSTATION,
    PB_ICONS_XBOX,
    PB_ICONS_SWITCH,
    PB_ICONS_GENERIC,
    PB_ICONS_GAMECUBE,
    PB_ICONS_SETTING_COUNT,
};

/* Families a port resolves to (Auto adds Steam Deck and touch). */
typedef enum {
    PB_FAMILY_GAMECUBE = 0,
    PB_FAMILY_PLAYSTATION,
    PB_FAMILY_XBOX,
    PB_FAMILY_SWITCH,
    PB_FAMILY_GENERIC,
    PB_FAMILY_STEAMDECK,
    PB_FAMILY_COUNT,
} PbIconFamily;

struct AnimData_s;

/* The family whose glyphs pad port `port` (0-3) shows under the current setting. */
PbIconFamily pb_button_icons_family(int port);
/* Display name of a family ("Xbox", ...). */
const char *pb_button_icons_family_name(PbIconFamily family);

/* Replacement for message icon `entry` (spcFontTbl index) in a window whose pads are `pad_mask` (bit = pad
 * port). Returns 0 to keep the original icon; else sets *anim and *bank. The anim is locked for the process. */
int pb_button_icon_for_window(int entry, u32 pad_mask, struct AnimData_s **anim, s16 *bank);

/* Test/debug: writes every family's glyph sheet as a PNG-less raw RGBA dump (w, h, then pixels) to `path`.
 * Returns 0 on success. */
int pb_button_icons_dump_sheet(const char *path);

#ifdef __cplusplus
}
#endif

#endif
