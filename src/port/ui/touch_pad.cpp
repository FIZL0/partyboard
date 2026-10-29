// N70: on-screen game pad for touch screens (include/port/touch_pad.h).
//
// Touches are hit-tested against a layout computed from the RmlUi context's size (the space SDL's normalized finger
// positions map to, as in input.cpp) and fed to controller port 1 with PADSetVirtualStatus: aurora merges it into
// PADRead's status for that port (buttons OR'd, the larger stick deflection wins) and drops it while a menu blocks
// pad input. Netplay reads local pads through the same PADRead, so online the touches are this machine's first
// seat. A pad that was never touched sets nothing: desktop play without a touch screen is unchanged.

#include "touch_pad.hpp"

#include "port/settings.h"
#include "port/touch_haptics.h"
#include "port/touch_pad.h"

#include <RmlUi/Core.h>
#include <SDL3/SDL_events.h>
#include <SDL3/SDL_touch.h>
#include <aurora/rmlui.hpp>
#include <dolphin/pad.h>

#include <algorithm>
#include <array>
#include <cmath>

namespace partyboard::ui {
namespace {

    enum Control : int { kStick, kA, kB, kX, kY, kL, kR, kZ, kStart, kControlCount, kNone = -1 };

    struct ControlInfo {
        const char *id;
        const char *label;
        u16 button;
        bool round;
    };

    constexpr std::array<ControlInfo, kControlCount> kControls { {
        { "stick", "", 0, true },
        { "a", "A", PAD_BUTTON_A, true },
        { "b", "B", PAD_BUTTON_B, true },
        { "x", "X", PAD_BUTTON_X, true },
        { "y", "Y", PAD_BUTTON_Y, true },
        { "l", "L", PAD_TRIGGER_L, false },
        { "r", "R", PAD_TRIGGER_R, false },
        { "z", "Z", PAD_TRIGGER_Z, false },
        { "start", "START", PAD_BUTTON_START, false },
    } };

    // A control's shape: round ones are circles of radius half.x, the others rounded boxes.
    struct Box {
        Rml::Vector2f center;
        Rml::Vector2f half;
    };

    struct Layout {
        std::array<Box, kControlCount> boxes;
        float knobRadius = 0.0f;
        // Below this, a touch on the left half of the screen that hits no button takes the stick.
        float stickZoneTop = 0.0f;
        float width = 0.0f;
        float unit = 0.0f;
    };

    struct Finger {
        SDL_FingerID id = 0;
        Control control = kNone;
        bool active = false;
    };

    constexpr int kMaxFingers = 10;
    constexpr float kStickDeadZone = 0.12f;
    // The analog value aurora gives a digital L/R press (pad.cpp).
    constexpr u8 kTriggerPressed = 180;
    // N111: the stick's haptic tick: past this deflection (of the base's radius) it is in one of 8 directions, and
    // back under kStickDirectionRelease it is centred again (the gap stops a finger on the edge from buzzing).
    constexpr float kStickDirectionPress = 0.5f;
    constexpr float kStickDirectionRelease = 0.35f;
    // Of a direction's 45 degree sector: how far past its edge the stick goes before the next direction takes over.
    constexpr float kStickSectorSlack = 0.15f;
    // A controller stick moved this far hides the pad; less is drift.
    constexpr Sint16 kAxisHideThreshold = 16000;

    struct State {
        bool shown = false;
        bool published = false;
        std::array<Finger, kMaxFingers> fingers;
        // Pressed since the last frame: a tap that is down and up within one frame's events still reaches PADRead.
        u16 latched = 0;
        int stickFinger = -1;
        // N111: the stick's 8-way direction (0 = east, counter-clockwise on screen), -1 centred.
        int stickDirection = -1;
        Rml::Vector2f stickOrigin;
        Rml::Vector2f stickTouch;
    };

    State sState;

    bool enabled() noexcept
    {
        return getSettings().game.touchControls.getValue();
    }

    // Live: shown, and no menu is open (menus take touches themselves and block pad input).
    bool live() noexcept
    {
        return sState.shown && enabled() && !any_document_visible();
    }

