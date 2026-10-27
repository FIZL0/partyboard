#include <stdio.h>
typedef unsigned char u8;
int pb_button_icons_glyph_rgba(int family, int glyph, u8 *out);
int main(int c, char **v) {
  static u8 buf[48*48*4]; FILE *f = fopen(v[1], "wb");
  int codes[64], n = 0;
  for (int s = 4; s <= 39; ++s) codes[n++] = s;
  for (int s = 45; s <= 56; ++s) codes[n++] = s;
  unsigned w = 48 * 16, h = 48 * ((n + 15) / 16); fwrite(&w,4,1,f); fwrite(&h,4,1,f);
  static u8 img[48*16*48*4*4];
  for (int i = 0; i < n; ++i) { pb_button_icons_glyph_rgba(7, 0x1000 + 16 + codes[i], buf);
    for (int y = 0; y < 48; ++y) for (int x = 0; x < 48; ++x) for (int k = 0; k < 4; ++k)
      img[(((i/16)*48 + y) * w + (i%16)*48 + x)*4 + k] = buf[(y*48+x)*4+k]; }
  fwrite(img, 1, w*h*4, f); fclose(f); return 0; }
