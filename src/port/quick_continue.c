/*
 * N132: Party Board menu > Continue (include/port/quick_continue.h). The resume is the menus' own, step by step:
 *  - file select (modeseldll filesel.c fn_1_37DC/FileSelectAutoLoadDefault): the card file into saveBuf, its GameStat;
 *  - mode select (modeseldll main.c fn_1_414, Party or Story): the pads, the pause options, flags (1,0/8/11/12);
 *  - mentDll's continue: party fn_1_85C8 (netplay_party_offer, SLLoadBoard, saveExecF 1) then fn_1_C174(1), story
 *    fn_1_C4B8 (SLLoadBoardStory and its branches); the board data preload of fn_1_7218.
 * The overlay history is the menus' (boot -> modesel -> mentDll, which renames its entry to mstory3dll before the
 * board), so the board's end goes to the party results and back to the mode select as it would.
 */

#include "dolphin.h"
#include "game/armem.h"
#include "game/audio.h"
#include "game/board/main.h"
#include "game/board/tutorial.h"
#include "game/card.h"
#include "game/chrman.h"
#include "game/data.h"
#include "game/flag.h"
#include "game/gamework.h"
#include "game/gamework_data.h"
#include "game/minigame_seq.h"
#include "game/object.h"
#include "game/objsub.h"
#include "game/pad.h"
#include "game/process.h"
#include "game/saveload.h"
#include "msm/msmsys.h"
#include "port/byteswap.h"
#include "port/home_screen.h"
#include "port/netplay.h"
#include "port/port_version.h"
#include "port/quick_continue.h"
#include "port/ui.h"

#include <stdlib.h>
#include <string.h>

SHARED_SYM extern s32 omovlhisidx;

static struct {
    BOOL requested;
    s32 kind;
    s32 box;
    BOOL loaded;
    BOOL offered;
} pending;

/* The checksum SLCheckSumCheck tests: SLCheckSumGet's, stored big-endian after the data. */
static BOOL file_checksum_ok(const u8 *buf)
{
    u32 i;
    u32 sum = 0;
    u16 want;

    for (i = 0; i < sizeof(SaveBufData); i++) {
        sum += buf[i];
    }
    want = (u16)(~sum & 0xFFFF);
    return buf[sizeof(SaveBufData)] == (want >> 8) && buf[sizeof(SaveBufData) + 1] == (want & 0xFF);
}

static void entry_from(QuickContinueEntry *e, s32 kind, s32 box, const SystemState *sys, const PlayerState *players)
{
    s32 i;

    memset(e, 0, sizeof(*e));
    e->kind = kind;
    e->box = box;
    e->board = sys->board;
    e->turn = sys->turn;
    e->max_turn = sys->max_turn;
    for (i = 0; i < 4; i++) {
        e->character[i] = players[i].character;
    }
}

s32 quick_continue_parse_file(const u8 *buf, s32 box, QuickContinueEntry *out)
{
    static SaveBufAll file;
    s32 n = 0;
    s32 i;

    if (!file_checksum_ok(buf)) {
        return 0;
    }
    memcpy(file.buf, buf, SAVE_BUF_SIZE);
    byteswap_gamestat(&file.data.stat);
    if (file.data.stat.party_continue) {
        byteswap_systemstate(&file.data.system);
        for (i = 0; i < 4; i++) {
            byteswap_playerstate(&file.data.player[i]);
        }
        entry_from(&out[n++], QUICK_CONTINUE_PARTY, box, &file.data.system, file.data.player);
    }
    if (file.data.stat.story_continue) {
        byteswap_systemstate(&file.data.systemStory);
        for (i = 0; i < 4; i++) {
            byteswap_playerstate(&file.data.playerStory[i]);
        }
        entry_from(&out[n++], QUICK_CONTINUE_STORY, box, &file.data.systemStory, file.data.playerStory);
    }
    return n;
}

BOOL quick_continue_parse_blob(const u8 *blob, u32 size, QuickContinueEntry *out)
{
    static SystemState sys;
    static PlayerState players[4];
    s32 i;

    if (size != sizeof(SystemState) + sizeof(players)) {
        return FALSE;
    }
    memcpy(&sys, blob, sizeof(sys));
    memcpy(players, blob + sizeof(sys), sizeof(players));
    byteswap_systemstate(&sys);
    for (i = 0; i < 4; i++) {
        byteswap_playerstate(&players[i]);
    }
    if (sys.board < 0 || sys.board >= NETPLAY_BOARDS) {
        return FALSE;
    }
    entry_from(out, QUICK_CONTINUE_PARTY, -1, &sys, players);
    out->autosave = TRUE;
    return TRUE;
}