    Layout compute_layout(Rml::Context &context) noexcept
    {
        const auto dims = context.GetDimensions();
        const Insets in = safe_area_insets(&context);
        const float w = static_cast<float>(dims.x);
        const float h = static_cast<float>(dims.y);
        const float u = std::min(w, h);
        const float m = 0.05f * u;
        const float left = in.left + m;
        const float right = w - in.right - m;
        const float top = in.top + m;
        const float bottom = h - in.bottom - m;

        Layout l;
        l.width = w;
        l.unit = u;
        const auto round = [](Rml::Vector2f c, float r) { return Box { c, { r, r } }; };

        const float stickR = 0.15f * u;
        l.boxes[kStick] = round({ left + stickR, bottom - stickR }, stickR);
        l.knobRadius = 0.065f * u;

        // GameCube face buttons: a big A, B down-left of it, X to its right, Y above it.
        const float faceR = 0.055f * u;
        const Rml::Vector2f face { right - 0.21f * u, bottom - 0.2f * u };
        l.boxes[kA] = round(face, 0.085f * u);
        l.boxes[kB] = round(face + Rml::Vector2f { -0.155f * u, 0.06f * u }, faceR);
        l.boxes[kX] = round(face + Rml::Vector2f { 0.155f * u, -0.045f * u }, faceR);
        l.boxes[kY] = round(face + Rml::Vector2f { -0.045f * u, -0.155f * u }, faceR);

        const Rml::Vector2f shoulder { 0.11f * u, 0.045f * u };
        l.boxes[kL] = { { left + shoulder.x, top + shoulder.y }, shoulder };
        l.boxes[kR] = { { right - shoulder.x, top + shoulder.y }, shoulder };
        const Rml::Vector2f z { 0.08f * u, 0.04f * u };
        l.boxes[kZ] = { { right - z.x, top + 2.0f * shoulder.y + 0.03f * u + z.y }, z };
        const Rml::Vector2f start { 0.07f * u, 0.035f * u };
        l.boxes[kStart] = { { w * 0.5f, top + start.y }, start };

        l.stickZoneTop = top + 2.0f * shoulder.y + 0.03f * u;
        return l;
    }

    // Distance from p to the control's edge (0 inside).
    float distance_to(const Layout &l, Control c, Rml::Vector2f p) noexcept
    {
        const Box &b = l.boxes[c];
        const Rml::Vector2f d = p - b.center;
        if (kControls[c].round) {
            return std::max(0.0f, std::sqrt(d.x * d.x + d.y * d.y) - b.half.x);
        }
        const float dx = std::max(0.0f, std::abs(d.x) - b.half.x);
        const float dy = std::max(0.0f, std::abs(d.y) - b.half.y);
        return std::sqrt(dx * dx + dy * dy);
    }

    // The button under p (with some slack, the nearest wins), else the stick if p is in its zone.
    Control hit_test(const Layout &l, Rml::Vector2f p, bool buttonsOnly) noexcept
    {
        const float slack = 0.035f * l.unit;
        Control best = kNone;
        float bestDistance = slack;
        for (int c = kA; c < kControlCount; ++c) {
            const float d = distance_to(l, static_cast<Control>(c), p);
            if (d <= bestDistance) {
                best = static_cast<Control>(c);
                bestDistance = d;
            }
        }
        if (best != kNone || buttonsOnly) {
            return best;
        }
        if (distance_to(l, kStick, p) <= slack || (p.x < l.width * 0.5f && p.y >= l.stickZoneTop)) {
            return kStick;
        }
        return kNone;
    }

    Rml::Vector2f finger_position(const SDL_TouchFingerEvent &event, Rml::Context &context) noexcept
    {
        return touch_to_context(event.x, event.y, context);
    }

    Finger *find_finger(SDL_FingerID id) noexcept
    {
        for (auto &f : sState.fingers) {
            if (f.active && f.id == id) {
                return &f;
            }
        }
        return nullptr;
    }

    int free_finger() noexcept
    {
        for (int i = 0; i < kMaxFingers; ++i) {
            if (!sState.fingers[i].active) {
                return i;
            }
        }
        return -1;
    }

    // The knob's offset from the stick's centre, at most the base's radius.
    Rml::Vector2f stick_offset(const Layout &l) noexcept
    {
        if (sState.stickFinger < 0) {
            return {};
        }
        Rml::Vector2f d = sState.stickTouch - sState.stickOrigin;
        const float r = l.boxes[kStick].half.x;
        const float len = std::sqrt(d.x * d.x + d.y * d.y);
        if (len > r && len > 0.0f) {
            d = d * (r / len);
        }
        return d;
    }

    Rml::Vector2f stick_center(const Layout &l) noexcept
    {
        return sState.stickFinger >= 0 ? sState.stickOrigin : l.boxes[kStick].center;
    }

