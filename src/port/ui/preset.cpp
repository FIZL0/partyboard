// Credits: TwilitRealm

#include "preset.hpp"

#include "port/settings.h"

namespace partyboard::ui {

void applyPresetPartyBoard()
{
    auto &s = getSettings();
    s.game.enableAchievementToasts.setValue(true);
    s.game.enableControllerToasts.setValue(true);
    s.game.enableQuickTransform.setValue(true);
    s.game.internalResolutionScale.setValue(0);
    s.game.shadowResolutionMultiplier.setValue(4);
}

} // namespace partyboard::ui
