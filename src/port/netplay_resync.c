/*
 * Desync resync points (N46): the board state at the start of every player's turn.
 *
 * A desync has every peer reload the host's newest resync point (net/src/desync.rs). Before N46 that was only the
 * online save after each minigame (netplay_checkpoint, N9), up to a whole turn back. MainFunc (board/main.c) now hands
 * the state at each player's turn start here: the same SLSaveBoard blob, kept by pbnet in memory as the host's next
 * resync point (pbnet_resync_point). N47 also writes it as the online save (netplay_checkpoint), and outside a match
 * as the local party's autosave (netplay_autosave.c).
 *
 * The board re-enters a turn from GWSystem/GWPlayer alone: the loop of MainFunc starts at GWSystem.player_curr, as it
 * does whenever the board overlay comes back mid-round. Every peer reloads the same blob, so they run on alike.
 */

#include "dolphin.h"
#include "game/flag.h"
#include "game/gamework_data.h"
#include "port/netplay.h"
#include "port/pbnet.h"

#include <string.h>

BOOL netplay_turn_hooks(void)
{
    return netplay_active() || GWPartyGet() == 1;
}

void netplay_turn_checkpoint(void)
{
    static u8 blob[NETPLAY_BLOB_MAX];
    u32 size;

    if (!netplay_match_running()) {
        netplay_party_autosave();
        return;
    }
    /* N47: every peer's online save follows every turn, so a crash or a quit continues from this turn. */
    netplay_checkpoint();
    if (!pbnet_lockstep_active()) {
        return;
    }
    size = netplay_checkpoint_last(blob, sizeof(blob));
    if (size == 0) {
        return;
    }
    if (pbnet_resync_point(blob, (int)size, GWSystem.turn, GWSystem.max_turn)) {
        OSReport("netplay: resync point at frame %u: turn %d/%d, player %d\n", netplay_frame(), GWSystem.turn,
            GWSystem.max_turn, GWSystem.player_curr);
    }
}

static BOOL resume_armed;

void netplay_turn_resume_arm(void)
{
    resume_armed = TRUE;
}

BOOL netplay_turn_resuming(void)
{
    /* BoardStartCheck's own flags (main.c): a return after a minigame or an event, which the board handles itself. */
    static const u16 events[] = { 2, 3, 4, 5, 6 };
    u32 i;

    if (!resume_armed || !_CheckFlag(FLAG_ID_MAKE(1, 1))) {
        return FALSE;
    }
    for (i = 0; i < sizeof(events) / sizeof(events[0]); i++) {
        if (_CheckFlag(FLAG_ID_MAKE(1, events[i]))) {
            return FALSE;
        }
    }
    return GWSystem.player_curr >= 0 && GWSystem.player_curr < 4;
}

void netplay_turn_resumed(void)
{
    if (resume_armed && netplay_turn_resuming()) {
        OSReport("netplay: the board resumes at player %d's turn\n", GWSystem.player_curr);
    }
    resume_armed = FALSE;
}

/*
 * N52: a desync inside a minigame went back to the start of the last turn before it (up to ~1.5 min). instDll starts
 * from GWSystem/GWPlayer alone (mg_next, the flags, the players), as it does after any board hands off to it, so the
 * same blob taken as it starts is a point to resume into it. Only the end-of-round minigame (flag (1,2)): battle and
 * Bowser minigames come back into the board's event code mid-turn, which the turn point before them covers.
 */
void netplay_minigame_point(void)
{
    static u8 blob[NETPLAY_BLOB_MAX];
    u32 size;
    s32 i;

    if (!netplay_match_running() || !pbnet_lockstep_active() || !_CheckFlag(FLAG_ID_MAKE(1, 2))
        || _CheckFlag(FLAG_ID_MAKE(1, 3)) || _CheckFlag(FLAG_ID_MAKE(1, 4))) {
        return;
    }
    size = netplay_board_blob_get(blob, sizeof(blob) - NETPLAY_MG_TRAILER_SIZE);
    if (size == 0) {
        return;
    }
    memcpy(&blob[size], NETPLAY_MG_MARK, NETPLAY_MG_MARK_SIZE);
    size += NETPLAY_MG_MARK_SIZE;
    /* A 1-vs-3 or 2-vs-2 minigame reads its sides here (m420 crashed at its setup with every group 0). */
    for (i = 0; i < 4; i++) {
        blob[size++] = (u8)GWPlayerCfg[i].group;
    }
    if (pbnet_resync_point(blob, (int)size, GWSystem.turn, GWSystem.max_turn)) {
        OSReport("netplay: resync point at frame %u: minigame %d, turn %d/%d, groups %d%d%d%d\n", netplay_frame(),
            GWSystem.mg_next, GWSystem.turn, GWSystem.max_turn, GWPlayerCfg[0].group, GWPlayerCfg[1].group,
            GWPlayerCfg[2].group, GWPlayerCfg[3].group);
    }
}