    u16 held_buttons() noexcept
    {
        u16 buttons = 0;
        for (const auto &f : sState.fingers) {
            if (f.active && f.control > kStick) {
                buttons |= kControls[f.control].button;
            }
        }
        return buttons;
    }

    void publish() noexcept
    {
        auto *context = aurora::rmlui::get_context();
        if (!live() || context == nullptr) {
            if (sState.published) {
                PADClearVirtualStatus(touch_pad_port());
                sState.published = false;
            }
            return;
        }

        PADStatus status {};
        status.button = held_buttons() | sState.latched;
        if (status.button & PAD_TRIGGER_L) {
            status.triggerLeft = kTriggerPressed;
        }
        if (status.button & PAD_TRIGGER_R) {
            status.triggerRight = kTriggerPressed;
        }
        const Layout l = compute_layout(*context);
        const Rml::Vector2f v = stick_offset(l) / l.boxes[kStick].half.x;
        if (std::sqrt(v.x * v.x + v.y * v.y) >= kStickDeadZone) {
            status.stickX = static_cast<s8>(std::lround(std::clamp(v.x, -1.0f, 1.0f) * 127.0f));
            // Screen y grows down, the pad's up.
            status.stickY = static_cast<s8>(std::lround(std::clamp(-v.y, -1.0f, 1.0f) * 127.0f));
        }
        PADSetVirtualStatus(touch_pad_port(), &status);
        sState.published = true;
    }

    // N111: a light tick each time the stick moves into another of its 8 directions (not when it re-centres).
    void update_stick_direction(const Layout &l) noexcept
    {
        const Rml::Vector2f v = stick_offset(l) / l.boxes[kStick].half.x;
        const float len = std::sqrt(v.x * v.x + v.y * v.y);
        if (len < kStickDirectionRelease || (sState.stickDirection < 0 && len < kStickDirectionPress)) {
            sState.stickDirection = -1;
            return;
        }
        if (len < kStickDirectionPress) {
            return;
        }
        const float sector = std::atan2(-v.y, v.x) / (2.0f * 3.14159265f) * 8.0f;
        if (sState.stickDirection >= 0) {
            // Stay in the current direction until a bit past its edge: a finger on the line between two would buzz.
            const float off = std::abs(std::remainder(sector - static_cast<float>(sState.stickDirection), 8.0f));
            if (off < 0.5f + kStickSectorSlack) {
                return;
            }
        }
        const int direction = (static_cast<int>(std::lround(sector)) + 8) % 8;
        if (direction != sState.stickDirection) {
            sState.stickDirection = direction;
            touch_haptic(TOUCH_HAPTIC_STICK);
        }
    }

    void release_fingers() noexcept
    {
        sState.fingers = {};
        sState.stickFinger = -1;
        sState.stickDirection = -1;
        sState.latched = 0;
    }

    void hide() noexcept
    {
        if (!sState.shown) {
            return;
        }
        sState.shown = false;
        release_fingers();
    }

    void finger_down(const SDL_TouchFingerEvent &event) noexcept
    {
        if (!enabled()) {
            return;
        }
        sState.shown = true;
        auto *context = aurora::rmlui::get_context();
        // A touch on a menu is the menu's; one that lands while the menu closes stays the menu's too.
        if (context == nullptr || any_document_visible() || find_finger(event.fingerID) != nullptr) {
            return;
        }
        const int slot = free_finger();
        if (slot < 0) {
            return;
        }
        const Layout l = compute_layout(*context);
        const Rml::Vector2f p = finger_position(event, *context);
        const Control c = hit_test(l, p, false);
        if (c == kNone || (c == kStick && sState.stickFinger >= 0)) {
            return;
        }
        sState.fingers[slot] = { event.fingerID, c, true };
        if (c == kStick) {
            // A touch on the base moves the knob from its centre; one beside it moves the base there (a floating
            // stick, kept on screen).
            const Box &base = l.boxes[kStick];
            Rml::Vector2f origin = base.center;
            if (distance_to(l, kStick, p) > 0.0f) {
                const float r = base.half.x;
                const auto dims = context->GetDimensions();
                origin = { std::clamp(p.x, r, std::max(r, static_cast<float>(dims.x) * 0.5f - r)),
                    std::clamp(p.y, l.stickZoneTop + r, std::max(l.stickZoneTop + r, static_cast<float>(dims.y) - r)) };
            }
            sState.stickFinger = slot;
            sState.stickOrigin = origin;
            sState.stickTouch = p;
            update_stick_direction(l);
        }
        else {
            sState.latched |= kControls[c].button;
            touch_haptic(TOUCH_HAPTIC_PRESS);
        }
    }

