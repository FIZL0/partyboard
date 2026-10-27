/* N33 scratch host: hosts a pbnet lobby, writes its ticket to argv[1], starts once the guest is ready, steps
 * lockstep with idle pads at 60 Hz. Each line on stdin "stall <ms>" stops stepping that long (the guest WAITs). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <time.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
struct PADStatus { uint16_t button; int8_t stickX, stickY, substickX, substickY; uint8_t triggerL, triggerR, analogA, analogB; int8_t err; uint32_t extButton; };
#include "port/pbnet.h"
static char buf[65536];
static double now(void){struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return t.tv_sec+t.tv_nsec/1e9;}
int main(int argc, char **argv) {
    const char *build = argv[2];
    char setup[512];
    snprintf(setup, sizeof setup, "{\"build\":\"%s\",\"disc\":\"GMPE01-01\",\"players\":[{\"name\":\"Hosty\",\"key\":\"hostykey\"}],\"relay\":\"off\",\"short\":false}", build);
    if (!pbnet_lobby_host(setup)) { fprintf(stderr, "host failed\n"); return 1; }
    int wrote = 0, started = 0;
    fcntl(0, F_SETFL, O_NONBLOCK);
    double next = now(); uint32_t steps = 0, waits = 0; double stall_until = 0;
    for (;;) {
        if (!started) {
            pbnet_lobby_json(buf, sizeof buf);
            if (!wrote && strstr(buf, "\"state\":\"open\"")) pbnet_lobby_settings(0, 10, 0, 0, NULL);
            if (!wrote) {
                char *c = strstr(buf, "\"code\":\"");
                if (c) { c += 8; char *e = strchr(c, '"'); if (e > c) { FILE *f = fopen(argv[1], "w"); fwrite(c, 1, e - c, f); fclose(f); wrote = 1; fprintf(stderr, "code written\n"); } }
            }
            if (strstr(buf, "\"ready\":[\"")) {
                char why[256] = "";
                if (pbnet_lobby_start(12345, why, sizeof why)) { started = 1; fprintf(stderr, "started\n"); }
                else fprintf(stderr, "start: %s\n", why);
            }
            usleep(100000);
            continue;
        }
        char line[64]; ssize_t n = read(0, line, sizeof line - 1);
        if (n > 0) { line[n] = 0; int ms; if (sscanf(line, "stall %d", &ms) == 1) { stall_until = now() + ms / 1000.0; fprintf(stderr, "stall %d ms at frame %u\n", ms, pbnet_frame()); } }
        double t = now();
        if (t < stall_until) { usleep(2000); next = t; continue; }
        if (t < next) { usleep((useconds_t)((next - t) * 1e6)); continue; }
        next += 1.0 / 60;
        struct PADStatus local[1] = {0}, out[4];
        if (!pbnet_lockstep_active()) { fprintf(stderr, "lockstep ended\n"); return 0; }
        int r = pbnet_step(local, 1, out);
        if (r == PBNET_READY) { if (++steps % 600 == 0) fprintf(stderr, "frame %u waits %u\n", pbnet_frame(), waits); }
        else { waits++; next = now(); }
    }
}
