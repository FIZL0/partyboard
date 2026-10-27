// N59 check: every pb_button_icons_glyph_rgba() glyph equals its cell of pb_button_icons_dump_sheet(); writes the
// prints whether they all match, and a few glyph-id lookups.
// cc glyphs.c -Lbuild/linux-default-relwithdebinfo -ldol -Wl,-rpath,$PWD/build/linux-default-relwithdebinfo \
//    -Wl,--unresolved-symbols=ignore-all
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int pb_button_icons_dump_sheet(const char *path);
int pb_button_icons_glyph_count(void);
int pb_button_icons_glyph_rgba(int family, int glyph, uint8_t *out);
int pb_button_icons_glyph_for_native_button(int32_t b);
int pb_button_icons_glyph_for_native_axis(int32_t a);

int main(int argc, char **argv)
{
    const char *sheet = argv[1];
    if (pb_button_icons_dump_sheet(sheet) != 0) {
        return 1;
    }
    FILE *f = fopen(sheet, "rb");
    uint32_t w, h;
    fread(&w, 4, 1, f);
    fread(&h, 4, 1, f);
    uint8_t *ref = malloc((size_t)w * h * 4);
    fread(ref, 1, (size_t)w * h * 4, f);
    fclose(f);
    const int n = pb_button_icons_glyph_count(), S = 48;
    printf("sheet %ux%u, %d glyphs\n", w, h, n);
    uint8_t g[48 * 48 * 4];
    int bad = 0;
    for (int fam = 1; fam < 6; ++fam) {
        for (int c = 0; c < n; ++c) {
            if (pb_button_icons_glyph_rgba(fam, c, g) != 0) {
                printf("family %d glyph %d: failed\n", fam, c);
                ++bad;
                continue;
            }
            for (int y = 0; y < S; ++y) {
                if (memcmp(g + y * S * 4, ref + (((size_t)(fam - 1) * S + y) * w + c * S) * 4, S * 4) != 0) {
                    printf("family %d glyph %d: row %d differs\n", fam, c, y);
                    ++bad;
                    break;
                }
            }
        }
    }
    printf("gamecube rgba -> %d (want -1), bad id -> %d (want -1)\n", pb_button_icons_glyph_rgba(0, 0, g),
        pb_button_icons_glyph_rgba(2, n, g));
    printf("native button 0(south) -> %d, -1 -> %d, axis 4(LT) -> %d, axis 0 -> %d\n", pb_button_icons_glyph_for_native_button(0),
        pb_button_icons_glyph_for_native_button(-1), pb_button_icons_glyph_for_native_axis(4), pb_button_icons_glyph_for_native_axis(0));
    printf("%s\n", bad ? "MISMATCH" : "all glyphs match the sheet");
    return bad != 0;
}