    void finger_motion(const SDL_TouchFingerEvent &event) noexcept
    {
        Finger *f = find_finger(event.fingerID);
        auto *context = aurora::rmlui::get_context();
        if (f == nullptr || context == nullptr) {
            return;
        }
        const Rml::Vector2f p = finger_position(event, *context);
        if (f->control == kStick) {
            sState.stickTouch = p;
            update_stick_direction(compute_layout(*context));
            return;
        }
        // Sliding onto another button presses that one instead (A to B without lifting).
        const Control c = hit_test(compute_layout(*context), p, true);
        if (c != kNone && c != f->control) {
            f->control = c;
            sState.latched |= kControls[c].button;
            touch_haptic(TOUCH_HAPTIC_PRESS);
        }
    }

    void finger_up(const SDL_TouchFingerEvent &event) noexcept
    {
        Finger *f = find_finger(event.fingerID);
        if (f == nullptr) {
            return;
        }
        if (f->control == kStick) {
            sState.stickFinger = -1;
            sState.stickDirection = -1;
        }
        *f = {};
    }

    bool hides_pad(const SDL_KeyboardEvent &key) noexcept
    {
        // Phone volume keys come as key events too.
        return !key.repeat && key.scancode != SDL_SCANCODE_VOLUMEUP && key.scancode != SDL_SCANCODE_VOLUMEDOWN
            && key.scancode != SDL_SCANCODE_MUTE;
    }

    const Rml::String kDocumentSource = R"RML(
<rml>
<head>
<style>
body {
    width: 100%;
    height: 100%;
    margin: 0;
    padding: 0;
    pointer-events: none;
    font-family: "FOT-NewRodin Pro";
    font-weight: normal;
}
tp-pad {
    display: block;
    position: absolute;
    left: 0;
    top: 0;
    width: 100%;
    height: 100%;
    opacity: 0;
    transition: opacity 0.15s linear-in-out;
}
tp-pad.shown {
    opacity: 1;
}
tp-ctl, tp-knob {
    display: block;
    position: absolute;
    box-sizing: border-box;
    border: 2dp rgba(255, 255, 255, 50%);
    background-color: rgba(20, 20, 30, 28%);
    color: rgba(255, 255, 255, 80%);
    text-align: center;
}
tp-ctl#a { background-color: rgba(30, 170, 110, 35%); }
tp-ctl#b { background-color: rgba(200, 50, 50, 35%); }
tp-ctl#z { background-color: rgba(110, 70, 190, 35%); }
tp-ctl#stick { background-color: rgba(20, 20, 30, 20%); }
tp-ctl.pressed { background-color: rgba(255, 255, 255, 45%); color: rgba(20, 20, 30, 90%); }
tp-knob { background-color: rgba(255, 255, 255, 38%); border-color: rgba(255, 255, 255, 65%); }
</style>
</head>
<body><tp-pad id="pad" /></body>
</rml>
)RML";

    void place(Rml::Element *e, Rml::Vector2f center, Rml::Vector2f half, bool round) noexcept
    {
        e->SetProperty(Rml::PropertyId::Left, Rml::Property(center.x - half.x, Rml::Unit::PX));
        e->SetProperty(Rml::PropertyId::Top, Rml::Property(center.y - half.y, Rml::Unit::PX));
        e->SetProperty(Rml::PropertyId::Width, Rml::Property(2.0f * half.x, Rml::Unit::PX));
        e->SetProperty(Rml::PropertyId::Height, Rml::Property(2.0f * half.y, Rml::Unit::PX));
        const float radius = round ? half.x : std::min(half.x, half.y);
        const Rml::Property r(radius, Rml::Unit::PX);
        e->SetProperty(Rml::PropertyId::BorderTopLeftRadius, r);
        e->SetProperty(Rml::PropertyId::BorderTopRightRadius, r);
        e->SetProperty(Rml::PropertyId::BorderBottomLeftRadius, r);
        e->SetProperty(Rml::PropertyId::BorderBottomRightRadius, r);
        e->SetProperty(Rml::PropertyId::LineHeight, Rml::Property(2.0f * half.y, Rml::Unit::PX));
        e->SetProperty(Rml::PropertyId::FontSize, Rml::Property(std::min(half.y * 0.9f, half.x * 0.5f), Rml::Unit::PX));
    }

} // namespace

