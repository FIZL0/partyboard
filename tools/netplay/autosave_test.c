/* N47: the per-turn checkpoint (src/port/netplay_resync.c) and the local party autosave (src/port/netplay_autosave.c)
 * against a fake SLSaveBoard/SLLoadBoard and a fake pbnet, with the real file helpers (netplay_dir.cpp) in a
 * temporary user dir. Run with tools/netplay/autosave_test.sh. */
#include "dolphin.h"
#include "game/flag.h"
#include "game/gamework_data.h"
#include "game/object.h"
#include "game/saveload.h"
#include "port/netplay.h"
#include "port/pbnet.h"
#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

GameStat GWGameStat; SystemState GWSystem; PlayerState GWPlayer[4]; PlayerConfig GWPlayerCfg[4];
SaveBufAll saveBuf; s32 rnd_seed = 1; u32 boardRandSeed = 2;
void OSReport(const char *fmt, ...) { va_list a; va_start(a, fmt); vprintf(fmt, a); va_end(a); }
void autosave_test_set_dir(const char *d);

static u8 flags[3][64];
s32 _CheckFlag(u32 f) { return flags[f >> 16][f & 0x3F]; }
static BOOL active, running, lockstep;
BOOL netplay_active(void) { return active; }
BOOL netplay_match_running(void) { return running; }
/* N64: an online match's results after its session; a turn start is a gameplay point (seeds again). */
static BOOL results; static int point_reseeds;
BOOL netplay_match_results(void) { return results; }
OMOVL omcurovl;
void netplay_point_reseed(u32 point, s32 ovl) { assert(point == NETPLAY_POINT_TURN); (void)ovl; point_reseeds++; }
u32 netplay_frame(void) { return 77; }
u32 netplay_session_seed(void) { return 3; }
u32 frandSeedGet(void) { return 4; }
int netplay_match_room_json(char *buf, int cap) { return snprintf(buf, (size_t)cap, "{}"); }
int pbnet_lockstep_active(void) { return lockstep; }

static int writes, points;
int pbnet_save_write(const char *dir, const char *room, const uint8_t *blob, int len, uint32_t frame, const uint32_t *seeds,
    char *path, int cap)
{
    (void)dir; (void)room; (void)blob; (void)len; (void)frame; (void)seeds;
    writes++;
    snprintf(path, (size_t)cap, "x.json");
    return 1;
}
static u8 point_blob[NETPLAY_BLOB_MAX];
int pbnet_resync_point(const uint8_t *blob, int len, int turn, int max_turn)
{
    (void)turn; (void)max_turn;
    memcpy(point_blob, blob, (size_t)len);
    points++;
    return 1;
}
int pbnet_save_finish(const char *dir, const char *room) { (void)dir; (void)room; return 0; }
int pbnet_save_delete(const char *path) { (void)path; return 0; }

#define SAVE_PLAYER(i) &saveBuf.buf[(i) * sizeof(PlayerState) + offsetof(SaveBufData, player)]
void SLSaveBoard(void)
{
    s32 i;
    memcpy(&saveBuf.data.system, &GWSystem, sizeof(SystemState));
    for (i = 0; i < 4; i++) {
        memcpy(SAVE_PLAYER(i), &GWPlayer[i], sizeof(PlayerState));
    }
}
void SLLoadBoard(void)
{
    s32 i;
    memcpy(&GWSystem, &saveBuf.data.system, sizeof(SystemState));
    for (i = 0; i < 4; i++) {
        memcpy(&GWPlayer[i], SAVE_PLAYER(i), sizeof(PlayerState));
    }
}

static char path[1024], tmp[1100];

static BOOL exists(const char *p)
{
    struct stat st;
    return stat(p, &st) == 0;
}

static void party_at(s32 turn, s32 player)
{
    memset(&GWSystem, 0, sizeof(GWSystem));
    memset(GWPlayer, 0, sizeof(GWPlayer));
    GWSystem.party = 1;
    GWSystem.board = 3;
    GWSystem.turn = (u8)turn;
    GWSystem.max_turn = 20;
    GWSystem.player_curr = (s8)player;
    GWPlayer[player].coins = (u16)(10 + turn);
    flags[1][1] = 1;
}