/* The card file an autosave belongs to: its own "box" if that file is on the card, else the first card file. */
static s32 autosave_box(s32 box, const u8 *const files[3])
{
    s32 i;

    if (box >= 0 && box < 3 && files[box] != NULL && file_checksum_ok(files[box])) {
        return box;
    }
    for (i = 0; i < 3; i++) {
        if (files[i] != NULL && file_checksum_ok(files[i])) {
            return i;
        }
    }
    return -1;
}

s32 quick_continue_list(const u8 *const files[3], QuickContinueEntry out[QUICK_CONTINUE_MAX])
{
    static u8 blob[NETPLAY_BLOB_MAX];
    QuickContinueEntry file[2];
    BOOL autosave = FALSE;
    s32 n = 0;
    s32 box;
    s32 i, j, k;
    u32 size;

    size = netplay_party_autosave_peek(blob, sizeof(blob), &box);
    if (size != 0 && quick_continue_parse_blob(blob, size, &out[0])) {
        out[0].box = autosave_box(box, files);
        autosave = TRUE;
        n = 1;
    }
    for (i = 0; i < 3; i++) {
        if (files[i] == NULL) {
            continue;
        }
        k = quick_continue_parse_file(files[i], i, file);
        for (j = 0; j < k; j++) {
            /* With an autosave, mentDll's continue of any file is the autosave's party (netplay_party_offer). */
            if (file[j].kind == QUICK_CONTINUE_PARTY && autosave) {
                continue;
            }
            out[n++] = file[j];
        }
    }
    return n;
}

void quick_continue_offer(BOOL on)
{
    pending.offered = on;
}

void QuickContinueHold(void)
{
    /* N141: the boot waits for the menu whether it offers Continue or not (its music plays meanwhile) */
    port_home_screen_hold();
    pending.offered = FALSE;
}

void quick_continue_request(s32 kind, s32 box)
{
    pending.requested = TRUE;
    pending.kind = kind;
    pending.box = box;
}

/* PB_CONTINUE=party|story[:box] (test runs, no launcher): as if picked in the launcher. Once per process: N134, a
 * reboot to the launcher (N133) boots at event 0 again and must not continue by itself. */
static void request_from_env(void)
{
    static BOOL read;
    const char *env = getenv("PB_CONTINUE");
    const char *colon;

    if (read) {
        return;
    }
    read = TRUE;
    if (env == NULL || env[0] == '\0') {
        return;
    }
    colon = strchr(env, ':');
    if (strncmp(env, "party", 5) == 0) {
        quick_continue_request(QUICK_CONTINUE_PARTY, colon != NULL ? atoi(colon + 1) : -1);
    }
    else if (strncmp(env, "story", 5) == 0) {
        quick_continue_request(QUICK_CONTINUE_STORY, colon != NULL ? atoi(colon + 1) : -1);
    }
    else {
        OSReport("continue: PB_CONTINUE wants party|story[:box], not '%s'\n", env);
    }
}

/* filesel.c's read of a card file into saveBuf (slot A): TRUE when it is there and its checksum is right. */
static BOOL card_file_read(s32 box)
{
    s32 result;

    SLCurSlotNoSet(0);
    SLCurBoxNoSet(box);
    result = HuCardMount(curSlotNo);
    if (result == CARD_RESULT_READY) {
        SLSerialNoGet();
        result = HuCardOpen(curSlotNo, SaveFileNameTbl[box], &curFileInfo);
        if (result == CARD_RESULT_READY) {
            result = HuCardRead(&curFileInfo, &saveBuf.buf[0], SAVE_BUF_SIZE, 0);
            HuCardClose(&curFileInfo);
        }
        HuCardUnMount(curSlotNo);
    }
    return result == CARD_RESULT_READY && SLCheckSumCheck();
}

/* The card file of a request without one (PB_CONTINUE): the autosave's for a party, else the first in progress. */
static s32 card_box_find(s32 kind)
{
    static u8 files[3][SAVE_BUF_SIZE];
    const u8 *list[3];
    QuickContinueEntry entries[QUICK_CONTINUE_MAX];
    s32 n, i;

    for (i = 0; i < 3; i++) {
        list[i] = card_file_read(i) ? memcpy(files[i], saveBuf.buf, SAVE_BUF_SIZE) : NULL;
    }
    n = quick_continue_list(list, entries);
    for (i = 0; i < n; i++) {
        if (entries[i].kind == kind) {
            return entries[i].box;
        }
    }
    return -2;
}

