#pragma once

#include "online_model.hpp"
#include "window.hpp"

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace partyboard::ui {

class Pane;

// Online menu: Host / Join by code / LAN games / Continue saved game, and this device's players.
class OnlineWindow : public Window {
public:
    OnlineWindow();

private:
    void save();
    void host();
    void join();
    void open_lobby();

    online::Profile mProfile;
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

// An online match is over and its lobby open again (netplay_lobby_returned): shows the lobby.
void open_lobby_after_match();

} // namespace partyboard::ui
