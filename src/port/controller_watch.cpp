// N143: controllers connected (or reconnected) at any time (include/port/controller_watch.h).
//
// A controller reaches the game in three steps: SDL's joystick driver sees it (iOS: the MFi driver's
// GCControllerDidConnectNotification observer, SDL_mfijoystick.m) and sends SDL_EVENT_GAMEPAD_ADDED; aurora's
// window.cpp opens it (input.cpp add_controller, which logs "Added controller") and SDL's player index is its port;
// the game reads that port with PADRead. On the desktop all three work for a pad plugged in after launch (journal N143,
// SDL virtual pad); on an iPhone an Xbox pad connected after launch did nothing until the app was relaunched, and a
// relaunch only redoes the first step (SDL's MFi driver lists [GCController controllers] when it starts).
//
// So, about once a second: a gamepad SDL has but aurora never added gets its SDL_EVENT_GAMEPAD_ADDED again; on iOS,
// when GameController.framework has more controllers connected than SDL has gamepads (for two checks in a row), SDL's
// joystick and gamepad subsystems are restarted, as a relaunch would: aurora's controllers are removed first (through
// SDL_EVENT_GAMEPAD_REMOVED, so its handles are closed before SDL frees them), then SDL lists the controllers again and
// sends GAMEPAD_ADDED for each. Every change of the ports is logged (partyboard::controllers in partyboard.log).

#include <port/controller_watch.h>

#include <aurora/lib/logging.hpp>
#include <dolphin/pad.h>
#include <fmt/format.h>

#include <SDL3/SDL_events.h>
#include <SDL3/SDL_gamepad.h>
#include <SDL3/SDL_init.h>
#include <SDL3/SDL_joystick.h>
#include <SDL3/SDL_stdinc.h>
#include <SDL3/SDL_timer.h>

#ifdef __APPLE__
#include <TargetConditionals.h>
#endif

#include <atomic>
#include <string>
#include <vector>

