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
