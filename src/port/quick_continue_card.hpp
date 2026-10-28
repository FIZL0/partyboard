#pragma once

#include "port/quick_continue.h"

#include <vector>

namespace partyboard::quick_continue {

// The games in progress on slot A's card and the party autosave (quick_continue_list's order), for a disc of
// that region.
std::vector<QuickContinueEntry> scan(bool pal);

} // namespace partyboard::quick_continue
