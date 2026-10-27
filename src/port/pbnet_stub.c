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

void pbnet_lockstep_finish(void) { }

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

int pbnet_desync_recover(uint32_t frame, int peer, uint64_t ours, uint64_t theirs)
{
    (void)frame;
    (void)peer;
    (void)ours;
    (void)theirs;
    return 0;
}

int pbnet_resync_poll(void)
{
    return 0;
}

int pbnet_resync_point(const uint8_t *blob, int len, int turn, int max_turn)
{
    (void)blob;
    (void)len;
    (void)turn;
    (void)max_turn;
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

int pbnet_lobby_back(void)
{
    return 0;
}

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

/* N9 online saves: no lobby, so no continue and nothing written; netplay_checkpoint logs the failed write. */
int pbnet_lobby_checkpoint(uint8_t *buf, int cap)
{
    (void)buf;
    (void)cap;
    return 0;
}

int pbnet_save_write(const char *user_dir, const char *room_json, const uint8_t *blob, int len, uint32_t frame,
                     const uint32_t *seeds, char *path, int cap)
{
    (void)user_dir;
    (void)room_json;
    (void)blob;
    (void)len;
    (void)frame;
    (void)seeds;
    (void)path;
    (void)cap;
    return 0;
}

int pbnet_save_list(const char *user_dir, char *buf, int cap)
{
    static const char empty[] = "[]";
    int i;
    (void)user_dir;
    if (buf != NULL && cap > 0) {
        for (i = 0; i < cap - 1 && i < 2; i++) {
            buf[i] = empty[i];
        }
        buf[i] = '\0';
    }
    return 2;
}

int pbnet_save_blob(const char *path, uint8_t *buf, int cap)
{
    (void)path;
    (void)buf;
    (void)cap;
    return 0;
}

int pbnet_save_delete(const char *path)
{
    (void)path;
    return 0;
}

int pbnet_save_finish(const char *user_dir, const char *room_json)
{
    (void)user_dir;
    (void)room_json;
    return 0;
}

/* LAN: no endpoint, so never any games. */
int pbnet_lan_browse(const char *setup, char *buf, int cap)
{
    (void)setup;
    return snprintf(buf, buf != NULL && cap > 0 ? (size_t)cap : 0, "%s",
                    "{\"state\":\"closed\",\"lan\":false,\"error\":\"netplay is not available in this build\",\"games\":[]}");
}

void pbnet_lan_stop(void) { }

/* N10 recovery: no lobby, so nothing to rejoin or reload. */
void pbnet_recover_set_dir(const char *dir)
{
    (void)dir;
}

int pbnet_rejoin_record(char *buf, int cap)
{
    (void)buf;
    (void)cap;
    return 0;
}

void pbnet_rejoin_forget(void) { }

int pbnet_lobby_rejoin(const char *setup)
{
    (void)setup;
    return 0;
}

int pbnet_recover_json(char *buf, int cap)
{
    static const char none[] = "{\"running\":false}";
    int i;
    if (buf != NULL && cap > 0) {
        for (i = 0; i < cap - 1 && none[i] != '\0'; i++) {
            buf[i] = none[i];
        }
        buf[i] = '\0';
    }
    return (int)sizeof(none) - 1;
}

uint32_t pbnet_recover_epoch(void)
{
    return 0;
}

int pbnet_recover_go(uint32_t epoch)
{
    (void)epoch;
    return 1;
}

int pbnet_recover_cpu(int index)
{
    (void)index;
    return 0;
}

#ifdef __ANDROID__
/* No HTTPS without the crate, so no certificate check to set up. */
int pbnet_android_init_tls(void *env, void *activity)
{
    (void)env;
    (void)activity;
    return 0;
}
#endif
