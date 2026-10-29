// Credits: TwilitRealm

#include "ui.hpp"

#include <RmlUi/Core.h>
#ifdef __APPLE__
#include <TargetConditionals.h>
#endif
#include <SDL3/SDL_filesystem.h>
#include <absl/container/flat_hash_set.h>
#include <aurora/rmlui.hpp>
#include <fmt/format.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <cstdio>
#include <filesystem>
#include <optional>
#include <ranges>

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif
#ifdef __ANDROID__
#include <SDL3/SDL_system.h>
#include <jni.h>
#endif

#include "RmlUi/Debugger/Debugger.h"
#include "aurora/lib/logging.hpp"
#include "aurora/lib/window.hpp"
#include "input.hpp"
#include "online.hpp"
#include "prelaunch.hpp"
#include "touch_pad.hpp"
#include "window.hpp"

#include <port/settings.h>

namespace partyboard::ui {
namespace {

    void load_font(const char *filename, bool fallback = false)
    {
        Rml::LoadFontFace(resource_path(filename).string(), fallback);
    }

    bool sInitialized = false;
    std::vector<std::unique_ptr<Document>> sDocumentStack;
    // Documents that don't participate in the focus stack
    std::vector<std::unique_ptr<Document>> sPassiveDocuments;
    std::deque<Toast> sToasts;
    bool sMenuNotificationRequested = false;

    // Sometimes gamepads can connect and disconnect quickly, especially during
    // connection negotiation. In this case, we'll receive an _ADDED event for a
    // disconnected gamepad. Storing IDs here lets use only show disconnected
    // notifications for gamepads that we sent a connected notification for.
    absl::flat_hash_set<SDL_JoystickID> sConnectedGamepads;

    aurora::Module Log { "partyboard::ui" };

    // N130: the device's safe-area insets (left, top, right, bottom) as shares of the window, read every 30 frames
    std::array<float, 4> sDeviceSafe {};
    // The insets in force: the device's under the Safe Area setting (what the game picture is fitted inside)
    std::array<float, 4> sSafe {};
    std::array<float, 4> sPresent {};
    unsigned sSafeFrame = 0;

#ifdef __EMSCRIPTEN__
    // The page's env(safe-area-inset-*) (left, top, right, bottom, CSS px) through a hidden probe element; the canvas
    // covers the whole page (viewport-fit=cover)
    EM_JS(void, pb_web_safe_area, (float *out), {
        let probe = document.getElementById('pb-safe-probe');
        if (!probe) {
            probe = document.createElement('div');
            probe.id = 'pb-safe-probe';
            probe.style.cssText = 'position:fixed;left:0;top:0;width:0;height:0;visibility:hidden;pointer-events:none;'
                + 'padding:env(safe-area-inset-top,0px) env(safe-area-inset-right,0px) env(safe-area-inset-bottom,0px) '
                + 'env(safe-area-inset-left,0px)';
            document.body.appendChild(probe);
        }
        const cs = getComputedStyle(probe);
        const w = Math.max(1, window.innerWidth), h = Math.max(1, window.innerHeight);
        HEAPF32[(out >> 2) + 0] = (parseFloat(cs.paddingLeft) || 0) / w;
        HEAPF32[(out >> 2) + 1] = (parseFloat(cs.paddingTop) || 0) / h;
        HEAPF32[(out >> 2) + 2] = (parseFloat(cs.paddingRight) || 0) / w;
        HEAPF32[(out >> 2) + 3] = (parseFloat(cs.paddingBottom) || 0) / h;
    });
#endif

#ifdef __ANDROID__
    // PartyBoardActivity.safeInsets(): the visible system bars and the cutout in physical px (SDL's own insets take in
    // the system gesture zones, which aren't a display safe area); false when it can't be called
    bool android_safe_px(std::array<float, 4> &out) noexcept
    {
        static jclass sClass = nullptr;
        static jmethodID sMethod = nullptr;
        static bool sFailed = false;
        auto *env = static_cast<JNIEnv *>(SDL_GetAndroidJNIEnv());
        if (sFailed || env == nullptr) {
            return false;
        }
        if (sMethod == nullptr) {
            auto activity = static_cast<jobject>(SDL_GetAndroidActivity());
            if (activity == nullptr) {
                env->ExceptionClear();
                return false;
            }
            jclass local = env->GetObjectClass(activity);
            env->DeleteLocalRef(activity);
            jmethodID method = local != nullptr ? env->GetStaticMethodID(local, "safeInsets", "()[I") : nullptr;
            if (method == nullptr || env->ExceptionCheck()) {
                env->ExceptionClear();
                if (local != nullptr) {
                    env->DeleteLocalRef(local);
                }
                sFailed = true;
                return false;
            }
            sClass = static_cast<jclass>(env->NewGlobalRef(local));
            env->DeleteLocalRef(local);
            sMethod = method;
        }
        auto array = static_cast<jintArray>(env->CallStaticObjectMethod(sClass, sMethod));
        if (env->ExceptionCheck()) {
            env->ExceptionClear();
            return false;
        }
        if (array == nullptr) {
            return false;
        }
        jint px[4] {};
        if (env->GetArrayLength(array) == 4) {
            env->GetIntArrayRegion(array, 0, 4, px);
        }
        env->DeleteLocalRef(array);
        out = { static_cast<float>(px[0]), static_cast<float>(px[1]), static_cast<float>(px[2]), static_cast<float>(px[3]) };
        return true;
    }
#endif

