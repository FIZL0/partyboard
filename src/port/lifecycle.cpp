// N110: the app's lifecycle. iOS sends SDL_EVENT_WILL_ENTER_BACKGROUND when the app resigns active (Control Center,
// the Local Network prompt) and DID_ENTER_BACKGROUND when it goes to the background; Android sends them on onPause.
// A backgrounded app that keeps submitting Metal work is killed by iOS, so the game holds (no tick, no aurora frame)
// from the first of them until DID_ENTER_FOREGROUND; a desktop window minimised or hidden holds the same way.
//
// The events are taken from an SDL event watch: it runs synchronously when SDL sends the event (on iOS inside the
// UIKit callback, before it returns), like an SDL_SetEventFilter, but it can't drop events and doesn't replace a
// filter someone else set.

#include <port/lifecycle.h>

#include <aurora/aurora.h>
#include <aurora/gfx.h>
#include <aurora/lib/logging.hpp>
#include <fmt/format.h>
#include <port/config.hpp>
#include <port/pbnet.h>

#include <SDL3/SDL_events.h>
#include <SDL3/SDL_timer.h>
#include <SDL3/SDL_video.h>

#include <atomic>
#include <cstdlib>
#include <string>
#include <vector>

extern AuroraInfo auroraInfo;

namespace {
aurora::Module Log("partyboard::lifecycle");

// From WILL_ENTER_BACKGROUND (or DID_ENTER_BACKGROUND) to DID_ENTER_FOREGROUND. Atomic: Android may send the events
// on another thread than the game's.
std::atomic_bool s_background{false};
// A save of the settings is due (the app went to the background): done on the game thread.
std::atomic_bool s_saveDue{false};

// The game loop holds (port_lifecycle_hold). Atomic: the watch reads it.
std::atomic_bool s_holding{false};
uint64_t s_heldSince = 0;
uint32_t s_heldLoops = 0;

// PB_LIFECYCLE_TEST="bg@300,fg@420,hide@600,show@700,min@800,restore@900": at the loop count, do the action (bg/fg:
// push the iOS/Android app events, hide/show/min/restore: the window, stall: the game thread sleeps 8 s, pause: bg,
// 8 s asleep and fg in one go, as Android's onPause blocks the game thread inside SDL's event pump, lose: the GPU device
// is lost as in a driver reset, shot: the game frame to $PB_LIFECYCLE_SHOT/loopNNNNN.png, N118). Desktop checks of the
// hold (journal N110).
// aurora_update sleeps in SDL_WaitEvent while the window is paused (hidden): a timer wakes it 20 times a second while
// steps are left, so the loop count goes on.
struct TestStep {
    uint32_t loop;
    std::string action;
};
std::vector<TestStep> s_testSteps;
uint32_t s_loops = 0;

void push_app_event(SDL_EventType type)
{
    SDL_Event event{};
    event.type = type;
    event.common.timestamp = SDL_GetTicksNS();
    SDL_PushEvent(&event);
}

void run_test_steps()
{
    ++s_loops;
    for (const auto& step : s_testSteps) {
        if (step.loop != s_loops) {
            continue;
        }
        Log.info("test: {} at loop {}", step.action, s_loops);
        auto* window = static_cast<SDL_Window*>(auroraInfo.window);
        if (step.action == "bg") {
            push_app_event(SDL_EVENT_WILL_ENTER_BACKGROUND);
            push_app_event(SDL_EVENT_DID_ENTER_BACKGROUND);
        } else if (step.action == "fg") {
            push_app_event(SDL_EVENT_WILL_ENTER_FOREGROUND);
            push_app_event(SDL_EVENT_DID_ENTER_FOREGROUND);
        } else if (step.action == "hide") {
            SDL_HideWindow(window);
        } else if (step.action == "show") {
            SDL_ShowWindow(window);
        } else if (step.action == "min") {
            SDL_MinimizeWindow(window);
        } else if (step.action == "restore") {
            SDL_RestoreWindow(window);
        } else if (step.action == "stall") {
            SDL_Delay(8000);
        } else if (step.action == "pause") {
            push_app_event(SDL_EVENT_WILL_ENTER_BACKGROUND);
            push_app_event(SDL_EVENT_DID_ENTER_BACKGROUND);
            SDL_Delay(8000);
            push_app_event(SDL_EVENT_WILL_ENTER_FOREGROUND);
            push_app_event(SDL_EVENT_DID_ENTER_FOREGROUND);
        } else if (step.action == "lose") {
            aurora_force_device_loss();
        } else if (step.action == "shot") {
            const char* dir = std::getenv("PB_LIFECYCLE_SHOT");
            const std::string path = fmt::format("{}/loop{:05}.png", dir != nullptr ? dir : ".", s_loops);
            aurora_capture_frame(path.c_str(), 0);
        } else {
            Log.warn("test: unknown action '{}'", step.action);
        }
    }
}

void parse_test_steps()
{
    const char* spec = std::getenv("PB_LIFECYCLE_TEST");
    if (spec == nullptr) {
        return;
    }
    std::string rest = spec;
    while (!rest.empty()) {
        const auto comma = rest.find(',');
        const std::string item = rest.substr(0, comma);
        rest = comma == std::string::npos ? std::string() : rest.substr(comma + 1);
        const auto at = item.find('@');
        if (at == std::string::npos) {
            continue;
        }
        s_testSteps.push_back({static_cast<uint32_t>(std::strtoul(item.c_str() + at + 1, nullptr, 10)), item.substr(0, at)});
    }
    Log.info("test: {} steps from PB_LIFECYCLE_TEST", s_testSteps.size());
    SDL_AddTimer(50, [](void*, SDL_TimerID, Uint32 interval) -> Uint32 {
        SDL_Event wake{};
        wake.type = SDL_EVENT_USER;
        SDL_PushEvent(&wake);
        return interval;
    }, nullptr);
}

bool SDLCALL watch(void*, SDL_Event* event)
{
    switch (event->type) {
    case SDL_EVENT_WILL_ENTER_BACKGROUND:
    case SDL_EVENT_DID_ENTER_BACKGROUND:
        if (!s_background.exchange(true)) {
            s_saveDue = true;
            // now, not at the game loop's next hold: Android blocks the game thread in SDL's event pump until
            // onResume, so the loop may never see it; the keeper thread speaks for the match meanwhile
            pbnet_keeper_hidden(1);
        }
        Log.info("{}", event->type == SDL_EVENT_WILL_ENTER_BACKGROUND ? "will enter background" : "did enter background");
        break;
    case SDL_EVENT_WILL_ENTER_FOREGROUND:
        Log.info("will enter foreground");
        break;
    case SDL_EVENT_DID_ENTER_FOREGROUND:
        s_background = false;
        if (!s_holding) {
            // the loop never held (the pause was all inside one event pump): frames run again at once
            pbnet_keeper_hidden(0);
        }
        Log.info("did enter foreground");
        break;
    case SDL_EVENT_TERMINATING:
        // iOS/Android end the app: nothing runs after this returns
        Log.info("terminating");
        s_saveDue = false;
        partyboard::config::Save();
        break;
    case SDL_EVENT_LOW_MEMORY:
        Log.warn("low memory");
        break;
    default:
        break;
    }
    return true;
}

// Why the game holds now, or nullptr.
const char* hold_reason()
{
    if (s_background) {
        return "the app is in the background";
    }
    auto* window = static_cast<SDL_Window*>(auroraInfo.window);
    const SDL_WindowFlags flags = window != nullptr ? SDL_GetWindowFlags(window) : 0;
    if ((flags & SDL_WINDOW_MINIMIZED) != 0) {
        return "the window is minimized";
    }
    if ((flags & SDL_WINDOW_HIDDEN) != 0) {
        return "the window is hidden";
    }
    if (auroraInfo.backend != BACKEND_NULL && !aurora_can_present()) {
        return "the window can't show frames";
    }
    return nullptr;
}
} // namespace

extern "C" void port_lifecycle_init(void)
{
    parse_test_steps();
    if (!SDL_AddEventWatch(watch, nullptr)) {
        Log.error("can't watch the app's lifecycle: {}", SDL_GetError());
    }
}

extern "C" bool port_lifecycle_hold(void)
{
    run_test_steps();
    if (s_saveDue.exchange(false)) {
        // the app may be killed in the background: the settings as they are now. A party's state is in its autosave
        // (N47), written at every turn's start.
        partyboard::config::Save();
    }
    const char* why = hold_reason();
    if (why != nullptr) {
        if (!s_holding) {
            s_holding = true;
            s_heldSince = SDL_GetTicks();
            s_heldLoops = 0;
            Log.info("holding the game: {}", why);
            pbnet_keeper_hidden(1);
        }
        ++s_heldLoops;
        pbnet_keeper_frame();
        return true;
    }
    if (s_holding) {
        s_holding = false;
        Log.info("resuming the game after {} held loops ({} ms)", s_heldLoops, SDL_GetTicks() - s_heldSince);
        pbnet_keeper_hidden(0);
    }
    return false;
}