namespace {
aurora::Module Log("partyboard::controllers");

constexpr uint32_t kCheckFrames = 60;
// A rescan waits this long at most for aurora to take the removals.
constexpr uint32_t kDrainFrames = 30;

uint32_t s_frames = 0;
// A controller was added or removed since the last port log (set by the event watch, any thread).
std::atomic_bool s_portsDirty{false};
std::string s_lastPorts;
// The frame the ports are logged at: two after the event, when aurora has taken it (an event pushed after this
// loop's aurora_update is only polled at the next).
uint32_t s_portsLogAt = 0;
std::atomic<const char *> s_rescanWhy{nullptr};
bool s_draining = false;
uint32_t s_drainedFrames = 0;

// Gamepads SDL has and aurora doesn't, with the checks they were missing at (re-sent at the second, once).
struct Missing {
    SDL_JoystickID id;
    int checks;
};
std::vector<Missing> s_missing;
// Joysticks SDL has without a gamepad mapping (aurora only opens gamepads), logged once each.
std::vector<SDL_JoystickID> s_plainLogged;

#if defined(__APPLE__) && TARGET_OS_IPHONE
int s_appleMissingChecks = 0;
int s_appleLastCount = -1;
// The GameController count a rescan was done for: no second one until it changes (SDL may skip a controller on
// purpose; restarting for it every second would drop the others' input each time).
int s_appleRescannedFor = -1;
#endif

std::string gamepad_name(SDL_Gamepad *gamepad)
{
    const char *name = gamepad != nullptr ? SDL_GetGamepadName(gamepad) : nullptr;
    return name != nullptr ? name : "unknown";
}

std::vector<SDL_JoystickID> aurora_gamepads()
{
    std::vector<SDL_JoystickID> ids;
    for (u32 i = 0; i < PADCount(); ++i) {
        if (SDL_Gamepad *gamepad = PADGetSDLGamepadForIndex(i)) {
            ids.push_back(SDL_GetGamepadID(gamepad));
        }
    }
    return ids;
}

bool contains(const std::vector<SDL_JoystickID> &ids, SDL_JoystickID id)
{
    for (SDL_JoystickID other : ids) {
        if (other == id) {
            return true;
        }
    }
    return false;
}

void log_ports()
{
    std::string ports;
    for (u32 port = 0; port < PAD_CHANMAX; ++port) {
        const char *name = PADGetName(port);
        ports += fmt::format("{}{}: {}", port == 0 ? "" : ", ", port + 1,
                             name != nullptr ? fmt::format("'{}'", name) : std::string("-"));
    }
    std::string unassigned;
    for (u32 i = 0; i < PADCount(); ++i) {
        SDL_Gamepad *gamepad = PADGetSDLGamepadForIndex(i);
        const int player = gamepad != nullptr ? SDL_GetGamepadPlayerIndex(gamepad) : -1;
        if (player < 0 || player >= PAD_CHANMAX) {
            unassigned += fmt::format("{}'{}'", unassigned.empty() ? "" : ", ", gamepad_name(gamepad));
        }
    }
    std::string line = fmt::format("{} controller(s); ports {}", PADCount(), ports);
    if (!unassigned.empty()) {
        line += fmt::format("; no port (Settings > Controllers assigns one): {}", unassigned);
    }
    if (line != s_lastPorts) {
        s_lastPorts = line;
        Log.info("{}", line);
    }
}

void push_gamepad_event(SDL_EventType type, SDL_JoystickID id)
{
    SDL_Event event{};
    event.type = type;
    event.gdevice.timestamp = SDL_GetTicksNS();
    event.gdevice.which = id;
    SDL_PushEvent(&event);
}

void restart_sdl_controllers()
{
    s_draining = false;
    SDL_QuitSubSystem(SDL_INIT_GAMEPAD | SDL_INIT_JOYSTICK);
    if (SDL_WasInit(SDL_INIT_JOYSTICK) != 0) {
        Log.warn("rescan: SDL's joystick subsystem is still up (initialized elsewhere too): no fresh controller list");
    }
    if (!SDL_InitSubSystem(SDL_INIT_GAMEPAD | SDL_INIT_JOYSTICK)) {
        Log.error("rescan: can't restart SDL's gamepad subsystem: {}", SDL_GetError());
        return;
    }
    int count = 0;
    SDL_free(SDL_GetGamepads(&count));
    Log.info("rescan: SDL lists {} gamepad(s)", count);
    s_missing.clear();
    s_plainLogged.clear();
    s_portsDirty = true;
}

void start_rescan(const char *why)
{
    Log.warn("re-scanning controllers: {}", why);
    const auto ids = aurora_gamepads();
    for (SDL_JoystickID id : ids) {
        push_gamepad_event(SDL_EVENT_GAMEPAD_REMOVED, id);
    }
    if (ids.empty()) {
        restart_sdl_controllers();
        return;
    }
    s_draining = true;
    s_drainedFrames = 0;
}

// SDL gamepads aurora doesn't have: their GAMEPAD_ADDED again. Missing at two checks in a row, so that one whose event
// is still on its way (another thread's push after this frame's poll) isn't sent twice.
void check_lost_gamepads(const std::vector<SDL_JoystickID> &sdl, const std::vector<SDL_JoystickID> &ours)
{
    std::vector<Missing> missing;
    for (SDL_JoystickID id : sdl) {
        if (contains(ours, id)) {
            continue;
        }
        int checks = 1;
        for (const auto &before : s_missing) {
            if (before.id == id) {
                checks = before.checks + 1;
            }
        }
        if (checks == 2) {
            const char *name = SDL_GetGamepadNameForID(id);
            Log.warn("gamepad {} '{}' is connected but was never added: adding it now", id, name != nullptr ? name : "unknown");
            push_gamepad_event(SDL_EVENT_GAMEPAD_ADDED, id);
        }
        missing.push_back({id, checks});
    }
    s_missing = std::move(missing);
}

void check_plain_joysticks()
{
    int count = 0;
    SDL_JoystickID *ids = SDL_GetJoysticks(&count);
    if (ids == nullptr) {
        return;
    }
    for (int i = 0; i < count; ++i) {
        const SDL_JoystickID id = ids[i];
        if (SDL_IsGamepad(id) || contains(s_plainLogged, id)) {
            continue;
        }
        s_plainLogged.push_back(id);
        const char *name = SDL_GetJoystickNameForID(id);
        Log.warn("joystick {} '{}' (vid {:04x}, pid {:04x}) has no gamepad mapping: not used", id,
                 name != nullptr ? name : "unknown", SDL_GetJoystickVendorForID(id), SDL_GetJoystickProductForID(id));
    }
    SDL_free(ids);
}

#if defined(__APPLE__) && TARGET_OS_IPHONE
void check_apple(size_t sdlCount)
{
    const int count = PartyBoard_iOS_ControllerCount();
    if (count != s_appleLastCount) {
        s_appleLastCount = count;
        s_appleRescannedFor = -1;
    }
    if (count <= static_cast<int>(sdlCount)) {
        s_appleMissingChecks = 0;
        return;
    }
    if (++s_appleMissingChecks < 2 || s_appleRescannedFor == count) {
        return;
    }
    s_appleRescannedFor = count;
    Log.warn("GameController has {} controller(s) connected, SDL {} gamepad(s)", count, sdlCount);
    start_rescan("SDL missed a controller GameController has");
}
#endif

bool SDLCALL watch(void *, SDL_Event *event)
{
    switch (event->type) {
    case SDL_EVENT_GAMEPAD_ADDED:
    case SDL_EVENT_GAMEPAD_REMOVED:
    case SDL_EVENT_JOYSTICK_ADDED:
    case SDL_EVENT_JOYSTICK_REMOVED:
        s_portsDirty = true;
        break;
    default:
        break;
    }
    return true;
}
} // namespace