    // PB_SAFE=l,t,r,b: fixed insets in window (logical) px instead of the device's, for shots on desktop
    const std::optional<std::array<float, 4>> &fake_safe() noexcept
    {
        static const std::optional<std::array<float, 4>> fake = [] () -> std::optional<std::array<float, 4>> {
            const char *env = std::getenv("PB_SAFE");
            std::array<float, 4> v {};
            if (env == nullptr || std::sscanf(env, "%f,%f,%f,%f", &v[0], &v[1], &v[2], &v[3]) != 4) {
                return std::nullopt;
            }
            return v;
        }();
        return fake;
    }

    std::array<float, 4> read_device_safe(SDL_Window *window) noexcept
    {
        int w = 0;
        int h = 0;
        if (window == nullptr || !SDL_GetWindowSize(window, &w, &h) || w <= 0 || h <= 0) {
            return {};
        }
        if (const auto &fake = fake_safe()) {
            const auto &px = *fake;
            return { px[0] / w, px[1] / h, px[2] / w, px[3] / h };
        }
#ifdef __ANDROID__
        int pw = 0;
        int ph = 0;
        std::array<float, 4> px {};
        if (android_safe_px(px) && SDL_GetWindowSizeInPixels(window, &pw, &ph) && pw > 0 && ph > 0) {
            return { px[0] / pw, px[1] / ph, px[2] / pw, px[3] / ph };
        }
#endif
#ifdef __EMSCRIPTEN__
        std::array<float, 4> web {};
        pb_web_safe_area(web.data());
        return web;
#else
        SDL_Rect r {};
        if (!SDL_GetWindowSafeArea(window, &r)) {
            return {};
        }
        std::array<float, 4> safe {
            std::max(0, r.x) / static_cast<float>(w),
            std::max(0, r.y) / static_cast<float>(h),
            std::max(0, w - (r.x + r.w)) / static_cast<float>(w),
            std::max(0, h - (r.y + r.h)) / static_cast<float>(h),
        };
#if defined(__APPLE__) && TARGET_OS_IOS
        // iPhone: only the camera cutout (Dynamic Island / notch) keeps the HUD out; the picture goes under the home
        // indicator and the status bar (user 2026-10-29). Landscape: the cutout is on a side; portrait: at the top.
        safe[3] = 0.0f;
        if (w > h) {
            safe[1] = 0.0f;
        }
#endif
        return safe;
#endif
    }

} // namespace

std::array<float, 4> safe_area_shares(std::array<float, 4> device, int setting) noexcept
{
    if (setting < 0) {
        return {};
    }
    const float margin = static_cast<float>(std::min(setting, 10)) * 0.01f;
    for (float &v : device) {
        v = std::clamp(std::max(v, margin), 0.0f, 0.45f);
    }
    return device;
}

void update_safe_area() noexcept
{
    auto *window = aurora::window::get_sdl_window();
    if (window == nullptr) {
        return;
    }
    if (sSafeFrame++ % 30 == 0) {
        const auto device = read_device_safe(window);
        if (device != sDeviceSafe) {
            sDeviceSafe = device;
            Log.info("safe area {} {} {} {} (window shares)", device[0], device[1], device[2], device[3]);
        }
    }
    const auto safe = safe_area_shares(sDeviceSafe, getSettings().video.safeArea.getValue());
    // The home screen's backdrop goes edge to edge (no black bars on a phone): the picture fills the window and the
    // home screen pads its own layout by safe_area_insets (Prelaunch::apply_safe_area).
    const bool home = std::any_of(sDocumentStack.begin(), sDocumentStack.end(),
        [](const auto &doc) { return dynamic_cast<const Prelaunch *>(doc.get()) != nullptr && doc->visible(); });
    const auto present = home ? std::array<float, 4> {} : safe;
    if (safe != sSafe || present != sPresent) {
        sSafe = safe;
        sPresent = present;
        aurora::window::set_present_insets(present[0], present[1], present[2], present[3]);
    }
}

bool initialize() noexcept
{
    if (sInitialized) {
        return true;
    }
    if (!aurora::rmlui::is_initialized()) {
        return false;
    }

    load_font("AlegreyaSC-Regular.ttf");
    load_font("AlegreyaSC-Bold.ttf");
    load_font("MaterialSymbolsRounded-Regular.ttf");
    load_font("N64Party-Monochromatic.otf");
    load_font("FOT-NewRodin Pro DB.otf");

    // Rml::Debugger::Initialise(aurora::rmlui::get_context());
    // Rml::Debugger::SetVisible(true);

    sInitialized = true;
    return true;
}

void shutdown() noexcept
{
    sDocumentStack.clear();
    sPassiveDocuments.clear();
    sConnectedGamepads.clear();
    input::reset_input_state();
    input::release_input_block();
    sInitialized = false;
}

const char *battery_icon(SDL_PowerState state, int level) noexcept
{
    if (state == SDL_POWERSTATE_UNKNOWN || state == SDL_POWERSTATE_NO_BATTERY) {
        return "e1a6"; // Battery Unknown
    }
    if (state == SDL_POWERSTATE_ERROR) {
        return "f7ea"; // Battery Error
    }
    if (state == SDL_POWERSTATE_CHARGED || level == 100) {
        return "e1a4"; // Battery Full
    }
    if (state == SDL_POWERSTATE_CHARGING) {
        if (level >= 90)
            return "f0a7"; // Battery Charging 90
        if (level >= 80)
            return "f0a6"; // Battery Charging 80
        if (level >= 60)
            return "f0a5"; // Battery Charging 60
        if (level >= 50)
            return "f0a4"; // Battery Charging 50
        if (level >= 30)
            return "f0a3"; // Battery Charging 30
        if (level >= 20)
            return "f0a2"; // Battery Charging 20
        return "e1a3"; // Battery Charging Full (we use it as empty)
    }
    if (level >= 90)
        return "ebd2"; // Battery 6 Bar
    if (level >= 80)
        return "ebd4"; // Battery 5 Bar
    if (level >= 60)
        return "ebe2"; // Battery 4 Bar
    if (level >= 50)
        return "ebdd"; // Battery 3 Bar
    if (level >= 30)
        return "ebe0"; // Battery 2 Bar
    if (level >= 20)
        return "ebd9"; // Battery 1 Bar
    return "e19c"; // Battery Alert
}

const char *connection_state_icon(SDL_JoystickConnectionState state) noexcept
{
    switch (state) {
        case SDL_JOYSTICK_CONNECTION_WIRELESS:
            return "e1a7";
        case SDL_JOYSTICK_CONNECTION_WIRED:
            return "e1e0";
        default:
            return nullptr;
    }
}

void handle_event(const SDL_Event &event) noexcept
{
    if (!aurora::rmlui::is_initialized()) {
        return;
    }

    if (event.type == SDL_EVENT_GAMEPAD_ADDED) {
        auto *gamepad = SDL_GetGamepadFromID(event.gdevice.which);
        if (SDL_GamepadConnected(gamepad)) {
            if (getSettings().game.enableControllerToasts) {
                const char *name = SDL_GetGamepadName(gamepad);
                Rml::String content = fmt::format("<span>{}</span>", name ? name : "[Unknown]");
                Rml::String title = "Controller connected";
                if (const char *icon = connection_state_icon(SDL_GetGamepadConnectionState(gamepad))) {
                    title = fmt::format("<row><span>{}</span> <icon class=\"connection\">&#x{};</icon></row>", title, icon);
                }
                int batteryLevel = -1;
                const auto powerState = SDL_GetGamepadPowerInfo(gamepad, &batteryLevel);
                if (powerState != SDL_POWERSTATE_UNKNOWN) {
                    content = fmt::format(
                        "<row>{}</row><row class=\"muted\"><icon class=\"battery\">&#x{};</icon>", content, battery_icon(powerState, batteryLevel));
                    if (batteryLevel > -1) {
                        content = fmt::format("{}&nbsp;<span>{}%</span>", content, batteryLevel);
                    }
                    content += "</row>";
                }
                push_toast({
                    .type = "controller",
                    .title = title,
                    .content = content,
                    .duration = std::chrono::seconds(4),
                });
            }
            sConnectedGamepads.insert(event.gdevice.which);
        }
    }
    else if (event.type == SDL_EVENT_GAMEPAD_REMOVED && sConnectedGamepads.contains(event.gdevice.which)) {
        if (getSettings().game.enableControllerToasts) {
            const char *name = SDL_GetGamepadNameForID(event.gdevice.which);
            push_toast({
                .type = "controller",
                .title = "Controller disconnected",
                .content = name ? name : "[Unknown]",
                .duration = std::chrono::seconds(4),
            });
        }
        sConnectedGamepads.erase(event.gdevice.which);
    }
    touch_pad::handle_event(event);
    input::handle_event(event);
}

Document &push_document(std::unique_ptr<Document> doc, bool show, bool passive) noexcept
{
    Document &ret = *doc;
    if (passive) {
        sPassiveDocuments.push_back(std::move(doc));
    }
    else {
        sDocumentStack.push_back(std::move(doc));
    }
    if (show) {
        ret.show();
    }
    input::sync_input_block();
    return ret;
}

void show_top_document() noexcept
{
    if (auto *doc = top_document()) {
        doc->show();
    }
    input::sync_input_block();
}

bool any_document_visible() noexcept
{
    return std::any_of(sDocumentStack.begin(), sDocumentStack.end(), [](const auto &doc) { return doc && doc->visible(); });
}

bool is_prelaunch_open() noexcept
{
    return std::any_of(sDocumentStack.begin(), sDocumentStack.end(), [](const auto &doc) {
        const auto *prelaunch = dynamic_cast<const Prelaunch *>(doc.get());
        return prelaunch != nullptr && !prelaunch->pending_close() && !prelaunch->closed();
    });
}

// N142: bootDll's lobby backdrop (QuickOnlineHeld) stays while one of these is open.
bool is_online_open() noexcept
{
    return std::any_of(sDocumentStack.begin(), sDocumentStack.end(), [](const auto &doc) {
        const bool online = dynamic_cast<const OnlineWindow *>(doc.get()) != nullptr ||
                            dynamic_cast<const LobbyWindow *>(doc.get()) != nullptr;
        return online && !doc->pending_close() && !doc->closed();
    });
}

Document *top_document() noexcept
{
    for (auto &doc : std::views::reverse(sDocumentStack)) {
        if (!doc->closed() && !doc->pending_close()) {
            return doc.get();
        }
    }
    return nullptr;
}

void update() noexcept
{
    update_safe_area();
    if (!aurora::rmlui::is_initialized()) {
        return;
    }

    input::update_input();
    const auto update_documents = [](auto &documents) {
        const std::size_t count = documents.size();
        for (std::size_t i = 0; i < count && i < documents.size(); ++i) {
            Document *doc = documents[i].get();
            if (doc != nullptr && !doc->closed()) {
                doc->update();
            }
        }
    };
    update_documents(sDocumentStack);
    update_documents(sPassiveDocuments);

    // Remove closed documents
    {
        const auto [first, last] = std::ranges::remove_if(sDocumentStack, [](const auto &doc) { return doc->closed(); });
        sDocumentStack.erase(first, last);
    }
    {
        const auto [first, last] = std::ranges::remove_if(sPassiveDocuments, [](const auto &doc) { return doc->closed(); });
        sPassiveDocuments.erase(first, last);
    }

    // If no documents have focus, explicitly focus the top one
    if (auto *context = aurora::rmlui::get_context();
        context != nullptr && (context->GetFocusElement() == nullptr || context->GetFocusElement() == context->GetRootElement())) {
        for (auto &doc : std::views::reverse(sDocumentStack)) {
            if (!doc->closed() && !doc->pending_close() && doc->focus()) {
                break;
            }
        }
    }

    input::sync_input_block();
}

std::filesystem::path resource_path(const std::filesystem::path &filename) noexcept
{
    return std::filesystem::path("res") / filename;
}

std::string escape(std::string_view str) noexcept
{
    std::string result;
    result.reserve(str.size());
    for (const char c : str) {
        switch (c) {
            case '&':
                result += "&amp;";
                break;
            case '<':
                result += "&lt;";
                break;
            case '>':
                result += "&gt;";
                break;
            case '"':
                result += "&quot;";
                break;
            default:
                result += c;
                break;
        }
    }
    return result;
}

Rml::Element *append(Rml::Element *parent, const Rml::String &tag) noexcept
{
    if (parent == nullptr) {
        return nullptr;
    }
    auto *doc = parent->GetOwnerDocument();
    if (doc == nullptr) {
        return nullptr;
    }
    return parent->AppendChild(doc->CreateElement(tag));
}

NavCommand map_nav_event(const Rml::Event &event) noexcept
{
    const auto key = static_cast<Rml::Input::KeyIdentifier>(event.GetParameter<int>("key_identifier", Rml::Input::KI_UNKNOWN));
    switch (key) {
        case Rml::Input::KeyIdentifier::KI_UP:
            return NavCommand::Up;
        case Rml::Input::KeyIdentifier::KI_DOWN:
            return NavCommand::Down;
        case Rml::Input::KeyIdentifier::KI_LEFT:
            return NavCommand::Left;
        case Rml::Input::KeyIdentifier::KI_RIGHT:
            return NavCommand::Right;
        case Rml::Input::KeyIdentifier::KI_ESCAPE:
            return NavCommand::Cancel;
        case Rml::Input::KeyIdentifier::KI_RETURN:
            return NavCommand::Confirm;
        case Rml::Input::KeyIdentifier::KI_F1:
            return event.GetParameter<int>("shift_key", 0) ? NavCommand::None : NavCommand::Menu;
        case Rml::Input::KeyIdentifier::KI_NEXT:
            return NavCommand::Next;
        case Rml::Input::KeyIdentifier::KI_PRIOR:
            return NavCommand::Previous;
        default:
            return NavCommand::None;
    }
}

Insets safe_area_insets(Rml::Context *context) noexcept
{
    if (context == nullptr) {
        return {};
    }

    const AuroraWindowSize windowSize = aurora::window::get_window_size();
    const Rml::Vector2i contextSize = context->GetDimensions();
    if (windowSize.native_fb_width == 0 || windowSize.native_fb_height == 0 || contextSize.x <= 0 || contextSize.y <= 0) {
        return {};
    }

    // N130: the context covers the game picture, which aurora fits (centred) inside the present insets' rect; the
    // insets here are what of the safe area (sSafe) still lies inside the picture, in context px
    const float nativeW = static_cast<float>(windowSize.native_fb_width);
    const float nativeH = static_cast<float>(windowSize.native_fb_height);
    const auto rect = aurora::window::present_rect(windowSize.native_fb_width, windowSize.native_fb_height);
    const float fit = std::min(static_cast<float>(rect.width) / static_cast<float>(contextSize.x),
        static_cast<float>(rect.height) / static_cast<float>(contextSize.y));
    const float viewW = static_cast<float>(contextSize.x) * fit;
    const float viewH = static_cast<float>(contextSize.y) * fit;
    const float viewL = static_cast<float>(rect.x) + (static_cast<float>(rect.width) - viewW) / 2.0f;
    const float viewT = static_cast<float>(rect.y) + (static_cast<float>(rect.height) - viewH) / 2.0f;
    // Under a context px is rounding (the picture's size is whole px)
    const auto px = [fit](float native) { return native / fit >= 1.0f ? native / fit : 0.0f; };
    return {
        .top = px(sSafe[1] * nativeH - viewT),
        .right = px((viewL + viewW) - (1.0f - sSafe[2]) * nativeW),
        .bottom = px((viewT + viewH) - (1.0f - sSafe[3]) * nativeH),
        .left = px(sSafe[0] * nativeW - viewL),
    };
}

Rml::Vector2f touch_to_context(float x, float y, Rml::Context &context) noexcept
{
    // SDL's finger x/y are shares of the whole window; the context is drawn into the picture aurora fits inside the
    // present insets (N130), so a touch maps through that rect as aurora's own touch input does
    const AuroraWindowSize windowSize = aurora::window::get_window_size();
    const Rml::Vector2i contextSize = context.GetDimensions();
    if (windowSize.native_fb_width == 0 || windowSize.native_fb_height == 0 || contextSize.x <= 0 || contextSize.y <= 0) {
        return { x * static_cast<float>(contextSize.x), y * static_cast<float>(contextSize.y) };
    }
    const auto rect = aurora::window::present_rect(windowSize.native_fb_width, windowSize.native_fb_height);
    const float fit = std::min(static_cast<float>(rect.width) / static_cast<float>(contextSize.x),
        static_cast<float>(rect.height) / static_cast<float>(contextSize.y));
    const float viewL = static_cast<float>(rect.x)
        + (static_cast<float>(rect.width) - static_cast<float>(contextSize.x) * fit) / 2.0f;
    const float viewT = static_cast<float>(rect.y)
        + (static_cast<float>(rect.height) - static_cast<float>(contextSize.y) * fit) / 2.0f;
    return { (x * static_cast<float>(windowSize.native_fb_width) - viewL) / fit,
        (y * static_cast<float>(windowSize.native_fb_height) - viewT) / fit };
}

void push_toast(Toast toast) noexcept
{
    sToasts.push_back(std::move(toast));
}

std::vector<std::unique_ptr<Document>> &get_document_stack() noexcept
{
    return sDocumentStack;
}

std::deque<Toast> &get_toasts() noexcept
{
    return sToasts;
}

void show_menu_notification() noexcept
{
    sMenuNotificationRequested = true;
}

bool consume_menu_notification_request() noexcept
{
    const bool requested = sMenuNotificationRequested;
    sMenuNotificationRequested = false;
    return requested;
}

} // namespace partyboard::ui
