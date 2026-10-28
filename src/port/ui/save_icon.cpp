// N129: the saving icon (include/port/save_icon.h). The game calls begin/end around a save; the overlay shows the
// icon while save_icon_wanted(). Wall-clock only: nothing here reaches the game's state or frames.

#include "save_icon.hpp"

#include "port/save_icon.h"
#include "ui.hpp"

#include <aurora/frame_capture_ui.h>

#include <atomic>
#include <cstdio>
#include <cstdlib>

namespace partyboard::ui {
namespace {
    // Long enough to be seen when the save itself takes a few milliseconds.
    constexpr auto kMinShown = std::chrono::milliseconds(1500);

    std::atomic<int> sSaving { 0 };
    std::atomic<clock::rep> sBegin { 0 };
    std::atomic<bool> sEverBegun { false };
    // PB_ICON_SHOT=<dir> (test hook, as N60's): at each save, the frame as shown (game + RmlUi) 2, 30 and 600 drawn
    // frames later (or PB_SAVE_SHOT_DELAYS) as <dir>/save-NN-dNNN.png (aurora_capture_frame_ui).
    void shot_save(int save)
    {
        static const char *dir = std::getenv("PB_ICON_SHOT");
        if (dir == nullptr) {
            return;
        }
        const char *delays = std::getenv("PB_SAVE_SHOT_DELAYS");
        for (const char *d = delays ? delays : "2,30,600"; *d != '\0';) {
            char *end;
            const unsigned long delay = std::strtoul(d, &end, 10);
            if (end == d) {
                break;
            }
            d = *end == ',' ? end + 1 : end;
            char path[1024];
            std::snprintf(path, sizeof(path), "%s/save-%02d-d%03lu.png", dir, save, delay);
            aurora_capture_frame_ui(path, static_cast<uint32_t>(delay));
        }
    }
} // namespace

bool save_icon_wanted() noexcept
{
    if (sSaving.load() > 0) {
        return true;
    }
    if (!sEverBegun.load()) {
        return false;
    }
    const clock::time_point begin { clock::duration { sBegin.load() } };
    return clock::now() < begin + kMinShown;
}

} // namespace partyboard::ui

extern "C" void port_save_icon_begin(void)
{
    using namespace partyboard::ui;
    sBegin.store(clock::now().time_since_epoch().count());
    sEverBegun.store(true);
    sSaving.fetch_add(1);
    static int sSaves;
    shot_save(sSaves++);
}

extern "C" void port_save_icon_end(void)
{
    using namespace partyboard::ui;
    if (sSaving.load() > 0) {
        sSaving.fetch_sub(1);
    }
}
