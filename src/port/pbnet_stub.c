/*
 * pbnet for targets the Rust crate (net/) cannot be built for, linked by cmake/pbnet.cmake instead of it
 * (tvOS: see research/journal/2026-10-27-n17-pbnet-tvos). Every call behaves as the crate does with no
 * session running, so the game plays offline and netplay never starts.
 * Keep in sync with include/port/pbnet.h: cmake/pbnet.cmake checks every pbnet_* there is defined here.
 */

#include "port/pbnet.h"

#include <math.h>
#include <stddef.h>
#include <stdio.h>

const char *pbnet_version(void)
{
    return "none (not built for this target)";
}

int pbnet_lockstep_active(void)
{
    return 0;
}

void pbnet_lockstep_stop(void) { }

int pbnet_step(const struct PADStatus *local, int n_local, struct PADStatus *out)
{
    (void)local;
    (void)n_local;
    (void)out;
    return PBNET_OFF;
}

uint32_t pbnet_frame(void)
{
    return 0;
}

uint32_t pbnet_waiting_on(void)
{
    return 0;
}

void pbnet_set_hash(uint32_t frame, uint64_t hash)
{
    (void)frame;
    (void)hash;
}

int pbnet_poll_desync(uint32_t *frame, int *peer, uint64_t *ours, uint64_t *theirs)
{
    (void)frame;
    (void)peer;
    (void)ours;
    (void)theirs;
    return 0;
}

uint32_t pbnet_delay_for(uint32_t rtt_ms)
{
    /* Same double arithmetic as net/src/lockstep.rs delay_for, so both give the same frames. */
    double frames = ceil((double)rtt_ms / 1000.0 / 2.0 * 60.0) + 1.0;
    if (frames < 2.0) {
        return 2;
    }
    if (frames > 15.0) {
        return 15;
    }
    return (uint32_t)frames;
}

/* Lobby: never goes online, so the online menu says it can't host or join. */
int pbnet_lobby_host(const char *setup)
{
    (void)setup;
    return 0;
}

int pbnet_lobby_join(const char *setup, const char *code)
{
    (void)setup;
    (void)code;
    return 0;
}

void pbnet_lobby_leave(void) { }

int pbnet_lobby_state(void)
{
    return PBNET_LOBBY_NONE;
}

uint32_t pbnet_lobby_gen(void)
{
    return 0;
}

int pbnet_lobby_json(char *buf, int cap)
{
    return snprintf(buf, buf != NULL && cap > 0 ? (size_t)cap : 0, "%s", "{\"state\":\"none\"}");
}

int pbnet_lobby_picks(const uint8_t *characters, const uint8_t *teams, int n)
{
    (void)characters;
    (void)teams;
    (void)n;
    return 0;
}

int pbnet_lobby_ready(int ready)
{
    (void)ready;
    return 0;
}

int pbnet_lobby_settings(int board, int turns, int bonus, int teams, const uint8_t *handicaps)
{
    (void)board;
    (void)turns;
    (void)bonus;
    (void)teams;
    (void)handicaps;
    return 0;
}

int pbnet_lobby_cpu(int seat, int difficulty)
{
    (void)seat;
    (void)difficulty;
    return 0;
}

int pbnet_lobby_start(uint64_t seed, char *why, int cap)
{
    (void)seed;
    snprintf(why, why != NULL && cap > 0 ? (size_t)cap : 0, "%s", "netplay is not available in this build");
    return 0;
}