/* What the file select leaves (FileSelectAutoLoadDefault): the file's GameStat, or a fresh one without a file. */
static BOOL file_load(s32 box)
{
    if (box < 0) {
        SLCurSlotNoSet(0);
        SLCurBoxNoSet(0);
        SLSaveFlagSet(0);
        GWGameStatReset();
        if (partyboard_version_is_pal()) {
            _ClearFlag(0x1000B);
        }
        GWGameStat.sound_mode = msmSysGetOutputMode();
        return TRUE;
    }
    if (!card_file_read(box)) {
        return FALSE;
    }
    SLSaveFlagSet(1);
    SLLoadGameStat();
    if (partyboard_version_is_pal()) {
        if (GwLanguageSave != -1) {
            GWGameStat.language = GwLanguageSave;
        }
        GwLanguage = GWGameStat.language;
        _ClearFlag(0x1000B);
    }
    return TRUE;
}

BOOL QuickContinueLoad(void)
{
    static GameStat stat;
    s32 box;
    s32 save_flag;

    request_from_env();
    /* Offline only: --script runs (netplay_active) are offline too, a lockstep session or match is not. */
    if (!pending.requested || netplay_lockstep() || netplay_match_running()) {
        return FALSE;
    }
    pending.requested = FALSE;
    box = pending.box;
    stat = GWGameStat;
    save_flag = SLSaveFlagGet();
    if (box == -1) {
        box = card_box_find(pending.kind);
    }
    if (box >= -1 && box < 3 && file_load(box)) {
        if (pending.kind == QUICK_CONTINUE_PARTY) {
            /* mentDll fn_1_85C8: the autosave (if any) is the party mentDll asks to continue. */
            netplay_party_offer();
            pending.loaded = GWGameStat.party_continue == 1;
        }
        else {
            pending.loaded = GWGameStat.story_continue == 1;
        }
    }
    if (!pending.loaded) {
        OSReport("continue: no %s in progress in card file %d, booting as usual\n",
            pending.kind == QUICK_CONTINUE_PARTY ? "party" : "story", (int)box);
        GWGameStat = stat;
        SLSaveFlagSet(save_flag);
        return FALSE;
    }
    pending.box = box;
    return TRUE;
}

/* mentDll fn_1_7218: the common board data into ARAM, the board's own read ahead. */
static void board_data_read(void)
{
    s32 stat = HuDataDirReadAsync(DATADIR_BOARD);
    static const s32 dirs[7] = { DATADIR_W01, DATADIR_W02, DATADIR_W03, DATADIR_W04, DATADIR_W05, DATADIR_W06,
        DATADIR_W10 };

    if (stat != -1) {
        while (HuDataGetAsyncStat(stat) == 0) {
            HuPrcVSleep();
        }
    }
    HuAR_MRAMtoARAM(DATADIR_BOARD);
    while (HuARDMACheck() != 0) {
        HuPrcVSleep();
    }
    HuDataDirClose(DATADIR_BOARD);
    if (_CheckFlag(FLAG_ID_MAKE(1, 11)) != 0) {
        stat = HuDataDirReadAsync(DATADIR_W10);
    }
    else {
        stat = HuDataDirReadAsync(dirs[GWSystem.board]);
    }
    if (stat != -1) {
        while (HuDataGetAsyncStat(stat) == 0) {
            HuPrcVSleep();
        }
    }
}

/* modeseldll main.c fn_1_414 after the mode select, and its ObjectSetup's minigame flags. */
static void mode_select(void)
{
    s32 i;
    const PauseBackupConfig *pause;

    for (i = 0; i < 4; i++) {
        GWPlayerCfg[i].pad_idx = i;
        GWPlayerCfg[i].iscom = HuPadStatGet(i) ? 1 : 0;
    }
    mgBoardHostEnableF = 0;
    mgPracticeEnableF = 0;
    mgQuitExtraF = 0;
    _ClearFlag(FLAG_ID_MAKE(1, 0));
    _ClearFlag(FLAG_ID_MAKE(1, 8));
    _ClearFlag(FLAG_ID_MAKE(1, 12));
    _ClearFlag(FLAG_ID_MAKE(1, 11));
    GWGameStat.sound_mode = msmSysGetOutputMode();
    pause = pending.kind == QUICK_CONTINUE_PARTY ? &GWGameStat.party_pause : &GWGameStat.story_pause;
    GWMGExplainSet(pause->explain_mg);
    GWMGShowComSet(pause->show_com_mg);
    GWMGListSet(pause->mg_list);
    GWMessSpeedSet(pause->mess_speed);
    GWSaveModeSet(pause->save_mode);
    /* mentDll ObjectSetup (common.c). */
    _ClearFlag(0x1000B);
    boardTutorialF = 0;
}

