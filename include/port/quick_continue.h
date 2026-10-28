/*
 * N132: Party Board menu > Continue. The launcher lists the card's games in progress (a party or story board saved by
 * resultDll, or the party autosave of N47) and boots straight into the one picked: bootDll loads that card file and
 * resumes the board as file select + mode select + mentDll's "continue?" would, skipping all three.
 * src/port/quick_continue.c (game side), src/port/quick_continue_card.cpp (the launcher's read of the card).
 */
#ifndef PORT_QUICK_CONTINUE_H
#define PORT_QUICK_CONTINUE_H

#include "dolphin/types.h"

#ifdef __cplusplus
extern "C" {
#endif

#define QUICK_CONTINUE_PARTY 0
#define QUICK_CONTINUE_STORY 1
#define QUICK_CONTINUE_MAX 7 /* the autosave, then a party and a story per card file */

typedef struct QuickContinueEntry {
    s32 kind; /* QUICK_CONTINUE_PARTY / _STORY */
    s32 box; /* card file 0-2 (MARIPA4BOX<n>), -1: none (an autosave of a party played without a card file) */
    BOOL autosave; /* the party is the autosave's (local/party.json), which mentDll's continue would offer */
    s32 board; /* GWSystem.board: 0-5 */
    s32 turn;
    s32 max_turn;
    s32 character[4]; /* the four players' characters, -1: unknown */
} QuickContinueEntry;

/* A card file's SAVE_BUF_SIZE bytes, as on the card: its party and story in progress into out (0-2 entries). */
s32 quick_continue_parse_file(const u8 *buf, s32 box, QuickContinueEntry *out);
/* An SLSaveBoard blob (GWSystem + GWPlayer[4], big-endian): the party in it. FALSE if it is no party board blob. */
BOOL quick_continue_parse_blob(const u8 *blob, u32 size, QuickContinueEntry *out);
/* The launcher's list: the autosave (if any; it stands for every card party, which the menus would replace by it),
 * then each card file's party and story. `files` are the card files' buffers (NULL: no such file). */
s32 quick_continue_list(const u8 *const files[3], QuickContinueEntry out[QUICK_CONTINUE_MAX]);

/* The launcher shows Continue: bootDll then waits for the launcher to close before it boots (QuickContinueHold). */
void quick_continue_offer(BOOL on);
/* The launcher's pick, for bootDll. */
void quick_continue_request(s32 kind, s32 box);
/* bootDll, first boot: with a verified disc the game starts behind the launcher, so while it offers Continue the boot
 * waits here for its choice (Continue, or Play: the usual boot). */
void QuickContinueHold(void);
/* bootDll, first boot (omovlevtno 0): TRUE when a continue was asked for (the launcher's, or PB_CONTINUE=party|story
 * [:box] in test runs) and its card file has that game in progress; the file is then loaded (GWGameStat, saveBuf). */
BOOL QuickContinueLoad(void);
/* bootDll, after QuickContinueLoad and the system init: the board (or story event) is called. */
void QuickContinueStart(void);

#ifdef __cplusplus
}
#endif

#endif
