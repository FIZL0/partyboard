// N150: GameCube button pictures in minigame/menu sprites and model textures, redrawn for the player's controller
// (include/port/button_art.h). The pictures are known by the FNV-1a hash of their texels; the list and how it was
// found are in research/journal/2026-10-29-n150-minigame-button-art.

#include "port/button_art.h"
#include "port/button_icons.h"

#include "game/animdata.h"
extern "C" {
#include "game/esprite.h"
}
#include "game/gamework_data.h"
#include "game/hsfformat.h"
#include "game/object.h"
#include "game/sprite.h"

#include <aurora/gfx.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <unordered_map>
#include <vector>

namespace {

// ------------------------------------------------------------------ the known pictures

enum Fill : u8 {
    F_CLEAR, // the slot becomes transparent, then the glyph fills it (a lone button sprite)
    F_ROWS, // each row of the slot takes the colour two pixels right of it (a button on a shaded panel)
    F_PLATE, // the slot takes the average colour of its border (a button face on a model)
    F_CHECKER, // a two-colour checker: origin cx, cy, cell size, light sample lx, ly, dark sample dx, dy
};

struct Checker {
    s16 cx, cy, cell, lx, ly, dx, dy;
};

struct Slot {
    s8 entry; // the GameCube button as a message icon number (2 A, 3 B, 4 X, 5 Y, 6 R, 8 L, 11 Z, 12 START)
    s16 x0, y0, x1, y1; // in the original bitmap's pixels
    Fill fill;
    float glyph; // the glyph's side over the slot's shorter side
    bool pink; // option's selected L/R frames are pink
    Checker checker;
};

struct Art {
    u64 hash;
    u8 kind; // 0 ANM bitmap, 1 HSF bitmap
    u8 fmt; // ANIM_BMP_* or HSF_BMPFMT_*
    s16 w, h;
    int scale; // the redrawn bitmap's size over the original's
    const char *what;
    int n;
    Slot slot[4];
};

constexpr Checker kNoChecker {};

#define ICON(e) { { e, 0, 0, 24, 24, F_CLEAR, 1.0f, false, kNoChecker } }

// clang-format off
const Art kArt[] = {
    // m405 (and m432) HUD buttons, 24x24 RGB5A3, one per player
    { 0xF404019F7327A74EULL, 0, ANIM_BMP_RGB5A3_DUPE, 24, 24, 2, "m405 A", 1, ICON(2) },
    { 0xB58A7209E04CC3E6ULL, 0, ANIM_BMP_RGB5A3_DUPE, 24, 24, 2, "m405/m432 B", 1, ICON(3) },
    { 0x248AE51D103AD9D2ULL, 0, ANIM_BMP_RGB5A3_DUPE, 24, 24, 2, "m405/m432 L", 1, ICON(8) },
    { 0xCE7CD5DE41D2CD24ULL, 0, ANIM_BMP_RGB5A3_DUPE, 24, 24, 2, "m405 R", 1, ICON(6) },
    { 0x753DBB90C016B743ULL, 0, ANIM_BMP_RGB5A3_DUPE, 24, 24, 2, "m432 R", 1, ICON(6) },
    { 0x6411C8346C7E97E3ULL, 0, ANIM_BMP_RGB5A3_DUPE, 24, 24, 2, "m432 Y", 1, ICON(5) },
    { 0xFBC9CAEE3D82DA50ULL, 0, ANIM_BMP_RGB5A3_DUPE, 24, 24, 2, "m432 X", 1, ICON(4) },
    // mgmodeDll's 'A start / B back' panels, mpexDll's 'START start / B back'
    { 0xBC1A8D8B8099BE93ULL, 0, ANIM_BMP_CMPR, 216, 58, 2, "mgmode A start B back", 2,
        { { 2, 13, 5, 35, 27, F_ROWS, 1.0f, false, kNoChecker }, { 3, 13, 31, 36, 54, F_ROWS, 1.0f, false, kNoChecker } } },
    { 0x387BCF0CFCC52530ULL, 0, ANIM_BMP_C8, 216, 36, 2, "mgmode B back", 1,
        { { 3, 13, 7, 35, 29, F_ROWS, 1.0f, false, kNoChecker } } },
    { 0xC3E6D236A3DBAF3CULL, 0, ANIM_BMP_C8, 216, 62, 2, "mgmode A add/delete B back", 2,
        { { 2, 13, 6, 35, 28, F_ROWS, 1.0f, false, kNoChecker }, { 3, 13, 34, 35, 56, F_ROWS, 1.0f, false, kNoChecker } } },
    { 0x0B2D2A200919A8A6ULL, 0, ANIM_BMP_C8, 216, 58, 2, "mpex START start B back", 2,
        { { 12, 12, 2, 36, 26, F_ROWS, 1.0f, false, kNoChecker }, { 3, 13, 32, 35, 55, F_ROWS, 1.0f, false, kNoChecker } } },
    // minigame_seq.c's practice banner 'Practice [Z] Back'
    { 0xE8443C8D26BA6BADULL, 0, ANIM_BMP_C8, 320, 42, 2, "practice Z back", 1,
        { { 11, 172, 11, 210, 32, F_CHECKER, 1.4f, false, { 155, 1, 8, 158, 6, 166, 6 } } } },
    // optionDll's L/R page buttons (sound, records): L, L selected, R, R selected
    { 0xA1474D65AB7CF710ULL, 0, ANIM_BMP_C8, 24, 96, 2, "option L/R", 4,
        { { 8, 0, 0, 24, 24, F_CLEAR, 1.0f, false, kNoChecker }, { 8, 0, 24, 24, 48, F_CLEAR, 1.0f, true, kNoChecker },
          { 6, 0, 48, 24, 72, F_CLEAR, 1.0f, false, kNoChecker }, { 6, 0, 72, 24, 96, F_CLEAR, 1.0f, true, kNoChecker } } },
    // m418's button stones (models), 64x64 CMPR
    { 0x23E1AC6E81C11A4AULL, 1, HSF_BMPFMT_CMPR, 64, 64, 2, "m418 stone B", 1, { { 3, 0, 0, 64, 64, F_PLATE, 0.8f, false, kNoChecker } } },
    { 0x92EFBED5BDF2E435ULL, 1, HSF_BMPFMT_CMPR, 64, 64, 2, "m418 stone A", 1, { { 2, 0, 0, 64, 64, F_PLATE, 0.8f, false, kNoChecker } } },
    { 0x6F0C86965295CAA4ULL, 1, HSF_BMPFMT_CMPR, 64, 64, 2, "m418 stone X", 1, { { 4, 0, 0, 64, 64, F_PLATE, 0.8f, false, kNoChecker } } },
    { 0xFB037D4BCA4854EAULL, 1, HSF_BMPFMT_CMPR, 64, 64, 2, "m418 stone Y", 1, { { 5, 0, 0, 64, 64, F_PLATE, 0.8f, false, kNoChecker } } },
    // m460's L/R floor panels (models), 128x128 RGB565
    { 0x8EA96ACC521A5C2AULL, 1, HSF_BMPFMT_RGB565, 128, 128, 1, "m460 L panel", 1, { { 8, 8, 8, 120, 120, F_PLATE, 0.8f, false, kNoChecker } } },
    { 0x5A2D10BCC75470FBULL, 1, HSF_BMPFMT_RGB565, 128, 128, 1, "m460 R panel", 1, { { 6, 8, 8, 120, 120, F_PLATE, 0.8f, false, kNoChecker } } },
    // instDll's START and Z button models (every instruction stand's colour shares them), C8 with an RGB565 palette
    { 0x0925FA4F8AE52409ULL, 1, HSF_BMPFMT_CI_RGB565, 128, 128, 1, "inst START", 1, { { 12, 0, 0, 128, 128, F_PLATE, 0.75f, false, kNoChecker } } },
    { 0xA8989C9ED7B70E3CULL, 1, HSF_BMPFMT_CI_RGB565, 128, 64, 1, "inst Z", 1, { { 11, 0, 0, 128, 48, F_PLATE, 1.4f, false, kNoChecker } } },
};
// clang-format on

#undef ICON

// ------------------------------------------------------------------ GX texels

enum Tex { T_I4, T_I8, T_IA4, T_IA8, T_RGB565, T_RGB5A3, T_RGBA8, T_CMPR, T_C4, T_C8, T_A8, T_NONE };
enum Pal { P_RGB5A3, P_RGB565, P_IA8 };

struct Src {
    Tex tex;
    Pal pal;
};

Src anm_src(int fmt)
{
    switch (fmt & ANIM_BMP_FMTMASK) {
        case ANIM_BMP_RGBA8:
            return { T_RGBA8, P_RGB5A3 };
        case ANIM_BMP_RGB5A3:
        case ANIM_BMP_RGB5A3_DUPE:
            return { T_RGB5A3, P_RGB5A3 };
        case ANIM_BMP_C8:
            return { T_C8, P_RGB5A3 };
        case ANIM_BMP_C4:
            return { T_C4, P_RGB5A3 };
        case ANIM_BMP_IA8:
            return { T_IA8, P_RGB5A3 };
        case ANIM_BMP_IA4:
            return { T_IA4, P_RGB5A3 };
        case ANIM_BMP_I8:
            return { T_I8, P_RGB5A3 };
        case ANIM_BMP_I4:
            return { T_I4, P_RGB5A3 };
        case ANIM_BMP_A8:
            return { T_A8, P_RGB5A3 };
        case ANIM_BMP_CMPR:
            return { T_CMPR, P_RGB5A3 };
        default:
            return { T_NONE, P_RGB5A3 };
    }
}

Src hsf_src(int fmt, int pix_size)
{
    switch (fmt) {
        case HSF_BMPFMT_I4:
            return { T_I4, P_RGB5A3 };
        case HSF_BMPFMT_I8:
            return { T_I8, P_RGB5A3 };
        case HSF_BMPFMT_IA4:
            return { T_IA4, P_RGB5A3 };
        case HSF_BMPFMT_IA8:
            return { T_IA8, P_RGB5A3 };
        case HSF_BMPFMT_RGB565:
            return { T_RGB565, P_RGB5A3 };
        case HSF_BMPFMT_RGB5A3:
            return { T_RGB5A3, P_RGB5A3 };
        case HSF_BMPFMT_RGBA8:
            return { T_RGBA8, P_RGB5A3 };
        case HSF_BMPFMT_CMPR:
            return { T_CMPR, P_RGB5A3 };
        case HSF_BMPFMT_CI_RGB565:
            return { pix_size < 8 ? T_C4 : T_C8, P_RGB565 };
        case HSF_BMPFMT_CI_RGB5A3:
            return { pix_size < 8 ? T_C4 : T_C8, P_RGB5A3 };
        case HSF_BMPFMT_CI_IA8:
            return { pix_size < 8 ? T_C4 : T_C8, P_IA8 };
        default:
            return { T_NONE, P_RGB5A3 };
    }
}

// Bytes of a w x h texture (one level), tiles padded.
u32 tex_bytes(Tex t, int w, int h)
{
    int tw = 4, th = 4, bpp = 16;
    switch (t) {
        case T_I4:
        case T_C4:
        case T_CMPR:
            tw = th = 8;
            bpp = 4;
            break;
        case T_I8:
        case T_IA4:
        case T_C8:
        case T_A8:
            tw = 8;
            bpp = 8;
            break;
        case T_RGBA8:
            bpp = 32;
            break;
        default:
            break;
    }
    return static_cast<u32>((w + tw - 1) / tw * tw) * static_cast<u32>((h + th - 1) / th * th) * bpp / 8;
}

u64 fnv64(const u8 *p, u32 n)
{
    u64 h = 0xCBF29CE484222325ULL;
    for (u32 i = 0; i < n; ++i) {
        h = (h ^ p[i]) * 0x100000001B3ULL;
    }
    return h;
}

struct Rgba8 {
    u8 r, g, b, a;
};

u16 be16(const u8 *p)
{
    return static_cast<u16>(p[0] << 8 | p[1]);
}

Rgba8 rgb5a3(u16 v)
{
    if (v & 0x8000) {
        return { static_cast<u8>(((v >> 10) & 31) * 255 / 31), static_cast<u8>(((v >> 5) & 31) * 255 / 31), static_cast<u8>((v & 31) * 255 / 31), 255 };
    }
    return { static_cast<u8>(((v >> 8) & 15) * 17), static_cast<u8>(((v >> 4) & 15) * 17), static_cast<u8>((v & 15) * 17), static_cast<u8>(((v >> 12) & 7) * 255 / 7) };
}

Rgba8 rgb565(u16 v)
{
    return { static_cast<u8>(((v >> 11) & 31) * 255 / 31), static_cast<u8>(((v >> 5) & 63) * 255 / 63), static_cast<u8>((v & 31) * 255 / 31), 255 };
}

Rgba8 pal_color(const u8 *pal, int pal_num, Pal pf, int idx)
{
    if (pal == nullptr || idx >= pal_num) {
        return { 0, 0, 0, 0 };
    }
    const u16 v = be16(pal + idx * 2);
    switch (pf) {
        case P_RGB565:
            return rgb565(v);
        case P_IA8:
            return { static_cast<u8>(v & 255), static_cast<u8>(v & 255), static_cast<u8>(v & 255), static_cast<u8>(v >> 8) };
        default:
            return rgb5a3(v);
    }
}

// Decodes GX texels to straight RGBA rows.
std::vector<Rgba8> decode(Src s, int w, int h, const u8 *d, const u8 *pal, int pal_num)
{
    std::vector<Rgba8> out(static_cast<size_t>(w) * h);
    auto put = [&](int x, int y, Rgba8 c) {
        if (x < w && y < h) {
            out[static_cast<size_t>(y) * w + x] = c;
        }
    };
    if (s.tex == T_CMPR) {
        for (int by = 0; by < h; by += 8) {
            for (int bx = 0; bx < w; bx += 8) {
                for (int sb = 0; sb < 4; ++sb, d += 8) {
                    const u16 c0 = be16(d), c1 = be16(d + 2);
                    Rgba8 c[4] = { rgb565(c0), rgb565(c1) };
                    if (c0 > c1) {
                        c[2] = { static_cast<u8>((2 * c[0].r + c[1].r) / 3), static_cast<u8>((2 * c[0].g + c[1].g) / 3), static_cast<u8>((2 * c[0].b + c[1].b) / 3), 255 };
                        c[3] = { static_cast<u8>((c[0].r + 2 * c[1].r) / 3), static_cast<u8>((c[0].g + 2 * c[1].g) / 3), static_cast<u8>((c[0].b + 2 * c[1].b) / 3), 255 };
                    } else {
                        c[2] = { static_cast<u8>((c[0].r + c[1].r) / 2), static_cast<u8>((c[0].g + c[1].g) / 2), static_cast<u8>((c[0].b + c[1].b) / 2), 255 };
                        c[3] = { 0, 0, 0, 0 };
                    }
                    const u32 bits = static_cast<u32>(d[4]) << 24 | d[5] << 16 | d[6] << 8 | d[7];
                    for (int p = 0; p < 16; ++p) {
                        put(bx + (sb & 1) * 4 + p % 4, by + (sb >> 1) * 4 + p / 4, c[(bits >> (30 - 2 * p)) & 3]);
                    }
                }
            }
        }
        return out;
    }
    if (s.tex == T_RGBA8) {
        for (int by = 0; by < h; by += 4) {
            for (int bx = 0; bx < w; bx += 4, d += 64) {
                for (int p = 0; p < 16; ++p) {
                    put(bx + p % 4, by + p / 4, { d[p * 2 + 1], d[32 + p * 2], d[32 + p * 2 + 1], d[p * 2] });
                }
            }
        }
        return out;
    }
    const bool four = s.tex == T_I4 || s.tex == T_C4;
    const bool sixteen = s.tex == T_IA8 || s.tex == T_RGB565 || s.tex == T_RGB5A3;
    const int tw = sixteen ? 4 : 8, th = four ? 8 : 4;
    for (int by = 0; by < h; by += th) {
        for (int bx = 0; bx < w; bx += tw) {
            for (int p = 0; p < tw * th; ++p) {
                const int x = bx + p % tw, y = by + p / tw;
                Rgba8 c {};
                if (four) {
                    const int v = (d[p / 2] >> ((p & 1) ? 0 : 4)) & 15;
                    c = s.tex == T_I4 ? Rgba8 { static_cast<u8>(v * 17), static_cast<u8>(v * 17), static_cast<u8>(v * 17), static_cast<u8>(v * 17) }
                                      : pal_color(pal, pal_num, s.pal, v);
                } else if (sixteen) {
                    const u16 v = be16(d + p * 2);
                    c = s.tex == T_RGB565 ? rgb565(v)
                        : s.tex == T_RGB5A3 ? rgb5a3(v)
                                            : Rgba8 { static_cast<u8>(v & 255), static_cast<u8>(v & 255), static_cast<u8>(v & 255), static_cast<u8>(v >> 8) };
                } else {
                    const u8 v = d[p];
                    switch (s.tex) {
                        case T_C8:
                            c = pal_color(pal, pal_num, s.pal, v);
                            break;
                        case T_IA4:
                            c = { static_cast<u8>((v & 15) * 17), static_cast<u8>((v & 15) * 17), static_cast<u8>((v & 15) * 17), static_cast<u8>((v >> 4) * 17) };
                            break;
                        default: // I8, A8
                            c = { v, v, v, v };
                            break;
                    }
                }
                put(x, y, c);
            }
            d += tw * th * (four ? 4 : sixteen ? 16 : 8) / 8;
        }
    }
    return out;
}

// GX RGBA8 tiles of `px` (w x h); w and h multiples of 4.
void encode_rgba8(const std::vector<Rgba8> &px, int w, int h, u8 *out)
{
    for (int by = 0; by < h; by += 4) {
        for (int bx = 0; bx < w; bx += 4, out += 64) {
            for (int p = 0; p < 16; ++p) {
                const Rgba8 &c = px[static_cast<size_t>(by + p / 4) * w + bx + p % 4];
                out[p * 2] = c.a;
                out[p * 2 + 1] = c.r;
                out[32 + p * 2] = c.g;
                out[32 + p * 2 + 1] = c.b;
            }
        }
    }
}

// ------------------------------------------------------------------ redrawing

struct Redrawn {
    int w = 0, h = 0;
    std::vector<Rgba8> rgba; // straight, rows
    std::vector<u8> gx; // RGBA8 tiles, then (mips) each smaller level
};

void over(Rgba8 &d, float r, float g, float b, float a)
{
    if (a <= 0.0f) {
        return;
    }
    const float da = d.a / 255.0f;
    const float oa = a + da * (1.0f - a);
    auto mix = [&](float s, u8 dc) { return static_cast<u8>(std::lround(std::clamp((s * a + dc / 255.0f * da * (1.0f - a)) / oa, 0.0f, 1.0f) * 255.0f)); };
    d = { mix(r, d.r), mix(g, d.g), mix(b, d.b), static_cast<u8>(std::lround(oa * 255.0f)) };
}

// Redraws `orig` (w x h) with each slot's glyph (glyph ids in `glyphs`, PB_GLYPH_NONE keeps that slot).
Redrawn redraw(const Art &art, const std::vector<Rgba8> &orig, PbIconFamily family, const std::vector<int> &glyphs, bool mips)
{
    const int s = art.scale;
    Redrawn r;
    r.w = art.w * s;
    r.h = art.h * s;
    r.rgba.resize(static_cast<size_t>(r.w) * r.h);
    for (int y = 0; y < r.h; ++y) {
        for (int x = 0; x < r.w; ++x) {
            r.rgba[static_cast<size_t>(y) * r.w + x] = orig[static_cast<size_t>(y / s) * art.w + x / s];
        }
    }
    auto at = [&](int x, int y) -> Rgba8 & { return r.rgba[static_cast<size_t>(std::clamp(y, 0, r.h - 1)) * r.w + std::clamp(x, 0, r.w - 1)]; };
    for (int i = 0; i < art.n; ++i) {
        const Slot &sl = art.slot[i];
        if (glyphs[i] == PB_GLYPH_NONE) {
            continue;
        }
        const int x0 = sl.x0 * s, y0 = sl.y0 * s, x1 = sl.x1 * s, y1 = sl.y1 * s;
        Rgba8 plate {};
        if (sl.fill == F_PLATE) {
            u32 sum[4] = {}, n = 0;
            for (int x = x0; x < x1; ++x) {
                for (int y : { y0, y1 - 1 }) {
                    const Rgba8 &c = at(x, y);
                    sum[0] += c.r, sum[1] += c.g, sum[2] += c.b, sum[3] += c.a, ++n;
                }
            }
            for (int y = y0; y < y1; ++y) {
                for (int x : { x0, x1 - 1 }) {
                    const Rgba8 &c = at(x, y);
                    sum[0] += c.r, sum[1] += c.g, sum[2] += c.b, sum[3] += c.a, ++n;
                }
            }
            plate = { static_cast<u8>(sum[0] / n), static_cast<u8>(sum[1] / n), static_cast<u8>(sum[2] / n), static_cast<u8>(sum[3] / n) };
        }
        Rgba8 light {}, dark {};
        if (sl.fill == F_CHECKER) {
            light = at(sl.checker.lx * s, sl.checker.ly * s);
            dark = at(sl.checker.dx * s, sl.checker.dy * s);
        }
        for (int y = y0; y < y1; ++y) {
            const Rgba8 row = at(x1 + 2 * s, y);
            for (int x = x0; x < x1; ++x) {
                Rgba8 &c = at(x, y);
                switch (sl.fill) {
                    case F_CLEAR:
                        c = {};
                        break;
                    case F_ROWS:
                        c = row;
                        break;
                    case F_PLATE:
                        c = plate;
                        break;
                    case F_CHECKER: {
                        const int cx = static_cast<int>(std::floor((x / static_cast<float>(s) - sl.checker.cx) / sl.checker.cell));
                        const int cy = static_cast<int>(std::floor((y / static_cast<float>(s) - sl.checker.cy) / sl.checker.cell));
                        c = ((cx + cy) & 1) == 0 ? light : dark;
                        break;
                    }
                }
            }
        }
        const int side = std::max(4, static_cast<int>(std::lround(std::min(x1 - x0, y1 - y0) * sl.glyph)));
        std::vector<u8> g(static_cast<size_t>(side) * side * 4);
        if (pb_button_icons_glyph_rgba_sized(family, glyphs[i], side, g.data()) != 0) {
            continue;
        }
        const int gx = x0 + (x1 - x0 - side) / 2, gy = y0 + (y1 - y0 - side) / 2;
        for (int y = 0; y < side; ++y) {
            for (int x = 0; x < side; ++x) {
                const u8 *p = &g[(static_cast<size_t>(y) * side + x) * 4];
                float cr = p[0] / 255.0f, cg = p[1] / 255.0f, cb = p[2] / 255.0f;
                if (sl.pink) {
                    cr = cr * 0.55f + 0.45f * 1.0f;
                    cg = cg * 0.55f + 0.45f * 0.55f;
                    cb = cb * 0.55f + 0.45f * 0.70f;
                }
                if (gx + x >= 0 && gx + x < r.w && gy + y >= 0 && gy + y < r.h) {
                    over(at(gx + x, gy + y), cr, cg, cb, p[3] / 255.0f);
                }
            }
        }
    }
    // GX tiles: the size padded to 4, then for models every smaller level (a material may ask for mipmaps)
    int w = r.w, h = r.h;
    std::vector<Rgba8> level = r.rgba;
    for (;;) {
        const int pw = (w + 3) / 4 * 4, ph = (h + 3) / 4 * 4;
        std::vector<Rgba8> padded(static_cast<size_t>(pw) * ph);
        for (int y = 0; y < ph; ++y) {
            for (int x = 0; x < pw; ++x) {
                padded[static_cast<size_t>(y) * pw + x] = level[static_cast<size_t>(std::min(y, h - 1)) * w + std::min(x, w - 1)];
            }
        }
        const size_t at0 = r.gx.size();
        r.gx.resize(at0 + static_cast<size_t>(pw) * ph * 4);
        encode_rgba8(padded, pw, ph, r.gx.data() + at0);
        if (!mips || (w == 1 && h == 1)) {
            break;
        }
        const int nw = std::max(1, w / 2), nh = std::max(1, h / 2);
        std::vector<Rgba8> next(static_cast<size_t>(nw) * nh);
        for (int y = 0; y < nh; ++y) {
            for (int x = 0; x < nw; ++x) {
                u32 sum[4] = {};
                for (int k = 0; k < 4; ++k) {
                    const Rgba8 &c = level[static_cast<size_t>(std::min(y * 2 + k / 2, h - 1)) * w + std::min(x * 2 + k % 2, w - 1)];
                    sum[0] += c.r, sum[1] += c.g, sum[2] += c.b, sum[3] += c.a;
                }
                next[static_cast<size_t>(y) * nw + x] = { static_cast<u8>(sum[0] / 4), static_cast<u8>(sum[1] / 4), static_cast<u8>(sum[2] / 4), static_cast<u8>(sum[3] / 4) };
            }
        }
        level.swap(next);
        w = nw;
        h = nh;
    }
    return r;
}

// The known art these texels are, or null. Cheap unless kind/format/size match an entry.
const Art *find_art(int kind, Tex tex, int fmt, int w, int h, const void *data)
{
    const Art *cand = nullptr;
    for (const Art &a : kArt) {
        if (a.kind == kind && a.w == w && a.h == h && (kind == 0 ? anm_src(a.fmt).tex == tex : a.fmt == fmt)) {
            cand = &a;
            break;
        }
    }
    if (cand == nullptr || data == nullptr) {
        return nullptr;
    }
    const u64 hash = fnv64(static_cast<const u8 *>(data), tex_bytes(tex, w, h));
    for (const Art &a : kArt) {
        if (a.hash == hash && a.kind == kind && a.w == w && a.h == h) {
            return &a;
        }
    }
    return nullptr;
}

// ------------------------------------------------------------------ seats

// The pad port of the first human player (the one whose controller art without a seat shows), else 0.
int default_port()
{
    for (int i = 0; i < 4; ++i) {
        if (!GWPlayerCfg[i].iscom) {
            return GWPlayerCfg[i].pad_idx & 3;
        }
    }
    return 0;
}

int player_port(s32 player)
{
    if (player >= 0 && player < 4 && !GWPlayerCfg[player].iscom) {
        return GWPlayerCfg[player].pad_idx & 3;
    }
    return default_port();
}

bool icon_log()
{
    static const bool on = std::getenv("PB_ICON_LOG") != nullptr;
    return on;
}

void log_once(const Art &art, int port, PbIconFamily f)
{
    if (!icon_log()) {
        return;
    }
    static std::map<std::pair<const Art *, int>, int> logged;
    int &n = logged[{ &art, port * 16 + f }];
    if (n++ == 0) {
        std::fprintf(stderr, "button_art: ovl %d %s port %d -> %s\n", static_cast<int>(omcurovl), art.what, port, pb_button_icons_family_name(f));
    }
}

// PB_ART_SHOT=<dir> (test hook, like N60's PB_ICON_SHOT): the first time an overlay draws a known picture (GameCube
// included, for the originals), save the game frame 20, 120 and 300 frames later (or PB_ART_SHOT_DELAYS, e.g. "5,200")
// as <dir>/ovlNNN-artNN-dNNN.png (aurora_capture_frame).
void shot_on_first(const Art &art)
{
    static const char *dir = std::getenv("PB_ART_SHOT");
    if (dir == nullptr) {
        return;
    }
    static int shot_ovl = -2;
    static std::vector<bool> shot;
    if (shot_ovl != omcurovl) {
        shot_ovl = omcurovl;
        shot.assign(sizeof(kArt) / sizeof(kArt[0]), false);
    }
    const size_t id = static_cast<size_t>(&art - kArt);
    if (shot[id]) {
        return;
    }
    shot[id] = true;
    const char *delays = std::getenv("PB_ART_SHOT_DELAYS");
    for (const char *d = delays ? delays : "20,120,300"; *d != '\0';) {
        char *end;
        const unsigned long delay = std::strtoul(d, &end, 10);
        if (end == d) {
            break;
        }
        d = *end == ',' ? end + 1 : end;
        char path[1024];
        std::snprintf(path, sizeof(path), "%s/ovl%03d-art%02d-d%03lu.png", dir, static_cast<int>(omcurovl), static_cast<int>(id), delay);
        aurora_capture_frame(path, static_cast<u32>(delay));
    }
}

std::vector<int> slot_glyphs(const Art &art, int port, PbIconFamily f)
{
    std::vector<int> g(art.n);
    for (int i = 0; i < art.n; ++i) {
        g[i] = pb_button_icons_glyph_for_entry_family(port, f, art.slot[i].entry);
    }
    return g;
}

// Redrawn bitmaps by (art, family, glyphs): content only, shared by every anim/model that loads the picture.
const Redrawn &redrawn(const Art &art, const std::vector<Rgba8> &orig, PbIconFamily f, const std::vector<int> &glyphs, bool mips)
{
    static std::map<std::pair<const Art *, std::vector<int>>, Redrawn> cache;
    std::vector<int> key = glyphs;
    key.push_back(f);
    key.push_back(mips);
    auto it = cache.find({ &art, key });
    if (it == cache.end()) {
        it = cache.emplace(std::make_pair(&art, key), redraw(art, orig, f, glyphs, mips)).first;
    }
    return it->second;
}

// ------------------------------------------------------------------ sprites

struct Known {
    const Art *art;
    int bmp;
    const void *data;
};

struct Wrapper {
    ANIMDATA anim;
    std::vector<ANIMBMP> bmp;
    // the patterns again, their layers on a redrawn bitmap in its pixels (it is `scale` times the original's)
    std::vector<ANIMPAT> pat;
    std::vector<std::vector<ANIMLAYER>> layer;
};

std::unordered_map<const ANIMDATA *, std::vector<Known>> g_known;
std::map<std::pair<const ANIMDATA *, std::vector<int>>, Wrapper *> g_wrappers;
std::unordered_map<int, std::pair<const ANIMDATA *, s32>> g_seats; // sprite id -> (its anim then, player)

void drop_anim(const ANIMDATA *anim)
{
    g_known.erase(anim);
    for (auto it = g_wrappers.begin(); it != g_wrappers.end();) {
        if (it->first.first == anim) {
            // the wrapper's texture objects are left as they are (a few per minigame at most)
            it = g_wrappers.erase(it);
        } else {
            ++it;
        }
    }
}

} // namespace