extern "C" void port_controller_watch_init(void)
{
    if (!SDL_AddEventWatch(watch, nullptr)) {
        Log.error("can't watch controller events: {}", SDL_GetError());
    }
#if defined(__APPLE__) && TARGET_OS_IPHONE
    PartyBoard_iOS_ControllerWatchStart();
#endif
}

extern "C" void port_controller_watch_note(const char *line)
{
    Log.info("{}", line);
}

extern "C" void port_controller_watch_rescan(const char *why)
{
    s_rescanWhy = why;
}

extern "C" void port_controller_watch_frame(void)
{
    ++s_frames;
    // aurora starts SDL's gamepads at its first update
    if (SDL_WasInit(SDL_INIT_GAMEPAD) == 0) {
        return;
    }
    if (s_draining) {
        if (PADCount() == 0 || ++s_drainedFrames > kDrainFrames) {
            restart_sdl_controllers();
        }
        return;
    }
    if (const char *why = s_rescanWhy.exchange(nullptr)) {
        start_rescan(why);
        return;
    }
    if (s_portsDirty.exchange(false)) {
        s_portsLogAt = s_frames + 2;
    }
    if (s_portsLogAt != 0 && s_frames >= s_portsLogAt) {
        s_portsLogAt = 0;
        log_ports();
    }
    if (s_frames % kCheckFrames != 0) {
        return;
    }

    int count = 0;
    SDL_JoystickID *ids = SDL_GetGamepads(&count);
    std::vector<SDL_JoystickID> sdl;
    if (ids != nullptr) {
        sdl.assign(ids, ids + count);
        SDL_free(ids);
    }
    check_lost_gamepads(sdl, aurora_gamepads());
    check_plain_joysticks();
#if defined(__APPLE__) && TARGET_OS_IPHONE
    check_apple(sdl.size());
#endif
}
