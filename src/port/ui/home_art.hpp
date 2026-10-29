#pragma once

// N141: the home screen's backdrop from the disc (home_art.cpp).

#include <RmlUi/Core.h>

namespace partyboard::ui {

// The image source of the title screen's background as the disc has it ("pbdisc://title-bg"), read on the first call.
// Empty without it: no disc open yet, or no such picture on it (the menu keeps its own backdrop then).
Rml::String home_art_title_background();

} // namespace partyboard::ui
