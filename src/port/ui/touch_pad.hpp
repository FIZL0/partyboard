#pragma once

#include "document.hpp"

#include <array>

namespace partyboard::ui {

// N70: the on-screen game pad (include/port/touch_pad.h). A passive document: draws the pad, takes no focus.
class TouchPad : public Document {
public:
    TouchPad();

    void show() override;
    void update() override;

protected:
    bool handle_nav_command(Rml::Event &event, NavCommand cmd) override;

    Rml::Element *mPad = nullptr;
    Rml::Element *mKnob = nullptr;
    std::array<Rml::Element *, 9> mControls {};
    std::array<float, 9 * 4 + 3> mPlaced {};
    bool mShown = false;
};

namespace touch_pad {
    // Finger, controller and keyboard events: the pad's touches, shown/hidden.
    void handle_event(const SDL_Event &event) noexcept;
} // namespace touch_pad

} // namespace partyboard::ui
