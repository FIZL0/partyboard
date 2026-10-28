#pragma once

#include "online_model.hpp"
#include "window.hpp"

#include <cstdint>
#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace partyboard::ui {

class Component;
class Pane;

// Online menu: Host / Join by code / LAN games / Continue saved game, and this device's players.
class OnlineWindow : public Window {
public:
    OnlineWindow();
    ~OnlineWindow() override;

    void update() override;
    // N98: joins the lobby at `code` (a link's ?code= in the browser), as Join Game with it entered.
    void join_code(const std::string &code);

private:
    void save();
    void host();
    // N96, desktop: hosts a WebRTC lobby browsers can join (net/src/link.rs).
    void host_browsers();
    void join();
    void rejoin();
    void open_lobby();
    // N29: Continue tab.
    void pick_save(const online::SaveInfo &save);
    void confirm_delete_save(const std::string &path, const Rml::String &title);
    void host_continue(const std::string &path);
    void poll_lan();
    void rebuild_lan();
    void join_lan(const std::string &code);
    std::string lan_status() const;

    online::Profile mProfile;

    // N28: the LAN tab browses (pbnet_lan_browse) while it is open.
    bool mLanOpen = false;
    bool mBrowsing = false;
    int mLanTick = 0;
    online::LanList mLan;
    std::string mLanKey;
    Pane *mLanLeft = nullptr;
    std::vector<std::pair<std::string, Component *>> mLanButtons;
};

// The lobby (pbnet_lobby_*): seats, this device's characters, the rules (host only), ready and start.
class LobbyWindow : public Window {
public:
    LobbyWindow();

    void update() override;

protected:
    bool consume_close_request() override;

private:
    void refresh();
    // N92: the host takes a browser guest's answer from the clipboard (N96: a desktop's WebRTC lobby too).
    void paste_answer();
    void show_right(Pane &pane, std::function<void(Pane &)> builder);
    void confirm_leave();
    void leave_to_menu();
    void send_picks(int mine, int character, int team);
    void send_settings(int board, int turns, bool bonus, bool teams, const uint8_t *handicaps);

    online::View mView;
    uint32_t mGen = UINT32_MAX;
    bool mFinished = false;
    Pane *mRightPane = nullptr;
    std::function<void(Pane &)> mRightBuilder;
};

// N10: during an online match, "Waiting for <name>" (the host may let the CPU play the seat, or save and quit) and,
// on a guest, "Host left". Call once per loop from ui_update.
void update_online_match();

// An online match is over and its lobby open again (netplay_lobby_returned): shows the lobby.
void open_lobby_after_match();

} // namespace partyboard::ui
