// N49: per-controller button hints (include/port/button_icons.h). Glyphs are drawn here from shapes and a small
// stroke font, so the art is ours; the layouts follow opentennis's Button Icons (B362).

#include "port/button_icons.h"
#include "port/settings.h"

#include "game/animdata.h"
#include "game/object.h"

#include <dolphin/pad.h>
#include <SDL3/SDL_gamepad.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

namespace {

// What a GameCube input is bound to on the player's controller (by position, SDL's names).
enum Ctl : int {
    C_SOUTH,
    C_EAST,
    C_WEST,
    C_NORTH,
    C_BACK,
    C_GUIDE,
    C_START,
    C_LCLICK,
    C_RCLICK,
    C_LSHOULDER,
    C_RSHOULDER,
    C_DPAD_UP,
    C_DPAD_DOWN,
    C_DPAD_LEFT,
    C_DPAD_RIGHT,
    C_LTRIGGER,
    C_RTRIGGER,
    C_LSTICK,
    C_RSTICK,
    C_DPAD,
    C_OTHER,
    C_COUNT,
};

constexpr int kSize = 48;    // bitmap pixels per glyph (drawn at 24x24 game units: crisp at 2x and above)
constexpr int kUnits = 24;   // the original icons' size

// ------------------------------------------------------------------ canvas

struct Rgba {
    float r, g, b, a;
};

constexpr Rgba kInk { 0.09f, 0.09f, 0.10f, 1.0f };
constexpr Rgba kBody { 0.22f, 0.23f, 0.25f, 1.0f };
constexpr Rgba kWhite { 0.96f, 0.96f, 0.96f, 1.0f };
constexpr Rgba kGrey { 0.62f, 0.64f, 0.68f, 1.0f };

struct Canvas {
    std::array<Rgba, kSize * kSize> px {};

