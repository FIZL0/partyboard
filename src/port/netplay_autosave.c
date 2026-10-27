/*
 * Local party autosave (N47): the board state at the start of every player's turn of an offline party, so a crash
 * or a closed window can go on from that turn.
 *
 * The state is the online saves' (N9): the SLSaveBoard blob (GWSystem + GWPlayer[4], big-endian as on the card),
 * written as <user dir>/local/party.json to a temporary file that is renamed over the last one. Continue is the game's
 * own: when the party menu (mentDll) is about to ask whether to continue the card's party, netplay_party_offer puts a
 * readable autosave into the card buffer's party board and turns the question on, so its "yes" SLLoadBoards the
 * autosave. The board then resumes the saved player's turn as an online continue does (netplay_turn_resume_arm, N46).
 * When the party's results end it, the autosave is deleted (netplay_party_finish).
 */

#include "dolphin.h"
#include "game/flag.h"
#include "game/gamework_data.h"
#include "game/saveload.h"
#include "port/netplay.h"

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define AUTOSAVE_DIR "local"
#define AUTOSAVE_NAME "party.json"
#define AUTOSAVE_FORMAT 1
#define BLOB_BEGIN offsetof(SaveBufData, system)

static char json[4096];
static BOOL offered; /* netplay_party_offer turned party_continue on (it was off) */

static const char *autosave_path(void)
{
    static char path[1100];
    snprintf(path, sizeof(path), "%s/" AUTOSAVE_DIR "/" AUTOSAVE_NAME, netplay_user_dir());
    return path;
}

void netplay_party_autosave(void)
{
    static const char digits[] = "0123456789abcdef";
    static u8 blob[NETPLAY_BLOB_MAX];
    char dir[1100];
    u32 size;
    u32 i;
    int n;

    /* A party (not story mode), not the board tutorial, and a user dir to write to. */
    if (GWPartyGet() != 1 || _CheckFlag(FLAG_ID_MAKE(1, 11)) || netplay_user_dir()[0] == '\0') {
        return;
    }
    size = netplay_board_blob_get(blob, sizeof(blob));
    if (size == 0) {
        return;
    }
    n = snprintf(json, sizeof(json),
        "{\n  \"format\": %d,\n  \"time\": %lld,\n  \"board\": %d,\n  \"turn\": %d,\n  \"max_turn\": %d,\n"
        "  \"player\": %d,\n  \"blob\": \"",
        AUTOSAVE_FORMAT, (long long)time(NULL), GWSystem.board, GWSystem.turn, GWSystem.max_turn, GWSystem.player_curr);
    if (n < 0 || (u32)n + 2 * size + 4 > sizeof(json)) {
        return;
    }
    for (i = 0; i < size; i++) {
        json[n++] = digits[blob[i] >> 4];
        json[n++] = digits[blob[i] & 15];
    }
    memcpy(&json[n], "\"\n}\n", 4);
    n += 4;
    snprintf(dir, sizeof(dir), "%s/" AUTOSAVE_DIR, netplay_user_dir());
    if (netplay_file_replace(dir, AUTOSAVE_NAME, json, (u32)n)) {
        OSReport("netplay: party autosave at turn %d/%d, player %d: %s\n", GWSystem.turn, GWSystem.max_turn,
            GWSystem.player_curr, autosave_path());
    } else {
        OSReport("netplay: party autosave could not be written: %s\n", autosave_path());
    }
}

static int hex_digit(char c)
{
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    }
    if (c >= 'A' && c <= 'F') {
        return c - 'A' + 10;
    }
    return -1;
}

/* The autosave's blob into out; its size, 0 if the file is missing, damaged or of another format. */
static u32 autosave_read(u8 *out, u32 cap)
{
    const char *p;
    u32 size = 0;
    int n;

    n = netplay_file_read(autosave_path(), json, sizeof(json) - 1);
    if (n <= 0) {
        return 0;
    }
    json[n] = '\0';
    p = strstr(json, "\"format\":");
    if (p == NULL || strtol(p + 9, NULL, 10) != AUTOSAVE_FORMAT) {
        return 0;
    }
    p = strstr(json, "\"blob\":");
    if (p == NULL || (p = strchr(p + 7, '"')) == NULL) {
        return 0;
    }
    for (p++; *p != '"'; p += 2) {
        int hi = hex_digit(p[0]);
        int lo = hi < 0 ? -1 : hex_digit(p[1]);
        if (lo < 0 || size >= cap) {
            return 0;
        }
        out[size++] = (u8)(hi << 4 | lo);
    }
    return size;
}

void netplay_party_offer(void)
{
    static u8 blob[NETPLAY_BLOB_MAX];
    u32 size;
    s32 board;

    size = autosave_read(blob, sizeof(blob));
    if (size == 0) {
        return;
    }
    board = netplay_board_blob_board(blob, size);
    if (board < 0 || board >= NETPLAY_BOARDS) {
        OSReport("netplay: party autosave is no party board blob, not offered: %s\n", autosave_path());
        return;
    }
    memcpy(&saveBuf.buf[BLOB_BEGIN], blob, size);
    if (GWGameStat.party_continue != 1) {
        GWGameStat.party_continue = 1;
        offered = TRUE;
    }
    netplay_turn_resume_arm();
    OSReport("netplay: party autosave offered (board %d): %s\n", board, autosave_path());
}

void netplay_party_finish(void)
{
    if (netplay_match_running() || GWPartyGet() != 1) {
        return;
    }
    if (netplay_file_remove(autosave_path())) {
        OSReport("netplay: party over: its autosave is deleted\n");
    }
    /* A question the autosave turned on must not outlive it (e.g. a party played without card saves). */
    if (offered) {
        GWGameStat.party_continue = 0;
        offered = FALSE;
    }
}
