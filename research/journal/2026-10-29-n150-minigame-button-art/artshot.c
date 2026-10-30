/* N150: renders each known button picture redrawn by button_art.cpp, without the game.
 * artshot <file.dat> <bmp index> <family> <out.rgba>: reads a decoded data file (unpack.c), finds bitmap <bmp index>
 * (ANM or HSF), calls pb_button_art_test_render with aurora's default bindings, writes w, h (u32 LE) + RGBA rows.
 * Build: cc artshot.c -L<build> -ldol -Wl,-rpath,<build> -Wl,--unresolved-symbols=ignore-all */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int pb_button_art_test_render(int kind, int fmt, int pix_size, int w, int h, const void *data, const void *pal,
    int pal_num, int family, int port, uint8_t *out, uint32_t cap, int *ow, int *oh);

static uint32_t be32(const uint8_t *p) { return (uint32_t)p[0] << 24 | p[1] << 16 | p[2] << 8 | p[3]; }
static int16_t be16(const uint8_t *p) { return (int16_t)(p[0] << 8 | p[1]); }

int main(int argc, char **argv)
{
    FILE *f = fopen(argv[1], "rb");
    if (!f) return 2;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *b = malloc(n);
    fread(b, 1, n, f);
    fclose(f);
    int idx = atoi(argv[2]), family = atoi(argv[3]);
    int kind, fmt, pix, w, h, paln = 0;
    const uint8_t *data, *pal = NULL;
    if (memcmp(b, "HSFV", 4) == 0) {
        uint32_t bmo = be32(b + 8 + 9 * 8), bmc = be32(b + 12 + 9 * 8);
        uint32_t pao = be32(b + 8 + 10 * 8), pac = be32(b + 12 + 10 * 8);
        const uint8_t *o = b + bmo + idx * 32;
        kind = 1;
        fmt = o[8];
        pix = o[9];
        w = be16(o + 10);
        h = be16(o + 12);
        data = b + bmo + bmc * 32 + be32(o + 28);
        uint32_t pi = be32(o + 20);
        if (pi < pac) {
            const uint8_t *p = b + pao + pi * 16;
            paln = (int)be32(p + 8);
            pal = b + pao + pac * 16 + be32(p + 12);
        }
    } else {
        const uint8_t *o = b + be32(b + 16) + idx * 20;
        kind = 0;
        pix = o[0];
        fmt = o[1];
        paln = be16(o + 2);
        w = be16(o + 4);
        h = be16(o + 6);
        pal = b + be32(o + 12);
        data = b + be32(o + 16);
    }
    uint32_t cap = 4096u * 4096u * 4u;
    uint8_t *out = malloc(cap);
    int ow = 0, oh = 0;
    int r = pb_button_art_test_render(kind, fmt, pix, w, h, data, pal, paln, family, -1, out, cap, &ow, &oh);
    if (r != 0) {
        fprintf(stderr, "%s:%d: render %d\n", argv[1], idx, r);
        return 1;
    }
    FILE *o = fopen(argv[4], "wb");
    uint32_t hdr[2] = { (uint32_t)ow, (uint32_t)oh };
    fwrite(hdr, 4, 2, o);
    fwrite(out, 4, (size_t)ow * oh, o);
    fclose(o);
    return 0;
}
