#ifndef PORT_BUTTON_ART_H
#define PORT_BUTTON_ART_H

/* N150: GameCube button pictures that minigames and menus draw from their own data (sprites and model textures, not
 * the message font's icons, which N49 swaps in window.c) are redrawn with the buttons of the player's controller, by
 * the Button icons setting. The pictures are found by the hash of their texels (research/journal/2026-10-29-n150-*
 * lists them). Pictures only: game state, heaps and timing are untouched (the art lives outside the game heaps). */

#include "dolphin/types.h"

#ifdef __cplusplus
extern "C" {
#endif

struct AnimData_s;
struct AnimPat_s;
struct HuSprite_s;
struct HsfBitmap_s;

/* HuSprAnimRead, after converting a fresh anim: notes the anim if one of its bitmaps is known button art. */
void pb_button_art_anim_read(struct AnimData_s *anim);
/* HuSprDisp: the anim to draw `sprite` with: a copy whose button bitmaps show the seat's controller, or `anim`. A copy
 * comes with its own patterns (the redrawn bitmaps are larger): *pat, one of anim's, is changed to the copy's. */
struct AnimData_s *pb_button_art_sprite_anim(struct HuSprite_s *sprite, struct AnimData_s *anim, struct AnimPat_s **pat);
/* The player (GWPlayerCfg index) whose controller a sprite's button art shows; sprites not set show the first
 * human player's. espId is an esprite id (espEntry). */
void pb_button_art_esp_seat(s16 espId, s32 player);
/* HSF load, after its bitmaps: redraws known button textures for the first human player's controller. */
void pb_button_art_hsf_bitmaps(struct HsfBitmap_s *bitmaps, s32 count);

/* Test (N150 harness): redraws one bitmap given as GX texels. kind 0 = ANM bitmap (fmt = ANIM_BMP_*), 1 = HSF
 * (fmt = HSF_BMPFMT_*); pal = big-endian palette or NULL. Port -1 uses aurora's default bindings. Writes straight RGBA8
 * rows to `out` (cap bytes) and the size to *ow, *oh. Returns 0, 1 if the texels are not known art, -1 on error. */
int pb_button_art_test_render(int kind, int fmt, int pix_size, int w, int h, const void *data, const void *pal,
    int pal_num, int family, int port, u8 *out, u32 cap, int *ow, int *oh);

#ifdef __cplusplus
}
#endif

#endif