/* The history of a party or story game started from the menus: boot -> modesel -> mentDll (party 0, story 1). */
static void history_set(void)
{
    omovlhisidx = 2;
    omOvlHisChg(2, DLL_bootdll, 1, 0);
    omOvlHisChg(1, DLL_modeseldll, 1, 0);
    omOvlHisChg(0, DLL_mentdll, pending.kind == QUICK_CONTINUE_PARTY ? 0 : 1, 0);
}

/* mentDll fn_1_C174(1) / fn_1_E71C: the board, from an entry renamed to mstory3dll (the party results). */
static void board_call(void)
{
    s32 i;

    CharModelKill(-1);
    MGSeqKillAll();
    omOvlHisChg(0, DLL_mstory3dll, 0, 0);
    board_data_read();
    for (i = 0; i < 4; i++) {
        CharMotionInit(GWPlayerCfg[i].character);
    }
    HuAudSeqAllFadeOut(1000);
    HuAudSStreamAllFadeOut(1000);
    OSReport("continue: %s board %d, turn %d/%d, card file %d\n",
        pending.kind == QUICK_CONTINUE_PARTY ? "party" : "story", GWSystem.board, GWSystem.turn, GWSystem.max_turn,
        (int)pending.box);
    omOvlCallEx(GWSystem.board == 6 ? DLL_w10dll : DLL_w01dll + GWSystem.board, 1, 0, 0);
}

/* mentDll fn_1_E654: the story events' exit. */
static void story_event_exit(s32 one)
{
    CharModelKill(-1);
    MGSeqKillAll();
    HuAudAllStop();
    CharMotionInit(GWPlayerCfg[0].character);
    if (one == 0) {
        CharMotionInit(GWPlayerCfg[1].character);
        CharMotionInit(GWPlayerCfg[2].character);
        CharMotionInit(GWPlayerCfg[3].character);
    }
}

static void story_goto(s32 one, OMOVL ovl, s32 event, const char *what)
{
    OSReport("continue: story, %s (board %d, card file %d)\n", what, GWSystem.board, (int)pending.box);
    story_event_exit(one);
    omOvlGotoEx(ovl, 1, event, 0);
}

/* mentDll fn_1_C354: the story board is cleared. */
static BOOL story_board_cleared(s32 board)
{
    return board >= 0 && board <= 5 && _CheckFlag(FLAG_ID_MAKE(0, board + 2)) != 0;
}

/* mentDll fn_1_C440: the five boards before Bowser's are cleared. */
static BOOL story_five_cleared(void)
{
    s32 i;

    for (i = 0; i < 5; i++) {
        if (!story_board_cleared(i)) {
            return FALSE;
        }
    }
    return TRUE;
}

/* mentDll fn_1_C4B8's "yes" (main.c:2068-2133). */
static void story_continue(void)
{
    SLLoadBoardStory();
    if (_CheckFlag(FLAG_ID_MAKE(0, 7)) != 0 && _CheckFlag(FLAG_ID_MAKE(0, 9)) != 0) {
        story_goto(1, DLL_mstory2dll, 4, "next to the ending");
    }
    else if (_CheckFlag(0x10000) != 0) {
        board_call();
    }
    else if (story_board_cleared(GWSystem.board)) {
        if (_CheckFlag(FLAG_ID_MAKE(0, 9)) != 0) {
            if (story_five_cleared()) {
                story_goto(1, DLL_mstory2dll, 0, "next to the map select, Koopa event");
            }
            else {
                story_goto(1, DLL_mentdll, 0xA, "next to the map select");
            }
        }
        else if (GWSystem.board == 5) {
            story_goto(0, DLL_mstory2dll, 1, "next to the board clear, Koopa event");
        }
        else {
            story_goto(0, DLL_mstorydll, 0, "next to the board clear");
        }
    }
    else if (_CheckFlag(FLAG_ID_MAKE(0, 9)) != 0) {
        if (GWSystem.board == 5) {
            story_goto(0, DLL_mstory2dll, 2, "next to the board miss, Koopa event");
        }
        else {
            story_goto(0, DLL_mstorydll, 1, "next to the board miss");
        }
    }
    else {
        board_call();
    }
}

void QuickContinueStart(void)
{
    pending.loaded = FALSE;
    mode_select();
    history_set();
    if (pending.kind == QUICK_CONTINUE_PARTY) {
        /* mentDll fn_1_85C8's "yes". */
        saveExecF = 1;
        SLLoadBoard();
        board_call();
    }
    else {
        saveExecF = 0;
        story_continue();
    }
}
