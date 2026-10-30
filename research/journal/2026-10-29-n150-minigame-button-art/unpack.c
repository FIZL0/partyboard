/* N150: decode every file of a data/*.bin archive with the game's HuDecodeData (libdol.so).
 * unpack <in.bin> <out dir>: writes <out dir>/NNN.dat (decoded) for each file. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

void HuDecodeData(void *src, void *dst, uint32_t size, int32_t decodeType);

static uint32_t be(const uint8_t *p) { return (uint32_t)p[0] << 24 | p[1] << 16 | p[2] << 8 | p[3]; }

int main(int argc, char **argv)
{
    FILE *f = fopen(argv[1], "rb");
    if (!f) return 1;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *b = malloc(n);
    fread(b, 1, n, f);
    fclose(f);
    uint32_t cnt = be(b);
    for (uint32_t i = 0; i < cnt; i++) {
        uint32_t ofs = be(b + 4 + i * 4);
        uint32_t raw = be(b + ofs), type = be(b + ofs + 4);
        uint8_t *dst = calloc(1, raw + 64);
        HuDecodeData(b + ofs + 8, dst, raw, (int32_t)type);
        char path[1024];
        snprintf(path, sizeof path, "%s/%03u.dat", argv[2], i);
        FILE *o = fopen(path, "wb");
        fwrite(dst, 1, raw, o);
        fclose(o);
        free(dst);
    }
    return 0;
}
