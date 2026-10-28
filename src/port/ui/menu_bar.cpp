// Credits: TwilitRealm

#include "menu_bar.hpp"

#include <RmlUi/Core.h>

#include "achievements.hpp"
#include "aurora/rmlui.hpp"
#include "port/netplay.h"
#include "port/pbnet.h"
#include "port/settings.h"
#include "imgui.h"
#include "modal.hpp"
#include "online.hpp"
#include "prelaunch.hpp"
#include "settings.hpp"
#include "ui.hpp"
#include "window.hpp"

#include <chrono>
#include <cmath>
#include <cstdlib>
#include <string>
#include <vector>
#if defined(__ANDROID__)
#include <sys/system_properties.h>
#endif

extern "C" {
#include "game/object.h"
#include "game/objsub.h"
}

namespace partyboard::ui {
namespace {

    const Rml::String kDocumentSource = R"RML(
<rml>
<head>
    <link type="text/rcss" href="res/rml/tabbing.rcss" />
    <link type="text/rcss" href="res/rml/popup.rcss" />
</head>
<body>
    <popup id="popup" />
</body>
</rml>
)RML";

}

void quit_to_launcher() noexcept
{
    // N133: back to the prelaunch menu, the game booting again under it as at launch. Online, the lobby is left
    // first (as the hold modal's Leave Match).
    if (pbnet_lobby_state() != PBNET_LOBBY_NONE) {
        pbnet_lobby_leave();
    }
    netplay_match_quit_to_launcher();
    push_document(std::make_unique<Prelaunch>(true));
}

namespace {

    // N133 test: PB_RELAUNCH_TEST="quit@89+1500,play@+200,quit@mg+300,play@+200": each step in turn, once the
    // overlay (its number, "mg" for any minigame, none for any overlay) has run that many UI updates in a row.
    struct RelaunchStep {
        std::string action;
        int ovl; // -1: any, -2: a minigame
        int updates;
    };

    std::vector<RelaunchStep> sRelaunchSteps;
    size_t sRelaunchNext = 0;
    int sRelaunchCount = 0;
    bool sRelaunchParsed = false;

    void parse_relaunch_test()
    {
        sRelaunchParsed = true;
        const char *spec = std::getenv("PB_RELAUNCH_TEST");
#if defined(__ANDROID__)
        // An app gets no env vars: `adb shell setprop debug.partyboard.relaunch <steps>` (setprop ... "" to stop).
        char prop[PROP_VALUE_MAX] = {};
        if (spec == nullptr && __system_property_get("debug.partyboard.relaunch", prop) > 0) {
            spec = prop;
        }
#endif
        if (spec == nullptr) {
            return;
        }
        std::string rest = spec;
        while (!rest.empty()) {
            const auto comma = rest.find(',');
            const std::string item = rest.substr(0, comma);
            rest = comma == std::string::npos ? std::string() : rest.substr(comma + 1);
            const auto at = item.find('@');
            const auto plus = item.find('+');
            if (at == std::string::npos || plus == std::string::npos || plus < at) {
                continue;
            }
            const std::string ovl = item.substr(at + 1, plus - at - 1);
            sRelaunchSteps.push_back({
                item.substr(0, at),
                ovl.empty() ? -1 : ovl == "mg" ? -2 : std::atoi(ovl.c_str()),
                std::atoi(item.c_str() + plus + 1),
            });
        }
        OSReport("relaunch test: %d steps\n", static_cast<int>(sRelaunchSteps.size()));
    }

} // namespace

void relaunch_test_poll() noexcept
{
    if (!sRelaunchParsed) {
        parse_relaunch_test();
    }
    if (sRelaunchNext >= sRelaunchSteps.size()) {
        return;
    }
    const auto &step = sRelaunchSteps[sRelaunchNext];
    const bool here = step.ovl == -1 || (step.ovl == -2 ? omcurovl >= 0 && omMgIndexGet(omcurovl) >= 0 : omcurovl == step.ovl);
    sRelaunchCount = here ? sRelaunchCount + 1 : 0;
    if (sRelaunchCount < step.updates) {
        return;
    }
    OSReport("relaunch test: step %d %s in overlay %d\n", static_cast<int>(sRelaunchNext), step.action.c_str(),
        static_cast<int>(omcurovl));
    if (step.action == "quit") {
        quit_to_launcher();
    } else if (step.action == "play") {
        auto *prelaunch = dynamic_cast<Prelaunch *>(top_document());
        if (prelaunch == nullptr || !prelaunch->press_play()) {
            OSReport("relaunch test: no prelaunch menu to play from\n");
        }
    }
    sRelaunchNext++;
    sRelaunchCount = 0;
}