TouchPad::TouchPad()
    : Document(kDocumentSource)
{
    if (mDocument == nullptr) {
        return;
    }
    mPad = mDocument->GetElementById("pad");
    if (mPad == nullptr) {
        return;
    }
    for (int c = 0; c < kControlCount; ++c) {
        auto element = mDocument->CreateElement("tp-ctl");
        element->SetId(kControls[c].id);
        element->SetInnerRML(kControls[c].label);
        mControls[c] = mPad->AppendChild(std::move(element));
    }
    mKnob = mPad->AppendChild(mDocument->CreateElement("tp-knob"));
}

void TouchPad::show()
{
    if (mDocument != nullptr) {
        mDocument->Show(Rml::ModalFlag::None, Rml::FocusFlag::None, Rml::ScrollFlag::None);
    }
}

void TouchPad::update()
{
    Document::update();
    // The frame's PADRead has seen this frame's taps: drop them (held buttons stay).
    sState.latched = 0;
    publish();

    auto *context = aurora::rmlui::get_context();
    if (mPad == nullptr || mKnob == nullptr || context == nullptr) {
        return;
    }
    const bool shown = live();
    if (shown != mShown) {
        mPad->SetClass("shown", shown);
        mShown = shown;
    }
    if (!shown) {
        return;
    }

    const Layout l = compute_layout(*context);
    const u16 held = held_buttons();
    const Rml::Vector2f stickCenter = stick_center(l);
    const Rml::Vector2f knob = stickCenter + stick_offset(l);
    // Only touch the elements when something moved: setting a property dirties the layout.
    std::array<float, 9 * 4 + 3> placed {};
    for (int c = 0; c < kControlCount; ++c) {
        const Rml::Vector2f center = c == kStick ? stickCenter : l.boxes[c].center;
        placed[c * 4 + 0] = center.x;
        placed[c * 4 + 1] = center.y;
        placed[c * 4 + 2] = l.boxes[c].half.x;
        placed[c * 4 + 3] = l.boxes[c].half.y;
    }
    placed[kControlCount * 4 + 0] = knob.x;
    placed[kControlCount * 4 + 1] = knob.y;
    placed[kControlCount * 4 + 2] = l.knobRadius;
    if (placed != mPlaced) {
        for (int c = 0; c < kControlCount; ++c) {
            place(mControls[c], { placed[c * 4 + 0], placed[c * 4 + 1] }, l.boxes[c].half, kControls[c].round);
        }
        place(mKnob, knob, { l.knobRadius, l.knobRadius }, true);
        mPlaced = placed;
    }
    for (int c = kA; c < kControlCount; ++c) {
        mControls[c]->SetClass("pressed", (held & kControls[c].button) != 0);
    }
    mControls[kStick]->SetClass("pressed", sState.stickFinger >= 0);
}

bool TouchPad::handle_nav_command(Rml::Event &, NavCommand)
{
    return false;
}

namespace touch_pad {
    void handle_event(const SDL_Event &event) noexcept
    {
        switch (event.type) {
            case SDL_EVENT_FINGER_DOWN:
                // Touches SDL makes from the mouse are the mouse's.
                if (event.tfinger.touchID == SDL_MOUSE_TOUCHID) {
                    return;
                }
                finger_down(event.tfinger);
                break;
            case SDL_EVENT_FINGER_MOTION:
                finger_motion(event.tfinger);
                break;
            case SDL_EVENT_FINGER_UP:
            case SDL_EVENT_FINGER_CANCELED:
                finger_up(event.tfinger);
                break;
            case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
                hide();
                break;
            case SDL_EVENT_GAMEPAD_AXIS_MOTION:
                if (std::abs(static_cast<int>(event.gaxis.value)) < kAxisHideThreshold) {
                    return;
                }
                hide();
                break;
            case SDL_EVENT_KEY_DOWN:
                if (!hides_pad(event.key)) {
                    return;
                }
                hide();
                break;
            case SDL_EVENT_WINDOW_FOCUS_LOST:
                release_fingers();
                break;
            default:
                return;
        }
        publish();
    }
} // namespace touch_pad

} // namespace partyboard::ui

extern "C" {

bool touch_pad_shown(void)
{
    return partyboard::ui::sState.shown && partyboard::ui::enabled();
}

int touch_pad_port(void)
{
    return PAD_CHAN0;
}
}
