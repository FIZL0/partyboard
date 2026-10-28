// N129: the saving icon's state (include/port/save_icon.h), read by the overlay each frame.

#pragma once

namespace partyboard::ui {

// The saving icon should be on screen now: a save is running, or one began less than the minimum time ago.
bool save_icon_wanted() noexcept;

} // namespace partyboard::ui