MenuBar::MenuBar()
    : Document(kDocumentSource)
    , mRoot(mDocument->GetElementById("popup"))
{
    mTabBar = std::make_unique<TabBar>(mRoot,
        TabBar::Props {
            .onClose =
                [this] {
                    // mDoAud_seStartMenu(kSoundMenuClose); // TODO
                    hide(false);
                },
            .autoSelect = false,
        });
    mTabBar->add_tab("Settings", [this] { push(std::make_unique<SettingsWindow>()); });
    mTabBar->add_tab("Online", [this] { push(std::make_unique<OnlineWindow>()); });
    // mTabBar->add_tab("Warp", [] {
    //     // TODO
    // });

    mTabBar->add_tab("Achievements", [this] { push(std::make_unique<AchievementsWindow>()); });
    // TODO PC
    // mTabBar->add_tab("Reset", [this] {
    //     mTabBar->set_active_tab(-1);
    //     const auto dismiss = [](Modal &modal) { modal.pop(); };
    //     push(std::make_unique<Modal>(Modal::Props{
    //         .title = "Reset Game",
    //         .bodyRml = "Unsaved progress will be lost.<br/>"
    //                    "<span class=\"tip\">Tip: You can also reset by holding Start+X+B</span>",
    //         .actions =
    //             {
    //                 ModalAction{
    //                     .label = "Cancel",
    //                     .onPressed =
    //                         [this, dismiss](Modal& modal) {
    //                             // mDoAud_seStartMenu(kSoundWindowClose); // TODO PC
    //                             dismiss(modal);
    //                         },
    //                 },
    //                 // ModalAction{
    //                 //     .label = "Reset",
    //                 //     .onPressed =
    //                 //         [this, dismiss](Modal& modal) {
    //                 //             // mDoAud_seStartMenu(kSoundClick); // TODO PC
    //                 //             // if (fpcM_SearchByName(fpcNm_LOGO_SCENE_e)) {
    //                 //             //     dismiss(modal);
    //                 //             //     return;
    //                 //             // }
    //                 //             dismiss(modal);
    //                 //             hide(false);
    //                 //         },
    //                 // },
    //             },
    //         .onDismiss = dismiss,
    //         .icon = "question-mark",
    //     }));
    // });
    mTabBar->add_tab("Quit", [this] {
        mTabBar->set_active_tab(-1);
        const auto dismiss = [](Modal &modal) { modal.pop(); };
        push(std::make_unique<Modal>(Modal::Props{
            .title = "Quit to Main Menu",
            .bodyRml = "Unsaved progress will be lost.",
            .actions =
                {
                    ModalAction{
                        .label = "Cancel",
                        .onPressed =
                            [dismiss](Modal& modal) {
                                // mDoAud_seStartMenu(kSoundWindowClose); // TODO PC
                                dismiss(modal);
                            },
                    },
                    ModalAction{
                        .label = "Quit",
                        .onPressed =
                            [this, dismiss](Modal& modal) {
                                // mDoAud_seStartMenu(kSoundClick); // TODO PC
                                dismiss(modal);
#ifdef __EMSCRIPTEN__
                                // The browser has no prelaunch menu (N87): back to the game's own menu.
                                if (netplay_active()) {
                                    pbnet_lobby_leave();  // as the hold modal's Leave Match
                                } else {
                                    netplay_match_quit_to_menu();
                                }
#else
                                hide(false);
                                quit_to_launcher();
#endif
                            },
                    },
                },
            .onDismiss = dismiss,
            .icon = "question-mark",
        }));
    });

    // Hide document after transition completion
    listen(mRoot, Rml::EventId::Transitionend, [this](Rml::Event &event) {
        if (event.GetTargetElement() == mRoot && !mRoot->HasAttribute("open") && Document::visible()) {
            Document::hide(mPendingClose);
        }
    });
}

void MenuBar::show()
{
    Document::show();
    mRoot->SetAttribute("open", "");
    mTabBar->set_active_tab(-1);
    if (!mTabBar->focus_tab(mFocusedTabIndex)) {
        mTabBar->focus();
    }
}

void MenuBar::hide(bool close)
{
    mFocusedTabIndex = mTabBar->focused_tab_index();
    mRoot->RemoveAttribute("open");
    if (close) {
        mPendingClose = true;
    }
}

void MenuBar::update()
{
    update_safe_area();
    Document::update();
}

void MenuBar::update_safe_area() noexcept
{
    if (mDocument == nullptr || mTabBar == nullptr) {
        return;
    }

    // Avoid ImGui menu bar if shown
    if (const auto *viewport = ImGui::GetMainViewport(); viewport != nullptr && mTopMargin != viewport->WorkPos.y) {
        mTopMargin = viewport->WorkPos.y;
        mRoot->SetProperty(Rml::PropertyId::MarginTop, Rml::Property(mTopMargin, Rml::Unit::DP));
    }

    Rml::Context *context = mDocument->GetContext();
    Insets safeInsets = safe_area_insets(context);
    safeInsets = {
        0.0f,
        std::round(safeInsets.right),
        0.0f,
        std::round(safeInsets.left),
    };
    if (safeInsets == mTabBarPadding) {
        return;
    }

    mTabBarPadding = safeInsets;
    auto *tabBar = mTabBar->root();
    tabBar->SetProperty(Rml::PropertyId::PaddingRight, Rml::Property(safeInsets.right, Rml::Unit::PX));
    tabBar->SetProperty(Rml::PropertyId::PaddingLeft, Rml::Property(safeInsets.left, Rml::Unit::PX));
    if (auto *close = tabBar->QuerySelector("close")) {
        close->SetProperty(
            Rml::PropertyId::Right, Rml::Property(safeInsets.right + 8.0f * context->GetDensityIndependentPixelRatio(), Rml::Unit::PX));
    }
}

bool MenuBar::visible() const
{
    return mRoot->HasAttribute("open");
}

bool MenuBar::handle_nav_command(Rml::Event &event, NavCommand cmd)
{
    if (!getSettings().backend.wasPresetChosen) {
        return true;
    }
    if (cmd == NavCommand::Cancel && visible()) {
        // mDoAud_seStartMenu(kSoundMenuClose); // TODO PC
        hide(false);
        return true;
    }
    return Document::handle_nav_command(event, cmd);
}

bool MenuBar::focus()
{
    return mTabBar->focus();
}

} // namespace partyboard::ui