extern "C" {

void pb_button_art_anim_read(ANIMDATA *anim)
{
    if (anim == nullptr || (anim->bmpNum & ANIM_BMP_ALLOC)) {
        return;
    }
    drop_anim(anim); // a new anim where a freed one was
    std::vector<Known> found;
    for (int i = 0; i < anim->bmpNum; ++i) {
        const ANIMBMP &b = anim->bmp[i];
        const Src s = anm_src(b.dataFmt);
        if (s.tex == T_NONE) {
            continue;
        }
        if (const Art *a = find_art(0, s.tex, b.dataFmt & ANIM_BMP_FMTMASK, b.sizeX, b.sizeY, b.data)) {
            found.push_back({ a, i, b.data });
        }
    }
    if (!found.empty()) {
        g_known[anim] = std::move(found);
    }
}

ANIMDATA *pb_button_art_sprite_anim(HUSPRITE *sprite, ANIMDATA *anim, ANIMPAT **pat)
{
    if (g_known.empty() || anim == nullptr) {
        return anim;
    }
    auto k = g_known.find(anim);
    if (k == g_known.end()) {
        return anim;
    }
    for (const Known &kn : k->second) {
        if (kn.bmp >= (anim->bmpNum & ANIM_BMP_NUM_MASK) || anim->bmp[kn.bmp].data != kn.data) {
            // freed with its memory number and the address reused by another anim
            drop_anim(anim);
            return anim;
        }
    }
    s32 player = -1;
    const int id = static_cast<int>(sprite - HuSprData);
    if (auto s = g_seats.find(id); s != g_seats.end() && s->second.first == anim) {
        player = s->second.second;
    }
    const int port = player_port(player);
    const PbIconFamily f = pb_button_icons_family(port);
    for (const Known &kn : k->second) {
        shot_on_first(*kn.art);
        if (f != PB_FAMILY_GAMECUBE) {
            log_once(*kn.art, port, f);
        }
    }
    if (f == PB_FAMILY_GAMECUBE) {
        return anim;
    }
    std::vector<int> key { static_cast<int>(f) };
    for (const Known &kn : k->second) {
        const std::vector<int> g = slot_glyphs(*kn.art, port, f);
        key.insert(key.end(), g.begin(), g.end());
    }
    Wrapper *&w = g_wrappers[{ anim, key }];
    if (w == nullptr) {
        w = new Wrapper;
        w->anim = *anim;
        const int n = anim->bmpNum & ANIM_BMP_NUM_MASK;
        w->bmp.assign(anim->bmp, anim->bmp + n);
        for (const Known &kn : k->second) {
            const ANIMBMP &b = anim->bmp[kn.bmp];
            const std::vector<Rgba8> orig = decode(anm_src(b.dataFmt), b.sizeX, b.sizeY, static_cast<const u8 *>(b.data),
                static_cast<const u8 *>(b.palData), b.palNum);
            const Redrawn &r = redrawn(*kn.art, orig, f, slot_glyphs(*kn.art, port, f), false);
            ANIMBMP &nb = w->bmp[kn.bmp];
            nb.pixSize = 32;
            nb.dataFmt = static_cast<u8>((b.dataFmt & ~ANIM_BMP_FMTMASK) | ANIM_BMP_RGBA8);
            nb.palNum = 0;
            nb.palData = nullptr;
            nb.sizeX = static_cast<s16>(r.w);
            nb.sizeY = static_cast<s16>(r.h);
            nb.dataSize = static_cast<u32>(r.gx.size());
            nb.data = const_cast<u8 *>(r.gx.data());
        }
#ifdef OPTIMIZED_TEXTURE_LOADING
        for (ANIMBMP &b : w->bmp) {
            std::memset(b.texData, 0, sizeof(b.texData));
        }
#endif
        w->anim.bmp = w->bmp.data();
        std::vector<int> scale(n, 1);
        for (const Known &kn : k->second) {
            scale[kn.bmp] = kn.art->scale;
        }
        w->pat.assign(anim->pat, anim->pat + anim->patNum);
        w->layer.resize(anim->patNum);
        for (int p = 0; p < anim->patNum; ++p) {
            w->layer[p].assign(anim->pat[p].layer, anim->pat[p].layer + anim->pat[p].layerNum);
            for (ANIMLAYER &l : w->layer[p]) {
                if (l.bmpNo >= 0 && l.bmpNo < n && scale[l.bmpNo] != 1) {
                    const s16 m = static_cast<s16>(scale[l.bmpNo]);
                    l.startX *= m;
                    l.startY *= m;
                    l.sizeX *= m;
                    l.sizeY *= m;
                }
            }
            w->pat[p].layer = w->layer[p].data();
        }
        w->anim.pat = w->pat.data();
    }
    if (*pat >= anim->pat && *pat < anim->pat + anim->patNum) {
        *pat = &w->pat[*pat - anim->pat];
    }
    return &w->anim;
}

void pb_button_art_esp_seat(s16 espId, s32 player)
{
    const s16 id = espSprIDGet(espId);
    if (id <= 0 || id >= HUSPR_MAX) {
        return;
    }
    g_seats[id] = { HuSprData[id].data, player };
    if (icon_log()) {
        std::fprintf(stderr, "button_art: sprite %d is player %d's\n", id, static_cast<int>(player));
    }
}

void pb_button_art_hsf_bitmaps(HSFBITMAP *bitmaps, s32 count)
{
    for (s32 i = 0; bitmaps != nullptr && i < count; ++i) {
        HSFBITMAP &b = bitmaps[i];
        const Src s = hsf_src(b.dataFmt, b.pixSize);
        if (s.tex == T_NONE) {
            continue;
        }
        const Art *a = find_art(1, s.tex, b.dataFmt, b.sizeX, b.sizeY, b.data);
        if (a == nullptr) {
            continue;
        }
        const int port = default_port();
        const PbIconFamily f = pb_button_icons_family(port);
        shot_on_first(*a);
        if (f == PB_FAMILY_GAMECUBE) {
            continue;
        }
        const std::vector<Rgba8> orig = decode(s, b.sizeX, b.sizeY, static_cast<const u8 *>(b.data), reinterpret_cast<const u8 *>(b.palData), b.palSize);
        const Redrawn &r = redrawn(*a, orig, f, slot_glyphs(*a, port, f), true);
        b.dataFmt = HSF_BMPFMT_RGBA8;
        b.pixSize = 32;
        b.sizeX = static_cast<s16>(r.w);
        b.sizeY = static_cast<s16>(r.h);
        b.palSize = 0;
        b.palData = nullptr;
        b.data = const_cast<u8 *>(r.gx.data());
        log_once(*a, port, f);
    }
}

int pb_button_art_test_render(int kind, int fmt, int pix_size, int w, int h, const void *data, const void *pal, int pal_num,
    int family, int port, u8 *out, u32 cap, int *ow, int *oh)
{
    if (family <= PB_FAMILY_GAMECUBE || family >= PB_FAMILY_COUNT) {
        return -1;
    }
    const Src s = kind == 0 ? anm_src(fmt) : hsf_src(fmt, pix_size);
    if (s.tex == T_NONE) {
        return -1;
    }
    const Art *a = find_art(kind, s.tex, kind == 0 ? (fmt & ANIM_BMP_FMTMASK) : fmt, w, h, data);
    if (a == nullptr) {
        return 1;
    }
    const PbIconFamily f = static_cast<PbIconFamily>(family);
    const std::vector<Rgba8> orig = decode(s, w, h, static_cast<const u8 *>(data), static_cast<const u8 *>(pal), pal_num);
    const Redrawn r = redraw(*a, orig, f, slot_glyphs(*a, port, f), false);
    if (static_cast<u64>(r.w) * r.h * 4 > cap) {
        return -1;
    }
    std::memcpy(out, r.rgba.data(), r.rgba.size() * 4);
    *ow = r.w;
    *oh = r.h;
    return 0;
}

} // extern "C"
