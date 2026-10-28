// N104: the stick-only waits a human seat can meet in a party, for tools/web_pair.mjs's fake gamepad (it taps only
// buttons, so a human seat there waits for ever): the board lottery's ball spin and ticket scratch, and m444's plunger.
// Read only; the page moves its pad's stick as a player would.
#include "game/gamework_data.h"
#include "port/netplay.h"

#include <emscripten.h>

s32 BoardLotteryNetplayStickGame(void);

// 1: the ball game (spun with the stick), 2: the ticket scratch (the hand moved with the stick), 0: neither, or the
// current player is a COM. As --script's `lottery-stick on` (src/port/netplay.c), which plays the same games.
EMSCRIPTEN_KEEPALIVE int pb_web_lottery_stick(void)
{
    s32 game = BoardLotteryNetplayStickGame();
    s32 cur = GWSystem.player_curr;

    if (game == 0 || cur < 0 || cur >= 4 || GWPlayer[cur].com) {
        return 0;
    }
    return game;
}

// The pad m444's plunger waits on (pulled with the stick down, shot with A), -1 if it doesn't wait. As --script's
// `plunger-stick on`.
EMSCRIPTEN_KEEPALIVE int pb_web_plunger_pad(void)
{
    return netplay_plunger_waiting();
}
