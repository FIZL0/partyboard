// Credits: TwilitRealm

#pragma once

#include "button.hpp"
#include "document.hpp"
#include "../iso_validate.hpp"
#include "port/quick_continue.h"

#include <array>
#include <memory>
#include <string>
#include <vector>

namespace partyboard::ui {

class Prelaunch : public Document {
public:
    // relaunch (N133): opened over the running game by the menu bar's Quit; the game is set up already.
    explicit Prelaunch(bool relaunch = false);

    void show() override;
    void hide(bool close) override;
    void update() override;
    bool focus() override;
    bool visible() const override;
    // The Play button, once a disc is loaded (the N133 relaunch test); false without one.
    bool press_play();
    // N134: the first Continue button (the relaunch test); false without one.
    bool press_continue();

protected:
    bool handle_nav_command(Rml::Event &event, NavCommand cmd) override;

private:
    void launch_game();
    void launch_online();
    void continue_game(const QuickContinueEntry &entry);
    void apply_safe_area();

    bool mEntranceAnimationStarted = false;
    bool mArtTried = false;
    std::array<float, 4> mSafePadding { -1.0f, -1.0f, -1.0f, -1.0f };
    bool mOnlineAfterDisc = false;
    bool mRestartSuppressed = false;
    size_t mPlayButton = 0;
    bool mRelaunch = false;
    int mRelaunchCardFileType = 0;
    std::vector<std::unique_ptr<Button>> mMenuButtons;
    std::vector<QuickContinueEntry> mContinueEntries;
    Rml::Element *mRoot = nullptr;
    Rml::Element *mDiscStatus = nullptr;
    Rml::Element *mDiscDetail = nullptr;
    Rml::Element *mVersion = nullptr;
    Rml::Element *mUpdateStatus = nullptr;
    Rml::Element *mUpdateMessage = nullptr;
    Rml::Element *mUpdateDownload = nullptr;
    Rml::Element *mUpdateDownloadLabel = nullptr;
};

class PrelaunchOptions;

struct PrelaunchState {
    bool initialized = false;
    std::string configuredDiscPath;
    bool configuredDiscCanLaunch = false;
    iso::DiscInfo configuredDiscInfo {};
    iso::ValidationError configuredDiscValidation = iso::ValidationError::Unknown;
    std::string activeDiscPath;
    iso::DiscInfo activeDiscInfo {};
    GameLanguage initialLanguage = GameLanguage::English;
    std::string initialGraphicsBackend;
    int initialCardFileType = 0;
    std::string errorString;
    std::string pendingDiscPath;
    iso::DiscInfo pendingDiscInfo {};
    iso::ValidationError pendingDiscValidation = iso::ValidationError::Unknown;
};

PrelaunchState &prelaunch_state() noexcept;
void ensure_initialized() noexcept;
void refresh_configured_disc_state() noexcept;
void open_iso_picker() noexcept;
bool is_restart_pending() noexcept;
void try_push_verification_modal(Document &host);

} // namespace partyboard::ui