    // Paints `color` where `inside(x, y)` holds, with 4x4 supersampled coverage. Coordinates are in 0..1 of the glyph.
    template <class F>
    void fill(const Rgba &c, F inside)
    {
        for (int y = 0; y < kSize; ++y) {
            for (int x = 0; x < kSize; ++x) {
                int n = 0;
                for (int sy = 0; sy < 4; ++sy) {
                    for (int sx = 0; sx < 4; ++sx) {
                        const float fx = (x + (sx + 0.5f) / 4.0f) / kSize;
                        const float fy = (y + (sy + 0.5f) / 4.0f) / kSize;
                        n += inside(fx, fy) ? 1 : 0;
                    }
                }
                if (n == 0) {
                    continue;
                }
                const float cov = (n / 16.0f) * c.a;
                Rgba &d = px[y * kSize + x];
                const float oa = cov + d.a * (1.0f - cov);
                if (oa <= 0.0f) {
                    continue;
                }
                d.r = (c.r * cov + d.r * d.a * (1.0f - cov)) / oa;
                d.g = (c.g * cov + d.g * d.a * (1.0f - cov)) / oa;
                d.b = (c.b * cov + d.b * d.a * (1.0f - cov)) / oa;
                d.a = oa;
            }
        }
    }
};

struct Seg {
    float x0, y0, x1, y1;
};

float seg_dist(float px, float py, const Seg &s)
{
    const float dx = s.x1 - s.x0, dy = s.y1 - s.y0;
    const float len = dx * dx + dy * dy;
    float t = len > 0.0f ? ((px - s.x0) * dx + (py - s.y0) * dy) / len : 0.0f;
    t = std::clamp(t, 0.0f, 1.0f);
    const float ex = s.x0 + t * dx - px, ey = s.y0 + t * dy - py;
    return std::sqrt(ex * ex + ey * ey);
}

bool in_circle(float x, float y, float cx, float cy, float r)
{
    return (x - cx) * (x - cx) + (y - cy) * (y - cy) <= r * r;
}

bool in_rrect(float x, float y, float x0, float y0, float x1, float y1, float r)
{
    if (x < x0 || x > x1 || y < y0 || y > y1) {
        return false;
    }
    const float cx = std::clamp(x, x0 + r, x1 - r), cy = std::clamp(y, y0 + r, y1 - r);
    return in_circle(x, y, cx, cy, r);
}

bool in_tri(float x, float y, float ax, float ay, float bx, float by, float cx, float cy)
{
    const float d1 = (x - bx) * (ay - by) - (ax - bx) * (y - by);
    const float d2 = (x - cx) * (by - cy) - (bx - cx) * (y - cy);
    const float d3 = (x - ax) * (cy - ay) - (cx - ax) * (y - ay);
    const bool neg = d1 < 0 || d2 < 0 || d3 < 0, pos = d1 > 0 || d2 > 0 || d3 > 0;
    return !(neg && pos);
}

// ------------------------------------------------------------------ stroke font (unit box, y down)

void arc(std::vector<Seg> &v, float cx, float cy, float rx, float ry, float a0, float a1)
{
    constexpr int kSteps = 10;
    for (int i = 0; i < kSteps; ++i) {
        const float t0 = a0 + (a1 - a0) * i / kSteps, t1 = a0 + (a1 - a0) * (i + 1) / kSteps;
        v.push_back({ cx + rx * std::cos(t0), cy + ry * std::sin(t0), cx + rx * std::cos(t1), cy + ry * std::sin(t1) });
    }
}

std::vector<Seg> letter(char c)
{
    constexpr float kPi = 3.14159265f;
    std::vector<Seg> v;
    switch (c) {
        case 'A':
            v = { { 0.0f, 1.0f, 0.5f, 0.0f }, { 0.5f, 0.0f, 1.0f, 1.0f }, { 0.22f, 0.64f, 0.78f, 0.64f } };
            break;
        case 'B':
            v = { { 0.05f, 0.0f, 0.05f, 1.0f }, { 0.05f, 0.0f, 0.55f, 0.0f }, { 0.05f, 0.48f, 0.6f, 0.48f },
                { 0.05f, 1.0f, 0.6f, 1.0f } };
            arc(v, 0.55f, 0.24f, 0.3f, 0.24f, -kPi / 2, kPi / 2);
            arc(v, 0.6f, 0.74f, 0.33f, 0.26f, -kPi / 2, kPi / 2);
            break;
        case 'X':
            v = { { 0.05f, 0.0f, 0.95f, 1.0f }, { 0.95f, 0.0f, 0.05f, 1.0f } };
            break;
        case 'Y':
            v = { { 0.0f, 0.0f, 0.5f, 0.5f }, { 1.0f, 0.0f, 0.5f, 0.5f }, { 0.5f, 0.5f, 0.5f, 1.0f } };
            break;
        case 'L':
            v = { { 0.1f, 0.0f, 0.1f, 1.0f }, { 0.1f, 1.0f, 0.9f, 1.0f } };
            break;
        case 'R':
            v = { { 0.05f, 0.0f, 0.05f, 1.0f }, { 0.05f, 0.0f, 0.55f, 0.0f }, { 0.05f, 0.52f, 0.55f, 0.52f },
                { 0.45f, 0.52f, 0.95f, 1.0f } };
            arc(v, 0.55f, 0.26f, 0.35f, 0.26f, -kPi / 2, kPi / 2);
            break;
        case 'Z':
            v = { { 0.05f, 0.0f, 0.95f, 0.0f }, { 0.95f, 0.0f, 0.05f, 1.0f }, { 0.05f, 1.0f, 0.95f, 1.0f } };
            break;
        case 'T':
            v = { { 0.0f, 0.0f, 1.0f, 0.0f }, { 0.5f, 0.0f, 0.5f, 1.0f } };
            break;
        case '1':
            v = { { 0.2f, 0.22f, 0.6f, 0.0f }, { 0.6f, 0.0f, 0.6f, 1.0f } };
            break;
        case '2':
            v = { { 0.9f, 0.32f, 0.05f, 1.0f }, { 0.05f, 1.0f, 0.95f, 1.0f } };
            arc(v, 0.5f, 0.3f, 0.4f, 0.3f, -kPi, 0.12f);
            break;
        case '+':
            v = { { 0.0f, 0.5f, 1.0f, 0.5f }, { 0.5f, 0.0f, 0.5f, 1.0f } };
            break;
        case '-':
            v = { { 0.0f, 0.5f, 1.0f, 0.5f } };
            break;
        default:
            break;
    }
    return v;
}

// Strokes `text` centered at (cx, cy) with letter height h (unit coords), width w per letter.
void text(Canvas &cv, const char *s, float cx, float cy, float h, float w, float stroke, const Rgba &c)
{
    const int n = static_cast<int>(std::strlen(s));
    const float gap = w * 0.35f;
    const float total = n * w + (n - 1) * gap;
    std::vector<Seg> segs;
    for (int i = 0; i < n; ++i) {
        const float x0 = cx - total / 2 + i * (w + gap), y0 = cy - h / 2;
        for (const Seg &g : letter(s[i])) {
            segs.push_back({ x0 + g.x0 * w, y0 + g.y0 * h, x0 + g.x1 * w, y0 + g.y1 * h });
        }
    }
    const float half = stroke / 2;
    cv.fill(c, [&](float x, float y) {
        for (const Seg &g : segs) {
            if (seg_dist(x, y, g) <= half) {
                return true;
            }
        }
        return false;
    });
}

void segs(Canvas &cv, const std::vector<Seg> &v, float stroke, const Rgba &c)
{
    cv.fill(c, [&](float x, float y) {
        for (const Seg &g : v) {
            if (seg_dist(x, y, g) <= stroke / 2) {
                return true;
            }
        }
        return false;
    });
}

// ------------------------------------------------------------------ glyphs

void disc(Canvas &cv, float r, const Rgba &fill)
{
    cv.fill(kInk, [&](float x, float y) { return in_circle(x, y, 0.5f, 0.5f, r); });
    cv.fill(fill, [&](float x, float y) { return in_circle(x, y, 0.5f, 0.5f, r - 0.06f); });
}

void pill(Canvas &cv, float x0, float y0, float x1, float y1, float r, const Rgba &fill)
{
    cv.fill(kInk, [&](float x, float y) { return in_rrect(x, y, x0, y0, x1, y1, r); });
    cv.fill(fill, [&](float x, float y) { return in_rrect(x, y, x0 + 0.06f, y0 + 0.06f, x1 - 0.06f, y1 - 0.06f, std::max(0.0f, r - 0.06f)); });
}

void face_letters(Canvas &cv, PbIconFamily f, Ctl c)
{
    static const char *kXbox[4] = { "A", "B", "X", "Y" };
    static const char *kSwitch[4] = { "B", "A", "Y", "X" };
    static const Rgba kXboxCol[4] = { { 0.38f, 0.80f, 0.30f, 1 }, { 0.92f, 0.28f, 0.25f, 1 }, { 0.25f, 0.55f, 0.95f, 1 },
        { 0.98f, 0.80f, 0.20f, 1 } };
    const int i = c - C_SOUTH;
    switch (f) {
        case PB_FAMILY_XBOX:
            disc(cv, 0.47f, kBody);
            text(cv, kXbox[i], 0.5f, 0.5f, 0.46f, 0.36f, 0.11f, kXboxCol[i]);
            break;
        case PB_FAMILY_STEAMDECK:
            disc(cv, 0.47f, kBody);
            text(cv, kXbox[i], 0.5f, 0.5f, 0.46f, 0.36f, 0.11f, kWhite);
            break;
        case PB_FAMILY_SWITCH:
            disc(cv, 0.47f, kBody);
            text(cv, kSwitch[i], 0.5f, 0.5f, 0.46f, 0.36f, 0.11f, kWhite);
            break;
        case PB_FAMILY_PLAYSTATION: {
            disc(cv, 0.47f, kBody);
            static const Rgba kCross { 0.50f, 0.68f, 0.98f, 1 }, kCircle { 0.98f, 0.40f, 0.42f, 1 },
                kSquare { 0.95f, 0.55f, 0.85f, 1 }, kTriangle { 0.30f, 0.85f, 0.70f, 1 };
            if (c == C_SOUTH) {
                segs(cv, { { 0.32f, 0.32f, 0.68f, 0.68f }, { 0.68f, 0.32f, 0.32f, 0.68f } }, 0.09f, kCross);
            } else if (c == C_EAST) {
                cv.fill(kCircle, [](float x, float y) {
                    const float d = std::sqrt((x - 0.5f) * (x - 0.5f) + (y - 0.5f) * (y - 0.5f));
                    return d <= 0.23f && d >= 0.14f;
                });
            } else if (c == C_WEST) {
                segs(cv, { { 0.32f, 0.32f, 0.68f, 0.32f }, { 0.68f, 0.32f, 0.68f, 0.68f }, { 0.68f, 0.68f, 0.32f, 0.68f },
                             { 0.32f, 0.68f, 0.32f, 0.32f } },
                    0.08f, kSquare);
            } else {
                segs(cv, { { 0.5f, 0.28f, 0.73f, 0.66f }, { 0.73f, 0.66f, 0.27f, 0.66f }, { 0.27f, 0.66f, 0.5f, 0.28f } }, 0.08f,
                    kTriangle);
            }
            break;
        }
        default: {
            // Generic: the four face buttons as a diamond, the one to press filled
            static const float kPos[4][2] = { { 0.5f, 0.79f }, { 0.79f, 0.5f }, { 0.21f, 0.5f }, { 0.5f, 0.21f } };
            for (int k = 0; k < 4; ++k) {
                const float px = kPos[k][0], py = kPos[k][1];
                cv.fill(kInk, [&](float x, float y) { return in_circle(x, y, px, py, 0.2f); });
                cv.fill(k == i ? kWhite : kBody, [&](float x, float y) { return in_circle(x, y, px, py, 0.14f); });
            }
            break;
        }
    }
}

// Shoulder (bumper) or trigger. left: the left one.
void shoulder(Canvas &cv, PbIconFamily f, bool left, bool trigger)
{
    const char *label = "";
    switch (f) {
        case PB_FAMILY_XBOX:
            label = trigger ? (left ? "LT" : "RT") : (left ? "LB" : "RB");
            break;
        case PB_FAMILY_PLAYSTATION:
        case PB_FAMILY_STEAMDECK:
            label = trigger ? (left ? "L2" : "R2") : (left ? "L1" : "R1");
            break;
        case PB_FAMILY_SWITCH:
            label = trigger ? (left ? "ZL" : "ZR") : (left ? "L" : "R");
            break;
        default:
            break;
    }
    if (f == PB_FAMILY_GENERIC) {
        // Letter-free shapes: the bumper a flat bar, the trigger a tall block, each taller on its outer side so
        // left and right read apart.
        const float x0 = trigger ? 0.2f : 0.04f, x1 = trigger ? 0.8f : 0.96f;
        const float top_out = trigger ? 0.04f : 0.24f, top_in = trigger ? 0.3f : 0.44f, bottom = trigger ? 0.94f : 0.74f;
        auto shape = [=](float inset) {
            const float ax = x0 + inset, bx = x1 - inset, by = bottom - inset;
            const float yl = (left ? top_out : top_in) + inset * 1.4f, yr = (left ? top_in : top_out) + inset * 1.4f;
            return [=](float x, float y) {
                if (x < ax || x > bx || y > by) {
                    return false;
                }
                const float t = (x - ax) / (bx - ax);
                return y >= yl + (yr - yl) * t;
            };
        };
        cv.fill(kInk, shape(0.0f));
        cv.fill(kGrey, shape(0.06f));
        // a groove on the trigger's face
        if (trigger) {
            segs(cv, { { 0.34f, 0.62f, 0.66f, 0.62f }, { 0.34f, 0.76f, 0.66f, 0.76f } }, 0.05f, kBody);
        }
        return;
    }
    const bool two = std::strlen(label) > 1;
    if (trigger) {
        pill(cv, two ? 0.04f : 0.16f, 0.12f, two ? 0.96f : 0.84f, 0.88f, 0.22f, kBody);
    } else {
        pill(cv, 0.02f, 0.24f, 0.98f, 0.76f, 0.24f, kBody);
    }
    text(cv, label, 0.5f, 0.5f, 0.36f, two ? 0.26f : 0.3f, 0.1f, kWhite);
}

void stick(Canvas &cv, PbIconFamily f, bool left, bool click)
{
    disc(cv, 0.47f, kBody);
    cv.fill(kInk, [](float x, float y) { return in_circle(x, y, 0.5f, 0.5f, 0.3f); });
    cv.fill(kGrey, [](float x, float y) { return in_circle(x, y, 0.5f, 0.5f, 0.25f); });
    if (f == PB_FAMILY_GENERIC) {
        // a dot on the stick's side tells left from right
        const float dx = left ? 0.36f : 0.64f;
        cv.fill(kInk, [=](float x, float y) { return in_circle(x, y, dx, 0.5f, 0.07f); });
    } else {
        text(cv, left ? "L" : "R", 0.5f, 0.5f, 0.28f, 0.2f, 0.08f, kInk);
    }
    if (click) {
        // pressed: arrows pointing in from top and bottom
        cv.fill(kWhite, [](float x, float y) {
            return in_tri(x, y, 0.42f, 0.0f, 0.58f, 0.0f, 0.5f, 0.14f) || in_tri(x, y, 0.42f, 1.0f, 0.58f, 1.0f, 0.5f, 0.86f);
        });
    }
}

void dpad(Canvas &cv, int dir /* -1 all, else C_DPAD_* */)
{
    auto cross = [](float w, float m) {
        return [=](float x, float y) {
            return (x >= 0.5f - w && x <= 0.5f + w && y >= m && y <= 1 - m) || (y >= 0.5f - w && y <= 0.5f + w && x >= m && x <= 1 - m);
        };
    };
    cv.fill(kInk, cross(0.19f, 0.02f));
    cv.fill(kBody, cross(0.13f, 0.08f));
    if (dir >= 0) {
        cv.fill(kWhite, [=](float x, float y) {
            const bool vert = x >= 0.37f && x <= 0.63f, horiz = y >= 0.37f && y <= 0.63f;
            switch (dir) {
                case C_DPAD_UP:
                    return vert && y >= 0.08f && y <= 0.37f;
                case C_DPAD_DOWN:
                    return vert && y >= 0.63f && y <= 0.92f;
                case C_DPAD_LEFT:
                    return horiz && x >= 0.08f && x <= 0.37f;
                default:
                    return horiz && x >= 0.63f && x <= 0.92f;
            }
        });
    }
}

void menu_button(Canvas &cv, PbIconFamily f, bool start)
{
    switch (f) {
        case PB_FAMILY_SWITCH:
            disc(cv, 0.4f, kBody);
            text(cv, start ? "+" : "-", 0.5f, 0.5f, 0.4f, 0.4f, 0.1f, kWhite);
            return;
        case PB_FAMILY_XBOX:
        case PB_FAMILY_STEAMDECK:
            disc(cv, 0.42f, kBody);
            if (start) {
                segs(cv, { { 0.32f, 0.36f, 0.68f, 0.36f }, { 0.32f, 0.5f, 0.68f, 0.5f }, { 0.32f, 0.64f, 0.68f, 0.64f } }, 0.07f, kWhite);
            } else {
                // view: two overlapping windows
                segs(cv, { { 0.3f, 0.3f, 0.58f, 0.3f }, { 0.3f, 0.3f, 0.3f, 0.54f } }, 0.06f, kWhite);
                segs(cv, { { 0.42f, 0.44f, 0.7f, 0.44f }, { 0.7f, 0.44f, 0.7f, 0.68f }, { 0.7f, 0.68f, 0.42f, 0.68f },
                             { 0.42f, 0.68f, 0.42f, 0.44f } },
                    0.06f, kWhite);
            }
            return;
        case PB_FAMILY_PLAYSTATION:
            // options / create: a small upright pill with three lines or a dot
            pill(cv, 0.3f, 0.08f, 0.7f, 0.92f, 0.18f, kBody);
            if (start) {
                segs(cv, { { 0.42f, 0.36f, 0.58f, 0.36f }, { 0.42f, 0.5f, 0.58f, 0.5f }, { 0.42f, 0.64f, 0.58f, 0.64f } }, 0.06f, kWhite);
            } else {
                cv.fill(kWhite, [](float x, float y) { return in_circle(x, y, 0.5f, 0.5f, 0.08f); });
            }
            return;
        default:
            // Generic: a flat pill with a play (start) or back triangle
            pill(cv, 0.06f, 0.26f, 0.94f, 0.74f, 0.24f, kBody);
            cv.fill(kWhite, [=](float x, float y) {
                return start ? in_tri(x, y, 0.4f, 0.36f, 0.4f, 0.64f, 0.64f, 0.5f) : in_tri(x, y, 0.6f, 0.36f, 0.6f, 0.64f, 0.36f, 0.5f);
            });
            return;
    }
}

void draw(Canvas &cv, PbIconFamily f, Ctl c)
{
    switch (c) {
        case C_SOUTH:
        case C_EAST:
        case C_WEST:
        case C_NORTH:
            face_letters(cv, f, c);
            break;
        case C_START:
        case C_BACK:
            menu_button(cv, f, c == C_START);
            break;
        case C_LSHOULDER:
        case C_RSHOULDER:
            shoulder(cv, f, c == C_LSHOULDER, false);
            break;
        case C_LTRIGGER:
        case C_RTRIGGER:
            shoulder(cv, f, c == C_LTRIGGER, true);
            break;
        case C_LSTICK:
        case C_RSTICK:
            stick(cv, f, c == C_LSTICK, false);
            break;
        case C_LCLICK:
        case C_RCLICK:
            stick(cv, f, c == C_LCLICK, true);
            break;
        case C_DPAD:
            dpad(cv, -1);
            break;
        case C_DPAD_UP:
        case C_DPAD_DOWN:
        case C_DPAD_LEFT:
        case C_DPAD_RIGHT:
            dpad(cv, c);
            break;
        case C_GUIDE:
            disc(cv, 0.47f, kBody);
            cv.fill(kWhite, [](float x, float y) { return in_circle(x, y, 0.5f, 0.5f, 0.17f); });
            break;
        default:
            // paddles, touchpad, misc: a plain button
            disc(cv, 0.4f, kGrey);
            break;
    }
}

// GX RGBA8: 4x4 tiles, each 32 bytes of AR then 32 bytes of GB.
void to_gx_rgba8(const Canvas &cv, float dim, float shift, u8 *out)
{
    const int sh = static_cast<int>(shift * kSize);
    size_t o = 0;
    for (int ty = 0; ty < kSize; ty += 4) {
        for (int tx = 0; tx < kSize; tx += 4) {
            u8 *ar = out + o, *gb = out + o + 32;
            for (int i = 0; i < 16; ++i) {
                const int x = tx + (i & 3), y = ty + (i >> 2) - sh;
                Rgba p {};
                if (y >= 0 && y < kSize) {
                    p = cv.px[y * kSize + x];
                }
                ar[i * 2] = static_cast<u8>(std::lround(p.a * 255));
                ar[i * 2 + 1] = static_cast<u8>(std::lround(p.r * dim * 255));
                gb[i * 2] = static_cast<u8>(std::lround(p.g * dim * 255));
                gb[i * 2 + 1] = static_cast<u8>(std::lround(p.b * dim * 255));
            }
            o += 64;
        }
    }
}

// One glyph: bank 0 still, bank 1 the original L/R "press" loop (normal, pressed, normal ...).
struct Glyph {
    ANIMDATA anim;
    ANIMBANK bank[2];
    ANIMFRAME frames[4];
    ANIMPAT pat[2];
    ANIMLAYER layer[2];
    ANIMBMP bmp[2];
    u8 *data[2];
};

Glyph *make_glyph(PbIconFamily f, Ctl c)
{
    Canvas cv;
    draw(cv, f, c);
    Glyph *g = static_cast<Glyph *>(std::calloc(1, sizeof(Glyph)));
    for (int i = 0; i < 2; ++i) {
        g->data[i] = static_cast<u8 *>(std::calloc(1, kSize * kSize * 4));
        to_gx_rgba8(cv, i == 0 ? 1.0f : 0.8f, i == 0 ? 0.0f : 0.06f, g->data[i]);
        ANIMBMP &b = g->bmp[i];
        b.pixSize = 32;
        b.dataFmt = ANIM_BMP_RGBA8;
        b.sizeX = b.sizeY = kSize;
        b.dataSize = kSize * kSize * 4;
        b.data = g->data[i];
        ANIMLAYER &l = g->layer[i];
        l.alpha = 255;
        l.bmpNo = static_cast<s16>(i);
        l.sizeX = l.sizeY = kSize;
        const s16 vtx[8] = { 0, 0, kUnits, 0, kUnits, kUnits, 0, kUnits };
        std::memcpy(l.vtx, vtx, sizeof(vtx));
        ANIMPAT &p = g->pat[i];
        p.layerNum = 1;
        p.centerX = p.centerY = kUnits / 2;
        p.sizeX = p.sizeY = kUnits;
        p.layer = &g->layer[i];
    }
    g->frames[0] = { 0, 16, 0, 0, 0, 0 };
    g->frames[1] = { 0, 32, 0, 0, 0, 0 };
    g->frames[2] = { 1, 32, 0, 0, 0, 0 };
    g->frames[3] = { 0, -1, 0, 0, 0, 0 };
    g->bank[0] = { 1, 0, &g->frames[0] };
    g->bank[1] = { 3, 0, &g->frames[1] };
    ANIMDATA &a = g->anim;
    a.bankNum = 2;
    a.patNum = 2;
    a.bmpNum = 2;
    a.useNum = 1; // never freed: HuSprKill only drops the sprites' references
    a.bank = g->bank;
    a.pat = g->pat;
    a.bmp = g->bmp;
#ifdef BYTESWAPPING
    a.valid = ANIM_DATA_ALLOCATION_VALID;
    a.raw = nullptr;
#endif
    return g;
}

Glyph *glyph(PbIconFamily f, Ctl c)
{
    static Glyph *cache[PB_FAMILY_COUNT][C_COUNT];
    Glyph *&g = cache[f][c];
    if (g == nullptr) {
        g = make_glyph(f, c);
    }
    return g;
}

// ------------------------------------------------------------------ binding lookup

Ctl ctl_for_native_button(u32 b)
{
    switch (b) {
        case SDL_GAMEPAD_BUTTON_SOUTH:
            return C_SOUTH;
        case SDL_GAMEPAD_BUTTON_EAST:
            return C_EAST;
        case SDL_GAMEPAD_BUTTON_WEST:
            return C_WEST;
        case SDL_GAMEPAD_BUTTON_NORTH:
            return C_NORTH;
        case SDL_GAMEPAD_BUTTON_BACK:
            return C_BACK;
        case SDL_GAMEPAD_BUTTON_GUIDE:
            return C_GUIDE;
        case SDL_GAMEPAD_BUTTON_START:
            return C_START;
        case SDL_GAMEPAD_BUTTON_LEFT_STICK:
            return C_LCLICK;
        case SDL_GAMEPAD_BUTTON_RIGHT_STICK:
            return C_RCLICK;
        case SDL_GAMEPAD_BUTTON_LEFT_SHOULDER:
            return C_LSHOULDER;
        case SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER:
            return C_RSHOULDER;
        case SDL_GAMEPAD_BUTTON_DPAD_UP:
            return C_DPAD_UP;
        case SDL_GAMEPAD_BUTTON_DPAD_DOWN:
            return C_DPAD_DOWN;
        case SDL_GAMEPAD_BUTTON_DPAD_LEFT:
            return C_DPAD_LEFT;
        case SDL_GAMEPAD_BUTTON_DPAD_RIGHT:
            return C_DPAD_RIGHT;
        default:
            return C_OTHER;
    }
}

Ctl ctl_for_native_axis(s32 axis)
{
    switch (axis) {
        case SDL_GAMEPAD_AXIS_LEFTX:
        case SDL_GAMEPAD_AXIS_LEFTY:
            return C_LSTICK;
        case SDL_GAMEPAD_AXIS_RIGHTX:
        case SDL_GAMEPAD_AXIS_RIGHTY:
            return C_RSTICK;
        case SDL_GAMEPAD_AXIS_LEFT_TRIGGER:
            return C_LTRIGGER;
        case SDL_GAMEPAD_AXIS_RIGHT_TRIGGER:
            return C_RTRIGGER;
        default:
            return C_OTHER;
    }
}

// The GameCube inputs behind message icons 0-12.
enum GcInput { GC_STICK, GC_DPAD, GC_A, GC_B, GC_X, GC_Y, GC_R, GC_L, GC_CSTICK, GC_Z, GC_START, GC_NONE };

GcInput gc_input_for_entry(int entry)
{
    static const GcInput kTbl[13] = { GC_STICK, GC_DPAD, GC_A, GC_B, GC_X, GC_Y, GC_R, GC_R, GC_L, GC_L, GC_CSTICK, GC_Z, GC_START };
    return entry >= 0 && entry < 13 ? kTbl[entry] : GC_NONE;
}

// aurora's default for a pad without its own mapping (and for the keyboard, which has no native buttons).
Ctl default_ctl(GcInput in)
{
    static const Ctl kTbl[] = { C_LSTICK, C_DPAD, C_SOUTH, C_EAST, C_WEST, C_NORTH, C_RTRIGGER, C_LTRIGGER, C_RSTICK, C_RSHOULDER, C_START };
    return kTbl[in];
}

PADButton pad_button(GcInput in)
{
    switch (in) {
        case GC_A:
            return PAD_BUTTON_A;
        case GC_B:
            return PAD_BUTTON_B;
        case GC_X:
            return PAD_BUTTON_X;
        case GC_Y:
            return PAD_BUTTON_Y;
        case GC_Z:
            return PAD_TRIGGER_Z;
        case GC_START:
            return PAD_BUTTON_START;
        case GC_L:
            return PAD_TRIGGER_L;
        case GC_R:
            return PAD_TRIGGER_R;
        default:
            return 0;
    }
}

// What `in` is bound to on `port`'s controller, through the controller config's mappings (a remapped button shows
// its real binding).
Ctl bound_ctl(int port, GcInput in)
{
    if (PADGetIndexForPort(static_cast<u32>(port)) < 0) {
        return default_ctl(in);
    }
    u32 n = 0;
    if (in == GC_STICK || in == GC_CSTICK || in == GC_L || in == GC_R) {
        const PADAxis want = in == GC_STICK ? PAD_AXIS_LEFT_X_POS : in == GC_CSTICK ? PAD_AXIS_RIGHT_X_POS : in == GC_L ? PAD_AXIS_TRIGGER_L : PAD_AXIS_TRIGGER_R;
        if (PADAxisMapping *axes = PADGetAxisMappings(static_cast<u32>(port), &n)) {
            for (u32 i = 0; i < n; ++i) {
                if (axes[i].padAxis != want) {
                    continue;
                }
                if (axes[i].nativeAxis.nativeAxis != -1) {
                    return ctl_for_native_axis(axes[i].nativeAxis.nativeAxis);
                }
                if (axes[i].nativeButton != -1) {
                    return ctl_for_native_button(static_cast<u32>(axes[i].nativeButton));
                }
            }
        }
        if (in == GC_STICK || in == GC_CSTICK) {
            return default_ctl(in);
        }
        // L/R with no analog trigger: a digital binding (e.g. a pad without trigger axes)
    }
    if (in == GC_DPAD) {
        return C_DPAD;
    }
    if (PADButtonMapping *buttons = PADGetButtonMappings(static_cast<u32>(port), &n)) {
        const PADButton want = pad_button(in);
        for (u32 i = 0; i < n; ++i) {
            if (buttons[i].padButton == want && buttons[i].nativeButton != PAD_NATIVE_BUTTON_INVALID) {
                return ctl_for_native_button(buttons[i].nativeButton);
            }
        }
    }
    return default_ctl(in);
}

PbIconFamily auto_family(int port)
{
    const u32 p = static_cast<u32>(port);
    if (PADGetIndexForPort(p) < 0) {
        // the keyboard or nothing: Auto's fallback
        return PB_FAMILY_XBOX;
    }
    u32 vid = 0, pid = 0;
    PADGetVidPid(p, &vid, &pid);
    if (vid == 0x28de && pid == 0x1205) {
        return PB_FAMILY_STEAMDECK;
    }
    if (PADIsGCAdapter(p) || (vid == 0x057e && pid == 0x2073)) {
        return PB_FAMILY_GAMECUBE;
    }
    switch (static_cast<SDL_GamepadType>(PADGetControllerType(p))) {
        case SDL_GAMEPAD_TYPE_PS3:
        case SDL_GAMEPAD_TYPE_PS4:
        case SDL_GAMEPAD_TYPE_PS5:
            return PB_FAMILY_PLAYSTATION;
        case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_PRO:
        case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_LEFT:
        case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_RIGHT:
        case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_PAIR:
            return PB_FAMILY_SWITCH;
        default:
            return PB_FAMILY_XBOX;
    }
}

// PB_ICON_FAMILY (test hook): forces the family Auto picks, e.g. "xbox", for runs without that controller.
int forced_family()
{
    static int cached = -2;
    if (cached == -2) {
        cached = -1;
        if (const char *e = std::getenv("PB_ICON_FAMILY")) {
            static const char *kNames[PB_FAMILY_COUNT] = { "gamecube", "playstation", "xbox", "switch", "generic", "steamdeck" };
            for (int i = 0; i < PB_FAMILY_COUNT; ++i) {
                if (std::strcmp(e, kNames[i]) == 0) {
                    cached = i;
                }
            }
        }
    }
    return cached;
}

} // namespace