int main(void)
{
    const char *dir = getenv("AUTOSAVE_TEST_DIR");
    static u8 card[sizeof(saveBuf)];
    FILE *f;

    assert(dir != NULL);
    autosave_test_set_dir(dir);
    snprintf(path, sizeof(path), "%s/local/party.json", dir);
    snprintf(tmp, sizeof(tmp), "%s.tmp", path);
    memset(saveBuf.buf, 0x5A, sizeof(saveBuf.buf));
    memcpy(card, saveBuf.buf, sizeof(card));

    /* Offline party: every turn start writes the autosave (replacing the last), not an online save; the card buffer
     * is unchanged. */
    party_at(1, 0);
    netplay_turn_checkpoint();
    assert(exists(path) && !exists(tmp) && writes == 0 && points == 0 && point_reseeds == 0);
    party_at(4, 2);
    netplay_turn_checkpoint();
    assert(exists(path) && !exists(tmp) && memcmp(card, saveBuf.buf, sizeof(card)) == 0);
    {
        static char json[4096];
        f = fopen(path, "rb");
        json[fread(json, 1, sizeof(json) - 1, f)] = '\0';
        fclose(f);
        assert(strstr(json, "\"turn\": 4,") && strstr(json, "\"player\": 2,") && strstr(json, "\"format\": 1,"));
    }

    /* Story mode and the tutorial board write nothing. */
    remove(path);
    party_at(5, 1);
    GWSystem.party = 0;
    netplay_turn_checkpoint();
    assert(!exists(path));
    party_at(5, 1);
    flags[1][11] = 1;
    netplay_turn_checkpoint();
    assert(!exists(path));
    flags[1][11] = 0;

    /* The party menu offers it: the card's party board gets the blob, the question is turned on, and the board is
     * armed to resume the saved player's turn. */
    party_at(4, 2);
    netplay_turn_checkpoint();
    party_at(9, 0);
    GWPlayer[2].coins = 0;
    GWGameStat.party_continue = 0;
    netplay_party_offer();
    assert(GWGameStat.party_continue == 1);
    SLLoadBoard(); /* mentDll's Continue */
    assert(GWSystem.turn == 4 && GWSystem.player_curr == 2 && GWPlayer[2].coins == 14 && GWSystem.board == 3);
    assert(netplay_turn_resuming());
    netplay_turn_resumed();
    assert(!netplay_turn_resuming());

    /* The party's end deletes it and turns the question it turned on off again; nothing is offered after. */
    netplay_party_finish();
    assert(!exists(path) && GWGameStat.party_continue == 0);
    memcpy(saveBuf.buf, card, sizeof(card));
    netplay_party_offer();
    assert(GWGameStat.party_continue == 0 && memcmp(card, saveBuf.buf, sizeof(card)) == 0);

    /* A card that already asks keeps asking after the end (the game clears its own question). */
    party_at(2, 1);
    netplay_turn_checkpoint();
    GWGameStat.party_continue = 1;
    netplay_party_offer();
    netplay_party_finish();
    assert(GWGameStat.party_continue == 1);

    /* Damaged files and other formats are not offered. */
    {
        static const char *bad[] = {
            "{\"format\": 2, \"blob\": \"00\"}",
            "{\"format\": 1, \"blob\": \"0g\"}",
            "{\"format\": 1, \"blob\": \"0011\"}", /* too short for a board blob */
            "{\"format\": 1}",
            "",
        };
        u32 i;
        for (i = 0; i < sizeof(bad) / sizeof(bad[0]); i++) {
            f = fopen(path, "wb");
            fputs(bad[i], f);
            fclose(f);
            GWGameStat.party_continue = 0;
            memcpy(saveBuf.buf, card, sizeof(card));
            netplay_party_offer();
            assert(GWGameStat.party_continue == 0 && memcmp(card, saveBuf.buf, sizeof(card)) == 0);
        }
        remove(path);
    }

    /* During a match every turn start is an online save (and a resync point with lockstep), never the autosave. */
    running = active = TRUE;
    party_at(3, 1);
    netplay_turn_checkpoint();
    assert(writes == 1 && points == 0 && !exists(path) && point_reseeds == 1);
    lockstep = TRUE;
    party_at(3, 2);
    netplay_turn_checkpoint();
    assert(writes == 2 && points == 1 && !exists(path));
    {
        static u8 last[NETPLAY_BLOB_MAX];
        u32 n = netplay_checkpoint_last(last, sizeof(last));
        assert(n == netplay_board_blob_size() && memcmp(last, point_blob, n) == 0);
    }
    /* The party end of a match is the online save's (N31), not the autosave's. */
    party_at(3, 2);
    running = active = lockstep = FALSE;
    netplay_turn_checkpoint();
    assert(exists(path));
    running = TRUE;
    netplay_party_finish();
    assert(exists(path));
    /* N64: nor are an online match's results, which run on after its session. */
    running = FALSE;
    results = TRUE;
    netplay_party_finish();
    assert(exists(path));
    results = FALSE;

    /* netplay_turn_hooks: on while netplay runs, or in any party. */
    running = FALSE;
    GWSystem.party = 0;
    assert(!netplay_turn_hooks());
    active = TRUE;
    assert(netplay_turn_hooks());
    active = FALSE;
    GWSystem.party = 1;
    assert(netplay_turn_hooks());

    printf("autosave_test: ok\n");
    return 0;
}
