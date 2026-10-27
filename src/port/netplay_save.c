/*
 * Online saves (N9): the board checkpoint of an online match, written by every peer.
 *
 * The checkpoint is the game's own: where resultDll's SaveExec would call SLSaveBoard and write the memory card
 * (after each minigame), netplay_checkpoint takes the same SLSaveBoard blob (GWSystem + GWPlayer[4], big-endian as on
 * the card) and pbnet writes it with the Room, the seat -> player key map, the seeds, the turn and the time to
 * <user dir>/online/<match id>.json (net/src/save.rs). A continue loads it back with SLLoadBoard
 * (netplay_board_blob_set, from netplay_match_continue).
 *
 * The blob goes through saveBuf, the local card's buffer: SLSaveBoard/SLLoadBoard only touch its party board part,
 * which is backed up and put back around every call, so an online match never changes what the local card would
 * get written (the next SLSave of a menu writes the player's own offline party, not the online one).
 */

#include "dolphin.h"
#include "game/gamework_data.h"
#include "game/saveload.h"
#include "port/netplay.h"
#include "port/pbnet.h"

#include <stddef.h>
#include <stdio.h>
#include <string.h>

extern s32 rnd_seed;
extern u32 boardRandSeed;
void SLSaveBoard(void);
void SLLoadBoard(void);

/* The part of saveBuf SLSaveBoard writes: the party board's GWSystem and GWPlayer[4]. */
#define BLOB_BEGIN offsetof(SaveBufData, system)
#define BLOB_END (offsetof(SaveBufData, player) + 4 * sizeof(PlayerState))
#define BLOB_SIZE (BLOB_END - BLOB_BEGIN)

static u8 last_blob[NETPLAY_BLOB_MAX];
static u32 last_size;
static char last_path[1024]; /* the save file this match's last checkpoint wrote ("": none) */

u32 netplay_board_blob_size(void)
{
    return (u32)BLOB_SIZE;
}

u32 netplay_board_blob_get(u8 *out, u32 cap)
{
    static u8 keep[BLOB_SIZE];
    if (cap < BLOB_SIZE) {
        return 0;
    }
    memcpy(keep, &saveBuf.buf[BLOB_BEGIN], BLOB_SIZE);
    SLSaveBoard();
    memcpy(out, &saveBuf.buf[BLOB_BEGIN], BLOB_SIZE);
    memcpy(&saveBuf.buf[BLOB_BEGIN], keep, BLOB_SIZE);
    return (u32)BLOB_SIZE;
}

BOOL netplay_board_blob_set(const u8 *blob, u32 size)
{
    static u8 keep[BLOB_SIZE];
    if (blob == NULL || size != BLOB_SIZE) {
        return FALSE;
    }
    memcpy(keep, &saveBuf.buf[BLOB_BEGIN], BLOB_SIZE);
    memcpy(&saveBuf.buf[BLOB_BEGIN], blob, BLOB_SIZE);
    SLLoadBoard();
    memcpy(&saveBuf.buf[BLOB_BEGIN], keep, BLOB_SIZE);
    return TRUE;
}

s32 netplay_board_blob_board(const u8 *blob, u32 size)
{
    static SystemState sys;
    static PlayerState players[4];
    static PlayerConfig cfg[4];
    s32 board;
    if (blob == NULL || size != BLOB_SIZE) {
        return -1;
    }
    sys = GWSystem;
    memcpy(players, GWPlayer, sizeof(players));
    memcpy(cfg, GWPlayerCfg, sizeof(cfg));
    netplay_board_blob_set(blob, size);
    board = GWSystem.board;
    GWSystem = sys;
    memcpy(GWPlayer, players, sizeof(players));
    memcpy(GWPlayerCfg, cfg, sizeof(cfg));
    return board;
}

void netplay_checkpoint(void)
{
    static char room[2048];
    char path[1024];
    u32 seeds[4];
    u32 size;

    if (!netplay_match_running()) {
        return;
    }
    size = netplay_board_blob_get(last_blob, sizeof(last_blob));
    if (size == 0) {
        last_size = 0;
        OSReport("netplay: checkpoint: the board blob does not fit\n");
        return;
    }
    last_size = size;
    seeds[0] = netplay_session_seed();
    seeds[1] = frandSeedGet();
    seeds[2] = (u32)rnd_seed;
    seeds[3] = boardRandSeed;
    if (netplay_match_room_json(room, sizeof(room)) >= (int)sizeof(room)) {
        room[0] = '\0';
    }
    if (pbnet_save_write(netplay_user_dir(), room[0] != '\0' ? room : NULL, last_blob, (int)size, netplay_frame(), seeds,
            path, sizeof(path))) {
        OSReport("netplay: checkpoint at frame %u, turn %d/%d: %s\n", netplay_frame(), GWSystem.turn, GWSystem.max_turn,
            path);
        snprintf(last_path, sizeof(last_path), "%s", path);
    } else {
        /* The match goes on: the save is a convenience, the blob is still kept for a reload. */
        OSReport("netplay: checkpoint at frame %u: the online save could not be written\n", netplay_frame());
    }
}

void netplay_checkpoint_finish(void)
{
    static char room[2048];

    if (!netplay_match_running()) {
        return;
    }
    if (netplay_match_room_json(room, sizeof(room)) >= (int)sizeof(room)) {
        room[0] = '\0';
    }
    /* By the match id: also a continue whose peer wrote no checkpoint since. The last path covers a lobby that closed
     * during the match (no started room, and the --match room's id is not the lobby's). */
    if (pbnet_save_finish(netplay_user_dir(), room[0] != '\0' ? room : NULL)
        || (last_path[0] != '\0' && pbnet_save_delete(last_path))) {
        OSReport("netplay: match over: its online save is deleted\n");
    }
    last_path[0] = '\0';
}

void netplay_checkpoint_set(const u8 *blob, u32 size)
{
    if (blob == NULL || size == 0 || size > sizeof(last_blob)) {
        last_size = 0;
        if (blob == NULL) {
            last_path[0] = '\0';
        }
        return;
    }
    memcpy(last_blob, blob, size);
    last_size = size;
}

u32 netplay_checkpoint_last(u8 *out, u32 cap)
{
    if (last_size == 0 || cap < last_size || !netplay_match_running()) {
        return 0;
    }
    memcpy(out, last_blob, last_size);
    return last_size;
}