extern "C" {

PbIconFamily pb_button_icons_family(int port)
{
    switch (partyboard::getSettings().game.buttonIcons.getValue()) {
        case PB_ICONS_PLAYSTATION:
            return PB_FAMILY_PLAYSTATION;
        case PB_ICONS_XBOX:
            return PB_FAMILY_XBOX;
        case PB_ICONS_SWITCH:
            return PB_FAMILY_SWITCH;
        case PB_ICONS_GENERIC:
            return PB_FAMILY_GENERIC;
        case PB_ICONS_GAMECUBE:
            return PB_FAMILY_GAMECUBE;
        default:
            break;
    }
    if (forced_family() >= 0) {
        return static_cast<PbIconFamily>(forced_family());
    }
    return auto_family(port);
}

const char *pb_button_icons_family_name(PbIconFamily family)
{
    static const char *kNames[PB_FAMILY_COUNT] = { "GameCube", "PlayStation", "Xbox", "Switch", "Generic", "Steam Deck" };
    return family < PB_FAMILY_COUNT ? kNames[family] : "?";
}

int pb_button_icon_for_window(int entry, u32 pad_mask, ANIMDATA **anim, s16 *bank)
{
    const GcInput in = gc_input_for_entry(entry);
    if (in == GC_NONE) {
        return 0;
    }
    int port = 0;
    for (int i = 0; i < 4; ++i) {
        if (pad_mask & (1u << i)) {
            port = i;
            break;
        }
    }
    const PbIconFamily f = pb_button_icons_family(port);
    if (f == PB_FAMILY_GAMECUBE) {
        return 0;
    }
    const Ctl c = bound_ctl(port, in);
    if (std::getenv("PB_ICON_LOG")) {
        // PB_ICON_LOG (test hook): each swap, once per overlay
        static int logged_ovl = -2;
        static bool logged[13];
        if (logged_ovl != omcurovl) {
            logged_ovl = omcurovl;
            std::memset(logged, 0, sizeof(logged));
        }
        if (!logged[entry]) {
            logged[entry] = true;
            std::fprintf(stderr, "button_icons: ovl %d icon %d port %d -> %s glyph %d\n", static_cast<int>(omcurovl), entry, port,
                pb_button_icons_family_name(f), c);
        }
    }
    *anim = &glyph(f, c)->anim;
    // the original's animated L/R banks (7, 9) loop a press
    *bank = (entry == 7 || entry == 9) ? 1 : 0;
    return 1;
}

int pb_button_icons_dump_sheet(const char *path)
{
    FILE *out = std::fopen(path, "wb");
    if (out == nullptr) {
        return -1;
    }
    const u32 w = kSize * C_COUNT, h = kSize * (PB_FAMILY_COUNT - 1);
    std::fwrite(&w, 4, 1, out);
    std::fwrite(&h, 4, 1, out);
    std::vector<u8> rgba(static_cast<size_t>(w) * h * 4);
    for (int f = 1; f < PB_FAMILY_COUNT; ++f) {
        for (int c = 0; c < C_COUNT; ++c) {
            Canvas cv;
            draw(cv, static_cast<PbIconFamily>(f), static_cast<Ctl>(c));
            for (int y = 0; y < kSize; ++y) {
                for (int x = 0; x < kSize; ++x) {
                    const Rgba &p = cv.px[y * kSize + x];
                    u8 *d = &rgba[((static_cast<size_t>(f - 1) * kSize + y) * w + c * kSize + x) * 4];
                    d[0] = static_cast<u8>(std::lround(p.r * 255));
                    d[1] = static_cast<u8>(std::lround(p.g * 255));
                    d[2] = static_cast<u8>(std::lround(p.b * 255));
                    d[3] = static_cast<u8>(std::lround(p.a * 255));
                }
            }
        }
    }
    std::fwrite(rgba.data(), 1, rgba.size(), out);
    std::fclose(out);
    return 0;
}
}
