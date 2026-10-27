#include "online.hpp"

#include "bool_button.hpp"
#include "modal.hpp"
#include "number_button.hpp"
#include "pane.hpp"
#include "partyboard_version.h"
#include "port/main.h"
#include "port/pbnet.h"
#include "port/port_version.h"
#include "string_button.hpp"
#include "ui.hpp"

#include <SDL3/SDL_clipboard.h>
#include <fmt/chrono.h>
#include <fmt/format.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <ctime>
#include <memory>

namespace partyboard::ui {
namespace {

    using namespace online;

    std::filesystem::path profile_path()
    {
        return PartyBoard_ConfigPath / "online.json";
    }

    // Peers must run the same build and disc; pbnet refuses a guest whose differ.
    std::string build_id()
    {
        return fmt::format("{} {}", PARTY_BOARD_VERSION_STRING, PARTY_BOARD_WC_REVISION);
    }

    std::string disc_id()
    {
        const DVDDiskID &id = version::getDiskID();
        if (id.gameName[0] == '\0') {
            return "GMPE01-00";
        }
        return fmt::format("{}{}-{:02}", std::string_view(id.gameName, sizeof(id.gameName)),
            std::string_view(id.company, sizeof(id.company)), id.gameVersion);
    }

    std::string trim(std::string s)
    {
        const auto first = s.find_first_not_of(" \t\r\n");
        if (first == std::string::npos) {
            return {};
        }
        const auto last = s.find_last_not_of(" \t\r\n");
        return s.substr(first, last - first + 1);
    }

    std::string shorten(const std::string &s, size_t n)
    {
        return s.size() <= n ? s : s.substr(0, n) + "...";
    }

    void copy_text(const std::string &text, const char *what)
    {
        if (text.empty()) {
            return;
        }
        SDL_SetClipboardText(text.c_str());
        push_toast({ .title = fmt::format("{} copied", what), .content = shorten(text, 24), .duration = std::chrono::seconds(2) });
    }

    void show_message(Document &parent, Rml::String title, Rml::String body)
    {
        const auto dismiss = [](Modal &modal) { modal.pop(); };
        parent.push(std::make_unique<Modal>(Modal::Props {
            .title = std::move(title),
            .bodyRml = escape(body),
            .actions = { ModalAction { .label = "OK", .onPressed = dismiss } },
            .onDismiss = dismiss,
        }));
    }

    // Text that follows a getter (status lines). Not focusable.
    class LiveText : public FluentComponent<LiveText> {
    public:
        LiveText(Rml::Element *parent, std::function<Rml::String()> getText)
            : FluentComponent(append(parent, "div"))
            , mGetText(std::move(getText))
        {
            update();
        }

        void update() override
        {
            Rml::String text = mGetText();
            if (text != mText) {
                mRoot->SetInnerRML(escape(text));
                mText = std::move(text);
            }
        }

        bool focus() override
        {
            return false;
        }

    private:
        std::function<Rml::String()> mGetText;
        Rml::String mText = "\x01";
    };

    // Live text that tells its owner when the tab it is on closes.
    class WatchedText : public LiveText {
    public:
        WatchedText(Rml::Element *parent, std::function<Rml::String()> getText, std::function<void()> onGone)
            : LiveText(parent, std::move(getText))
            , mOnGone(std::move(onGone))
        {
        }
        ~WatchedText() override
        {
            if (mOnGone) {
                mOnGone();
            }
        }

    private:
        std::function<void()> mOnGone;
    };

    // A button that can be disabled by a getter.
    class ActionButton : public Button {
    public:
        ActionButton(Rml::Element *parent, Rml::String text, std::function<bool()> isDisabled)
            : Button(parent, std::move(text))
            , mIsDisabled(std::move(isDisabled))
        {
        }

        void update() override
        {
            set_disabled(mIsDisabled && mIsDisabled());
            Button::update();
        }

    private:
        std::function<bool()> mIsDisabled;
    };

    // A text field that can be disabled by a getter. When the label shows a shortened value or a placeholder,
    // getInput gives the full value to edit: the input starts out with it (and a phone's soft keyboard with it),
    // so "(none)" or a "..." code would otherwise be committed as typed.
    class TextButton : public StringButton {
    public:
        TextButton(Rml::Element *parent, StringButton::Props props, std::function<bool()> isDisabled,
            std::function<Rml::String()> getInput = {})
            : StringButton(parent, std::move(props))
            , mIsDisabled(std::move(isDisabled))
            , mGetInput(std::move(getInput))
        {
        }

        bool disabled() const override
        {
            return mIsDisabled && mIsDisabled();
        }

    protected:
        Rml::String input_value() override
        {
            return mGetInput ? mGetInput() : StringButton::input_value();
        }

    private:
        std::function<bool()> mIsDisabled;
        std::function<Rml::String()> mGetInput;
    };

    void help(Pane &left, Pane &right, Component &control, Rml::String text)
    {
        left.register_control(control, right, [text = std::move(text)](Pane &pane) { pane.add_rml(text); });
    }

    Rml::String relay_name(const Profile &profile)
    {
        if (profile.relay == "off") {
            return "Off";
        }
        if (profile.relay == "own") {
            return "Own";
        }
        return "Public";
    }

    Rml::String player_list(const Profile &profile)
    {
        Rml::String names;
        for (int i = 0; i < profile.local; ++i) {
            names += (i ? ", " : "") + profile.players[i].name;
        }
        return names;
    }

    const char *const kTeamNames[2] = { "Team A", "Team B" };

    // N10: the rejoin token lives in the user dir (pbnet keeps it in <dir>/rejoin.txt).
    void set_recover_dir()
    {
        pbnet_recover_set_dir(PartyBoard_ConfigPath.string().c_str());
    }

    // A pbnet snprintf-style JSON getter's text ("" when it gives none).
    std::string pbnet_text(int (*get)(char *, int))
    {
        int len = get(nullptr, 0);
        if (len <= 0) {
            return {};
        }
        std::string text(static_cast<size_t>(len) + 1, '\0');
        len = get(text.data(), static_cast<int>(text.size()));
        text.resize(static_cast<size_t>(std::clamp(len, 0, static_cast<int>(text.size()) - 1)));
        return text;
    }

    // The match this device can rejoin (its id), or "" if none.
    std::string rejoin_match()
    {
        const auto j = nlohmann::json::parse(pbnet_text(pbnet_rejoin_record), nullptr, false);
        return j.is_object() && j.contains("id") && j["id"].is_string() ? j["id"].get<std::string>() : std::string();
    }

    // The modal the match's hold shows; it marks itself gone when the UI deletes it.
    class HoldModal : public Modal {
    public:
        HoldModal(Props props, std::shared_ptr<bool> alive)
            : Modal(std::move(props))
            , mAlive(std::move(alive))
        {
            *mAlive = true;
        }
        ~HoldModal() override
        {
            *mAlive = false;
        }

    private:
        std::shared_ptr<bool> mAlive;
    };

    // The user dir as pbnet takes it (UTF-8).
    std::string user_dir_utf8()
    {
        const auto u8 = PartyBoard_ConfigPath.u8string();
        return std::string(u8.begin(), u8.end());
    }

    // This device's online saves (pbnet_save_list), newest first, with their seat keys.
    std::vector<SaveInfo> load_saves()
    {
        const std::string dir = user_dir_utf8();
        int len = pbnet_save_list(dir.c_str(), nullptr, 0);
        std::vector<SaveInfo> saves;
        if (len <= 0) {
            return saves;
        }
        std::string json(static_cast<size_t>(len) + 1, '\0');
        len = pbnet_save_list(dir.c_str(), json.data(), static_cast<int>(json.size()));
        json.resize(static_cast<size_t>(std::clamp(len, 0, static_cast<int>(json.size()) - 1)));
        parse_saves(json, saves);
        for (auto &save : saves) {
            load_save_keys(save);
        }
        return saves;
    }

    Rml::String local_date(int64_t unixSeconds)
    {
        const std::time_t when = static_cast<std::time_t>(unixSeconds);
        std::tm local {};
        if (const std::tm *tm = std::localtime(&when)) {
            local = *tm;
        }
        return fmt::format("{:%Y-%m-%d %H:%M}", local);
    }

    Rml::String turn_text(const SaveInfo &save)
    {
        return save.maxTurn > 0 ? fmt::format("Turn {}/{}", save.turn, save.maxTurn) : fmt::format("Turn {}", save.turn);
    }

    // The details pane of a save: board, turn, date and who sat where (this device's seats marked).
    Rml::String save_details(const SaveInfo &save, const Profile &profile, const Rml::String &date)
    {
        Rml::String rml = fmt::format("<b>{}</b><br/>{}, saved {}.<br/><br/>", escape(kBoardNames[save.board]),
            escape(turn_text(save)), escape(date));
        const auto own = own_seats(save, profile);
        for (int i = 0; i < static_cast<int>(save.seats.size()); ++i) {
            const SavedSeat &s = save.seats[i];
            const bool mine = std::find(own.begin(), own.end(), i) != own.end();
            rml += fmt::format("{}P: {} - {}{}<br/>", i + 1, s.cpu ? Rml::String("CPU") : escape(s.name),
                kCharacterNames[s.character], mine ? " (this device)" : "");
        }
        rml += "<br/>Press to host this match again from where it was saved. Send the code to the others: each "
               "player gets their seat back, and the computer plays the seats nobody takes.";
        return rml;
    }

} // namespace

OnlineWindow::OnlineWindow()
    : mProfile(load_profile(profile_path()))
{
    set_recover_dir();
    add_tab("Host", [this](Rml::Element *content) {
        auto &left = add_child<Pane>(content, Pane::Type::Controlled);
        auto &right = add_child<Pane>(content, Pane::Type::Uncontrolled);

        const int state = pbnet_lobby_state();
        if (state == Connecting || state == Open) {
            left.add_section("Lobby");
            help(left, right, left.add_button("Back to Lobby").on_pressed([this] { open_lobby(); }),
                "Go back to the lobby you are in.");
        }

        left.add_section("Host a Game");
        left.add_child<LiveText>([this] { return fmt::format("Players: {}", player_list(mProfile)); });
        left.add_child<LiveText>([this] { return fmt::format("Relay: {}", relay_name(mProfile)); });
        help(left, right, left.add_button("Host Game").on_pressed([this] { host(); }),
            "Open a lobby and get a code to send to your friends.<br/><br/>Players and relay are set in the "
            "<b>Players</b> tab.");
        left.finalize();
    });

    add_tab("Join", [this](Rml::Element *content) {
        auto &left = add_child<Pane>(content, Pane::Type::Controlled);
        auto &right = add_child<Pane>(content, Pane::Type::Uncontrolled);

        // N10: a match this device dropped out of (the app closed or crashed mid-match)
        if (const std::string match = rejoin_match(); !match.empty() && pbnet_lobby_state() == None) {
            left.add_section("Your Match");
            help(left, right, left.add_button("Rejoin Match").on_pressed([this] { rejoin(); }),
                fmt::format("Go back to the online match <b>{}</b> you were playing. Everyone goes back to its last "
                            "save point (after the last minigame) and plays on from there.",
                    escape(match)));
            help(left, right, left.add_button("Forget Match").on_pressed([] {
                pbnet_rejoin_forget();
                push_toast({ .title = "Online match", .content = "You won't rejoin it.", .duration = std::chrono::seconds(2) });
            }),
                "Don't go back to that match. The host can let the computer play your seat.");
        }

        left.add_section("Join by Code");
        help(left, right,
            left.add_child<TextButton>(
                StringButton::Props {
                    .key = "Code",
                    .getValue = [this] { return mProfile.lastCode.empty() ? Rml::String("(none)") : shorten(mProfile.lastCode, 20); },
                    .setValue =
                        [this](Rml::String value) {
                            mProfile.lastCode = trim(std::move(value));
                            save();
                        },
                    .maxLength = 1024,
                },
                nullptr, [this] { return mProfile.lastCode; }),
            "The code the host sent you: a short code, a long code or a link.");
        help(left, right, left.add_button("Paste Code").on_pressed([this] {
            if (char *text = SDL_GetClipboardText()) {
                const std::string code = trim(text);
                SDL_free(text);
                if (!code.empty()) {
                    mProfile.lastCode = code;
                    save();
                }
            }
        }),
            "Use the code on the clipboard.");
        help(left, right, left.add_child<ActionButton>("Join Game", [this] { return mProfile.lastCode.empty(); }).on_pressed([this] { join(); }),
            "Join the lobby with this code.");
        left.finalize();
    });

    add_tab("LAN", [this](Rml::Element *content) {
        auto &left = add_child<Pane>(content, Pane::Type::Controlled);
        auto &right = add_child<Pane>(content, Pane::Type::Uncontrolled);

        const int state = pbnet_lobby_state();
        if (state == Connecting || state == Open) {
            left.add_section("Lobby");
            help(left, right, left.add_button("Back to Lobby").on_pressed([this] { open_lobby(); }),
                "Go back to the lobby you are in.");
            left.add_text("Leave the lobby to find games on this network.");
            left.finalize();
            return;
        }

        // Browses until the tab closes (the status line goes with it); update() polls and rebuilds the list.
        if (mLan.state.empty()) {
            poll_lan();
        }
        mLanOpen = true;
        mLanLeft = &left;
        mLanKey = lan_key(mLan);
        mLanButtons.clear();

        left.add_section("Games on This Network");
        left.add_child<WatchedText>([this] { return lan_status(); }, [this] {
            mLanOpen = false;
            mLanLeft = nullptr;
            mLanButtons.clear();
        });
        for (const auto &game : mLan.games) {
            const Rml::String name = game.name.empty() ? Rml::String("Game") : shorten(game.name, 20);
            const Rml::String seats = game.open == 1 ? Rml::String("1 seat open") : fmt::format("{} seats open", game.open);
            auto &button = left.add_select_button({
                .key = name,
                .getValue = [seats] { return seats; },
                .isDisabled = [open = game.open] { return open == 0; },
            });
            button.on_pressed([this, code = game.code] { join_lan(code); });
            help(left, right, button,
                fmt::format("<b>{}</b>'s lobby: {}.<br/><br/>Press to join it.", escape(name), seats));
            mLanButtons.emplace_back(game.code, &button);
        }
        left.add_text("A lobby hosted on this network shows up here while it waits for players. If it doesn't, ask "
                      "the host for the code and use Join.");
        left.finalize();
    });

    add_tab("Continue", [this](Rml::Element *content) {
        auto &left = add_child<Pane>(content, Pane::Type::Controlled);
        auto &right = add_child<Pane>(content, Pane::Type::Uncontrolled);

        const int state = pbnet_lobby_state();
        if (state == Connecting || state == Open) {
            left.add_section("Lobby");
            help(left, right, left.add_button("Back to Lobby").on_pressed([this] { open_lobby(); }),
                "Go back to the lobby you are in. Continuing a saved match leaves it.");
        }

        left.add_section("Saved Online Matches");
        const auto saves = load_saves();
        if (saves.empty()) {
            left.add_text("No saved online matches. A match is saved after every minigame.");
        }
        for (const auto &save : saves) {
            const Rml::String date = local_date(save.time);
            auto &button = left.add_select_button({
                .key = kBoardNames[save.board],
                .getValue = [turn = turn_text(save)] { return turn; },
            });
            button.on_pressed([this, save] { pick_save(save); });
            help(left, right, button, save_details(save, mProfile, date));
        }
        left.finalize();
    });

    add_tab("Players", [this](Rml::Element *content) {
        auto &left = add_child<Pane>(content, Pane::Type::Controlled);
        auto &right = add_child<Pane>(content, Pane::Type::Uncontrolled);

        left.add_section("This Device");
        help(left, right,
            left.add_child<NumberButton>(NumberButton::Props {
                .key = "Players on This Device",
                .getValue = [this] { return mProfile.local; },
                .setValue =
                    [this](int value) {
                        mProfile.local = std::clamp(value, 1, kSeats);
                        save();
                    },
                .min = 1,
                .max = kSeats,
            }),
            "How many people play on this device. Each one takes a seat and picks a character.");
        for (int i = 0; i < kSeats; ++i) {
            help(left, right,
                left.add_child<TextButton>(
                    StringButton::Props {
                        .key = fmt::format("Player {} Name", i + 1),
                        .getValue = [this, i] { return mProfile.players[i].name; },
                        .setValue =
                            [this, i](Rml::String value) {
                                value = trim(std::move(value));
                                if (!value.empty()) {
                                    mProfile.players[i].name = std::move(value);
                                    save();
                                }
                            },
                        .maxLength = 16,
                    },
                    [this, i] { return i >= mProfile.local; }),
                "The name the other players see.");
        }

        left.add_section("Network");
        left.register_control(left.add_select_button({
                                  .key = "Relay",
                                  .getValue = [this] { return relay_name(mProfile); },
                              }),
            right, [this](Pane &pane) {
                const auto option = [this, &pane](const char *id, const char *label) {
                    pane.add_button({
                                        .text = label,
                                        .isSelected = [this, id] { return mProfile.relay == id; },
                                    })
                        .on_pressed([this, id] {
                            mProfile.relay = id;
                            save();
                        });
                };
                option("n0", "Public");
                option("own", "Own");
                option("off", "Off");
                pane.add_rml("<br/>When two devices can't reach each other directly, the connection goes through a "
                             "relay.<br/><br/><b>Public</b>: the free public relays. <b>Own</b>: a relay server you run. "
                             "<b>Off</b>: same network or direct connections only.");
            });
        help(left, right,
            left.add_child<TextButton>(
                StringButton::Props {
                    .key = "Own Relay URL",
                    .getValue = [this] { return mProfile.relayUrl.empty() ? Rml::String("(none)") : shorten(mProfile.relayUrl, 24); },
                    .setValue =
                        [this](Rml::String value) {
                            mProfile.relayUrl = trim(std::move(value));
                            save();
                        },
                    .maxLength = 256,
                },
                [this] { return mProfile.relay != "own"; }, [this] { return mProfile.relayUrl; }),
            "The https:// address of your own relay server.");
        left.finalize();
    });
}

void OnlineWindow::save()
{
    save_profile(profile_path(), mProfile);
}

void OnlineWindow::open_lobby()
{
    push_document(std::make_unique<LobbyWindow>());
    hide(true);
}

void OnlineWindow::host()
{
    save();
    set_recover_dir();
    const std::string setup = setup_json(mProfile, build_id(), disc_id(), true);
    if (!pbnet_lobby_host(setup.c_str())) {
        show_message(*this, "Can't Host", "The lobby could not be opened. Check the relay setting in the Players tab.");
        return;
    }
    open_lobby();
}

void OnlineWindow::join()
{
    save();
    set_recover_dir();
    const std::string setup = setup_json(mProfile, build_id(), disc_id(), false);
    if (mProfile.lastCode.empty() || !pbnet_lobby_join(setup.c_str(), mProfile.lastCode.c_str())) {
        show_message(*this, "Can't Join", "Enter the code the host sent you. Check the relay setting in the Players tab.");
        return;
    }
    open_lobby();
}

void OnlineWindow::rejoin()
{
    save();
    set_recover_dir();
    const std::string setup = setup_json(mProfile, build_id(), disc_id(), false);
    if (!pbnet_lobby_rejoin(setup.c_str())) {
        show_message(*this, "Can't Rejoin", "That match can't be rejoined any more.");
        return;
    }
    open_lobby();
}

void OnlineWindow::pick_save(const SaveInfo &save)
{
    const Rml::String title = fmt::format("{}, {}", kBoardNames[save.board], turn_text(save));
    std::vector<ModalAction> actions;
    actions.push_back({ .label = "Host Match", .onPressed = [this, path = save.path](Modal &modal) {
                           modal.pop();
                           host_continue(path);
                       } });
    actions.push_back({ .label = "Delete Save", .onPressed = [this, path = save.path, title](Modal &modal) {
                           modal.pop();
                           confirm_delete_save(path, title);
                       } });
    actions.push_back({ .label = "Cancel", .onPressed = [](Modal &modal) { modal.pop(); } });
    push(std::make_unique<Modal>(Modal::Props {
        .title = title,
        .bodyRml = "Host this saved match again? You get a code to send to the others; it goes on from the last "
                   "minigame.",
        .actions = std::move(actions),
        .onDismiss = [](Modal &modal) { modal.pop(); },
        .icon = "question-mark",
    }));
}

void OnlineWindow::confirm_delete_save(const std::string &path, const Rml::String &title)
{
    std::vector<ModalAction> actions;
    actions.push_back({ .label = "Delete", .onPressed = [this, path](Modal &modal) {
                           modal.pop();
                           if (!pbnet_save_delete(path.c_str())) {
                               show_message(*this, "Can't Delete", "The save could not be deleted.");
                           }
                           refresh_active_tab();
                       } });
    actions.push_back({ .label = "Keep", .onPressed = [](Modal &modal) { modal.pop(); } });
    push(std::make_unique<Modal>(Modal::Props {
        .title = "Delete Save",
        .bodyRml = fmt::format("Delete the save of <b>{}</b> on this device? The other players keep theirs.", escape(title)),
        .actions = std::move(actions),
        .onDismiss = [](Modal &modal) { modal.pop(); },
        .icon = "question-mark",
    }));
}

void OnlineWindow::host_continue(const std::string &path)
{
    save();
    set_recover_dir();
    const std::string setup = setup_json(mProfile, build_id(), disc_id(), true, path);
    if (!pbnet_lobby_host(setup.c_str())) {
        show_message(*this, "Can't Continue", "The lobby could not be opened. Check the relay setting in the Players tab.");
        return;
    }
    open_lobby();
}

OnlineWindow::~OnlineWindow()
{
    // The LAN tab's status line resets mLan* as it goes: let it go while they are still there.
    clear_content();
    if (mBrowsing) {
        pbnet_lan_stop();
    }
}

void OnlineWindow::update()
{
    if (mLanOpen) {
        // Twice a second is enough for a list that changes when a host opens or closes a lobby.
        if (++mLanTick % 30 == 0) {
            poll_lan();
        }
        if (lan_key(mLan) != mLanKey) {
            rebuild_lan();
        }
    }
    else if (mBrowsing) {
        pbnet_lan_stop();
        mBrowsing = false;
        mLan = {};
        mLanKey.clear();
    }
    Window::update();
}

void OnlineWindow::poll_lan()
{
    // Not while in a lobby: it took the browse's endpoint, and a poll would bind another.
    const int state = pbnet_lobby_state();
    if (state == Connecting || state == Open || state == Started) {
        return;
    }
    const std::string setup = setup_json(mProfile, build_id(), disc_id(), false);
    int len = pbnet_lan_browse(setup.c_str(), nullptr, 0);
    mBrowsing = true;
    if (len <= 0) {
        return;
    }
    std::string json(static_cast<size_t>(len) + 1, '\0');
    len = pbnet_lan_browse(setup.c_str(), json.data(), static_cast<int>(json.size()));
    json.resize(static_cast<size_t>(std::clamp(len, 0, static_cast<int>(json.size()) - 1)));
    parse_lan(json, mLan);
}

void OnlineWindow::rebuild_lan()
{
    // Keep the player on the game they were on (or in the list) across the rebuild.
    auto *context = mDocument != nullptr ? mDocument->GetContext() : nullptr;
    auto *focused = context != nullptr ? context->GetFocusElement() : nullptr;
    const bool inList = focused != nullptr && mLanLeft != nullptr && mLanLeft->contains(focused);
    std::string focusedCode;
    for (const auto &[code, button] : mLanButtons) {
        if (inList && button->contains(focused)) {
            focusedCode = code;
        }
    }
    refresh_active_tab();
    if (!inList || mLanLeft == nullptr) {
        return;
    }
    for (const auto &[code, button] : mLanButtons) {
        if (code == focusedCode && button->focus()) {
            return;
        }
    }
    mLanLeft->focus();
}

void OnlineWindow::join_lan(const std::string &code)
{
    save();
    set_recover_dir();
    const std::string setup = setup_json(mProfile, build_id(), disc_id(), false);
    if (!pbnet_lobby_join(setup.c_str(), code.c_str())) {
        show_message(*this, "Can't Join", "That game can't be joined. Check the relay setting in the Players tab.");
        return;
    }
    // The lobby took the browse's endpoint.
    mBrowsing = false;
    open_lobby();
}

std::string OnlineWindow::lan_status() const
{
    if (mLan.state == "closed") {
        return mLan.error.empty() ? "Can't look for games on this network." : mLan.error;
    }
    if (mLan.state != "open") {
        return "Looking for games...";
    }
    if (!mLan.lan) {
        return "This device can't look for games on the network. Ask the host for the code and use Join.";
    }
    if (mLan.games.empty()) {
        return "No games found yet.";
    }
    return "Press a game to join it.";
}

void update_online_match()
{
    static std::shared_ptr<bool> alive = std::make_shared<bool>(false);
    static HoldModal *shown = nullptr;
    static std::string shownKey;
    static std::string dismissedKey;
    static int tick = 0;
    static std::string resyncShown;

    // A few times a second is enough for a 3 s hold.
    if (++tick % 10 != 0) {
        return;
    }
    if (!*alive) {
        shown = nullptr;
    }
    std::string message;
    std::string key;
    bool host = false;
    bool hostLeft = false;
    int waitIndex = -1;
    std::string waitName;
    std::string resync;
    if (pbnet_lobby_state() == Started) {
        const auto j = nlohmann::json::parse(pbnet_text(pbnet_recover_json), nullptr, false);
        if (j.is_object() && j.value("running", false)) {
            resync = j.value("resync", "");
            message = j.value("message", "");
            host = j.value("host", false);
            hostLeft = j.value("host_left", false);
            if (const auto w = j.find("waiting"); w != j.end() && w->is_array() && !w->empty() && (*w)[0].is_object()) {
                waitIndex = (*w)[0].value("index", -1);
                waitName = (*w)[0].value("name", "");
            }
            key = fmt::format("{}|{}|{}", message, waitIndex, j.value("epoch", 0));
        }
    }
    // N46: a desync is mended without asking anyone: a short notice, play goes on.
    if (!resync.empty() && resync != resyncShown) {
        push_toast({ .title = "Online match", .content = resync, .duration = std::chrono::seconds(3) });
    }
    resyncShown = resync;
    if (message.empty()) {
        dismissedKey.clear();
    }
    if (key == shownKey && (shown != nullptr || key == dismissedKey)) {
        return;
    }
    if (shown != nullptr) {
        shown->pop();
        shown = nullptr;
    }
    shownKey = key;
    if (message.empty() || key == dismissedKey) {
        return;
    }

    const auto keep = [key](Modal &modal) {
        dismissedKey = key;
        modal.pop();
    };
    std::vector<ModalAction> actions;
    Rml::String body = escape(message);
    if (hostLeft) {
        body += "<br/><br/>Your copy of the match is saved too.";
        actions.push_back({ .label = "Leave Match", .onPressed = [](Modal &modal) {
                               modal.pop();
                               pbnet_rejoin_forget();
                               pbnet_lobby_leave();
                           } });
    }
    else if (host && waitIndex > 0) {
        body += "<br/><br/>The match holds until they are back. You can let the computer play for them (they take "
                "the seat back when they return; everyone goes back to the last save point), or save and quit: "
                "every player keeps the save, and any of them can host Continue.";
        actions.push_back({ .label = "Keep Waiting", .onPressed = keep });
        actions.push_back({ .label = fmt::format("CPU Plays for {}", waitName), .onPressed = [waitIndex](Modal &modal) {
                               modal.pop();
                               pbnet_recover_cpu(waitIndex);
                           } });
        actions.push_back({ .label = "Save and Quit", .onPressed = [](Modal &modal) {
                               modal.pop();
                               pbnet_lobby_leave();
                           } });
    }
    else {
        body += "<br/><br/>The match holds until they are back. The host can let the computer play for them.";
        actions.push_back({ .label = "Keep Waiting", .onPressed = keep });
        actions.push_back({ .label = "Leave Match", .onPressed = [](Modal &modal) {
                               // the rejoin record stays: Online > Join > Rejoin Match goes back
                               modal.pop();
                               pbnet_lobby_leave();
                           } });
    }
    // N38: a flag per modal. The one this replaces is deleted later in this loop; with a shared flag its destructor
    // marked the new modal gone, which then stayed up (a "Host left" over the running match after a reconnect).
    alive = std::make_shared<bool>(false);
    auto modal = std::make_unique<HoldModal>(
        Modal::Props {
            .title = hostLeft ? "Host Left" : "Waiting",
            .bodyRml = body,
            .actions = std::move(actions),
            .onDismiss = keep,
            .icon = "question-mark",
        },
        alive);
    shown = modal.get();
    push_document(std::move(modal));
}

void open_lobby_after_match()
{
    push_toast({ .title = "Online match", .content = "The match is over. Back in the lobby.", .duration = std::chrono::seconds(3) });
    push_document(std::make_unique<LobbyWindow>());
}

LobbyWindow::LobbyWindow()
{
    refresh();

    add_tab("Lobby", [this](Rml::Element *content) {
        mRightPane = nullptr;
        mRightBuilder = {};
        auto &left = add_child<Pane>(content, Pane::Type::Controlled);
        auto &right = add_child<Pane>(content, Pane::Type::Uncontrolled);
        const auto control = [this, &left, &right](Component &c, std::function<void(Pane &)> builder) {
            left.register_control(c, right, [this, builder = std::move(builder)](Pane &pane) { show_right(pane, builder); });
        };

        left.add_child<LiveText>([this]() -> Rml::String {
            switch (mView.state) {
                case Connecting:
                    return mView.host ? "Going online..." : "Joining...";
                case Open: {
                    if (!mView.hasRoom) {
                        return "Waiting for a seat...";
                    }
                    const auto waiting = not_ready(mView);
                    if (waiting.empty()) {
                        return mView.host ? "Everyone is ready." : "Everyone is ready. Waiting for the host to start.";
                    }
                    Rml::String names;
                    for (size_t i = 0; i < waiting.size(); ++i) {
                        names += (i ? ", " : "") + waiting[i];
                    }
                    return fmt::format("Waiting for {} to be ready.", names);
                }
                case Started:
                    return "The match is starting.";
                case Closed:
                    return mView.error.empty() ? "The lobby closed." : mView.error;
                default:
                    return "Not in a lobby.";
            }
        });
        // N29: a continued save keeps its seats, characters and rules.
        left.add_child<LiveText>([this]() -> Rml::String {
            if (!mView.resume) {
                return "";
            }
            return mView.resumeMaxTurn > 0
                ? fmt::format("Continuing a saved match at turn {} of {}.", mView.resumeTurn, mView.resumeMaxTurn)
                : Rml::String("Continuing a saved match.");
        });

        if (mView.host) {
            left.add_section("Invite");
            control(left.add_select_button({
                            .key = "Code",
                            .getValue = [this] { return mView.shortCode.empty() ? Rml::String("...") : mView.shortCode; },
                            .submit = false,
                        })
                        .on_nav_command([this](Rml::Event &, NavCommand cmd) {
                            if (cmd == NavCommand::Confirm) {
                                copy_text(mView.shortCode.empty() ? mView.code : mView.shortCode, "Code");
                                return true;
                            }
                            return false;
                        }),
                [this](Pane &pane) {
                    pane.add_rml("Send this code to your friends; they enter it under <b>Online &gt; Join</b>. "
                                 "Press to copy it.");
                    if (mView.shortCode.empty() && !mView.error.empty()) {
                        pane.add_rml("<br/>" + escape(mView.error) + "<br/>Copy the long code instead.");
                    }
                });
            control(left.add_child<ActionButton>("Copy Long Code", [this] { return mView.code.empty(); }).on_pressed([this] {
                copy_text(mView.code, "Long code");
            }),
                [](Pane &pane) {
                    pane.add_rml("The long code works without the short-code service, for example on a network "
                                 "without internet.");
                });
        }

        left.add_section("Seats");
        for (int i = 0; i < kSeats; ++i) {
            control(left.add_select_button({
                        .key = fmt::format("{}P", i + 1),
                        .getValue =
                            [this, i]() -> Rml::String {
                                const Seat &s = mView.seats[i];
                                if (!mView.hasRoom) {
                                    return "";
                                }
                                if (s.open) {
                                    return "Open";
                                }
                                if (s.cpu && mView.resume) {
                                    // the saved player's seat ("<name> (CPU)") until they join
                                    return fmt::format("{} - {} ({})", s.name, kCharacterNames[s.character], kDifficultyNames[s.difficulty]);
                                }
                                if (s.cpu) {
                                    return fmt::format("CPU ({}) - {}", kDifficultyNames[s.difficulty], kCharacterNames[s.character]);
                                }
                                Rml::String text = fmt::format("{} - {}", s.name, kCharacterNames[s.character]);
                                if (mView.teams) {
                                    text += fmt::format(" - {}", kTeamNames[s.team]);
                                }
                                if (s.peer != "host" && peer_ready(mView, s.peer)) {
                                    text += " - Ready";
                                }
                                return text;
                            },
                    }),
                [this, i](Pane &pane) {
                    const Seat &s = mView.seats[i];
                    if (!mView.hasRoom) {
                        return;
                    }
                    if (!s.open && !s.cpu) {
                        const bool mine = std::find(mView.mySeats.begin(), mView.mySeats.end(), i) != mView.mySeats.end();
                        pane.add_text(mine ? fmt::format("{} plays on this device.", s.name)
                                           : s.peer == "host" ? fmt::format("{} is the host.", s.name)
                                                              : fmt::format("{} joined.", s.name));
                        return;
                    }
                    if (!mView.host) {
                        pane.add_text(mView.resume ? "The computer plays this seat until its player joins."
                                : s.cpu        ? "The computer plays this seat."
                                               : "Open: a friend can join here. Open seats are played by the computer.");
                        return;
                    }
                    if (!mView.resume) {
                        pane.add_button({
                                            .text = "Open",
                                            .isSelected = [this, i] { return mView.seats[i].open; },
                                        })
                            .on_pressed([i] { pbnet_lobby_cpu(i, -1); });
                    }
                    for (int d = 0; d < kDifficulties; ++d) {
                        pane.add_button({
                                            .text = fmt::format("CPU ({})", kDifficultyNames[d]),
                                            .isSelected = [this, i, d] { return mView.seats[i].cpu && mView.seats[i].difficulty == d; },
                                        })
                            .on_pressed([i, d] { pbnet_lobby_cpu(i, d); });
                    }
                    pane.add_rml(mView.resume ? "<br/>The player of this seat gets it back when they join (by their "
                                                "player key); anyone else joining takes a free seat. Until then the "
                                                "computer plays it at this level."
                                              : "<br/>An open seat waits for a friend to join; at the start the computer plays it.");
                });
        }

        left.add_section("Your Characters");
        for (int k = 0; k < kSeats; ++k) {
            const auto seat = [this, k] { return k < static_cast<int>(mView.mySeats.size()) ? mView.mySeats[k] : -1; };
            auto &button = left.add_select_button({
                .key = fmt::format("Player {}", k + 1),
                .getValue =
                    [this, seat]() -> Rml::String {
                        const int i = seat();
                        if (i < 0) {
                            return "";
                        }
                        const Seat &s = mView.seats[i];
                        return mView.teams ? fmt::format("{} - {}", kCharacterNames[s.character], kTeamNames[s.team])
                                           : Rml::String(kCharacterNames[s.character]);
                    },
                .isDisabled = [this, seat] { return seat() < 0 || mView.state != Open || mView.resume; },
            });
            button.on_nav_command([this, k, seat](Rml::Event &, NavCommand cmd) {
                if (cmd != NavCommand::Left && cmd != NavCommand::Right) {
                    return false;
                }
                const int i = seat();
                const int dir = cmd == NavCommand::Right ? 1 : kCharacters - 1;
                int c = mView.seats[i].character;
                for (int n = 0; n < kCharacters; ++n) {
                    c = (c + dir) % kCharacters;
                    if (!character_taken(mView, c, i)) {
                        send_picks(k, c, mView.seats[i].team);
                        break;
                    }
                }
                return true;
            });
            control(button, [this, k, seat](Pane &pane) {
                const int i = seat();
                if (i < 0) {
                    return;
                }
                pane.add_section(fmt::format("{}P: {}", i + 1, mView.seats[i].name));
                for (int c = 0; c < kCharacters; ++c) {
                    auto &b = pane.add_button({
                                                  .text = kCharacterNames[c],
                                                  .isSelected = [this, seat, c] { return seat() >= 0 && mView.seats[seat()].character == c; },
                                              })
                                  .on_pressed([this, k, seat, c] {
                                      if (seat() >= 0) {
                                          send_picks(k, c, mView.seats[seat()].team);
                                      }
                                  });
                    b.set_disabled(character_taken(mView, c, i));
                }
                if (mView.teams) {
                    pane.add_section("Team");
                    for (int t = 0; t < 2; ++t) {
                        pane.add_button({
                                            .text = kTeamNames[t],
                                            .isSelected = [this, seat, t] { return seat() >= 0 && mView.seats[seat()].team == t; },
                                        })
                            .on_pressed([this, k, seat, t] {
                                if (seat() >= 0) {
                                    send_picks(k, mView.seats[seat()].character, t);
                                }
                            });
                    }
                }
            });
        }

        left.add_section("Match");
        if (mView.host) {
            control(left.add_child<ActionButton>("Start Match", [this] { return mView.state != Open; }).on_pressed([this] {
                char why[256] = {};
                if (!pbnet_lobby_start(0, why, sizeof(why))) {
                    show_message(*this, "Can't Start", why[0] ? why : "The match could not be started.");
                }
            }),
                [](Pane &pane) {
                    pane.add_rml("Start once everyone is ready. Open seats are played by the computer.");
                });
        }
        else {
            auto &ready = left.add_child<BoolButton>(BoolButton::Props {
                .key = "Ready",
                .getValue = [this] { return peer_ready(mView, mView.me); },
                .setValue = [](bool value) { pbnet_lobby_ready(value ? 1 : 0); },
                .isDisabled = [this] { return mView.state != Open || !mView.hasRoom; },
            });
            control(ready, [](Pane &pane) {
                pane.add_rml("Tell the host you are ready. A change to the seats or rules clears it.");
            });
        }
        control(left.add_button("Leave Lobby").on_pressed([this] { confirm_leave(); }), [this](Pane &pane) {
            pane.add_rml(mView.host ? "Close the lobby for everyone." : "Leave the lobby; your seats open again.");
        });
        left.finalize();
    });

    add_tab("Rules", [this](Rml::Element *content) {
        mRightPane = nullptr;
        mRightBuilder = {};
        auto &left = add_child<Pane>(content, Pane::Type::Controlled);
        auto &right = add_child<Pane>(content, Pane::Type::Uncontrolled);
        const auto locked = [this] { return !mView.host || mView.state != Open || !mView.hasRoom || mView.resume; };

        left.add_section(mView.resume ? "Rules (the save's)" : mView.host ? "Rules" : "Rules (set by the host)");
        auto &board = left.add_select_button({
            .key = "Board",
            .getValue = [this] { return Rml::String(kBoardNames[mView.board]); },
            .isDisabled = locked,
        });
        board.on_nav_command([this](Rml::Event &, NavCommand cmd) {
            if (cmd != NavCommand::Left && cmd != NavCommand::Right) {
                return false;
            }
            const int b = (mView.board + (cmd == NavCommand::Right ? 1 : kBoards - 1)) % kBoards;
            send_settings(b, mView.turns, mView.bonus, mView.teams, mView.handicaps);
            return true;
        });
        left.register_control(board, right, [this](Pane &pane) {
            show_right(pane, [this](Pane &pane) {
                for (int b = 0; b < kBoards; ++b) {
                    pane.add_button({
                                        .text = kBoardNames[b],
                                        .isSelected = [this, b] { return mView.board == b; },
                                    })
                        .on_pressed([this, b] { send_settings(b, mView.turns, mView.bonus, mView.teams, mView.handicaps); });
                }
            });
        });
        help(left, right,
            left.add_child<NumberButton>(NumberButton::Props {
                .key = "Turns",
                .getValue = [this] { return mView.turns; },
                .setValue = [this](int value) { send_settings(mView.board, value, mView.bonus, mView.teams, mView.handicaps); },
                .isDisabled = locked,
                .min = kTurnsMin,
                .max = kTurnsMax,
                .step = 5,
            }),
            "How many turns the match lasts.");
        help(left, right,
            left.add_child<BoolButton>(BoolButton::Props {
                .key = "Bonus Stars",
                .getValue = [this] { return mView.bonus; },
                .setValue = [this](bool value) { send_settings(mView.board, mView.turns, value, mView.teams, mView.handicaps); },
                .isDisabled = locked,
            }),
            "Give out the bonus stars at the end of the match.");
        help(left, right,
            left.add_child<BoolButton>(BoolButton::Props {
                .key = "Team Battle",
                .getValue = [this] { return mView.teams; },
                .setValue = [this](bool value) { send_settings(mView.board, mView.turns, mView.bonus, value, mView.handicaps); },
                .isDisabled = locked,
            }),
            "Play two against two. Players pick their team with their character.");

        left.add_section("Handicaps");
        for (int i = 0; i < kSeats; ++i) {
            help(left, right,
                left.add_child<NumberButton>(NumberButton::Props {
                    .key = fmt::format("{}P Stars at Start", i + 1),
                    .getValue = [this, i] { return static_cast<int>(mView.handicaps[i]); },
                    .setValue =
                        [this, i](int value) {
                            uint8_t handicaps[kSeats];
                            std::copy(std::begin(mView.handicaps), std::end(mView.handicaps), handicaps);
                            handicaps[i] = static_cast<uint8_t>(std::clamp(value, 0, kHandicapMax));
                            send_settings(mView.board, mView.turns, mView.bonus, mView.teams, handicaps);
                        },
                    .isDisabled = locked,
                    .min = 0,
                    .max = kHandicapMax,
                }),
                "Stars this seat starts the match with.");
        }
        left.finalize();
    });
}

void LobbyWindow::refresh()
{
    int len = pbnet_lobby_json(nullptr, 0);
    std::string json(static_cast<size_t>(std::max(len, 0)) + 1, '\0');
    len = pbnet_lobby_json(json.data(), static_cast<int>(json.size()));
    json.resize(static_cast<size_t>(std::clamp(len, 0, static_cast<int>(json.size()) - 1)));
    if (!parse_view(json, mView)) {
        mView = {};
    }

    // Redraw the details for the new room unless the player is using them.
    if (mRightPane != nullptr && mRightBuilder) {
        auto *context = mDocument->GetContext();
        auto *focused = context != nullptr ? context->GetFocusElement() : nullptr;
        if (focused == nullptr || !mRightPane->contains(focused)) {
            mRightPane->clear();
            mRightBuilder(*mRightPane);
        }
    }
}

void LobbyWindow::show_right(Pane &pane, std::function<void(Pane &)> builder)
{
    mRightPane = &pane;
    mRightBuilder = std::move(builder);
    mRightBuilder(pane);
}

void LobbyWindow::update()
{
    if (const uint32_t gen = pbnet_lobby_gen(); gen != mGen || pbnet_lobby_state() != mView.state) {
        mGen = gen;
        refresh();
    }

    if (!mFinished && !pending_close()) {
        if (mView.state == Started) {
            // The match takes over from here (the lobby keeps running for it).
            mFinished = true;
            push_toast({ .title = "Online match", .content = "The match is starting.", .duration = std::chrono::seconds(3) });
            hide(true);
        }
        else if (mView.state == Closed) {
            mFinished = true;
            const auto back = [this](Modal &modal) {
                modal.pop();
                leave_to_menu();
            };
            push(std::make_unique<Modal>(Modal::Props {
                .title = "Lobby Closed",
                .bodyRml = escape(mView.error.empty() ? "The lobby closed." : mView.error),
                .actions = { ModalAction { .label = "OK", .onPressed = back } },
                .onDismiss = back,
            }));
        }
        else if (mView.state == None) {
            mFinished = true;
            leave_to_menu();
        }
    }

    Window::update();
}

bool LobbyWindow::consume_close_request()
{
    confirm_leave();
    return true;
}

void LobbyWindow::confirm_leave()
{
    const auto dismiss = [](Modal &modal) { modal.pop(); };
    push(std::make_unique<Modal>(Modal::Props {
        .title = "Leave Lobby",
        .bodyRml = mView.host ? "The lobby closes for everyone." : "Your seats open again for others.",
        .actions =
            {
                ModalAction { .label = "Stay", .onPressed = dismiss },
                ModalAction {
                    .label = "Leave",
                    .onPressed =
                        [this](Modal &modal) {
                            modal.pop();
                            mFinished = true;
                            leave_to_menu();
                        },
                },
            },
        .onDismiss = dismiss,
        .icon = "question-mark",
    }));
}

void LobbyWindow::leave_to_menu()
{
    pbnet_lobby_leave();
    hide(true);
    push_document(std::make_unique<OnlineWindow>());
}

void LobbyWindow::send_picks(int mine, int character, int team)
{
    const int n = static_cast<int>(mView.mySeats.size());
    if (mine < 0 || mine >= n) {
        return;
    }
    uint8_t characters[kSeats];
    uint8_t teams[kSeats];
    for (int k = 0; k < n; ++k) {
        const Seat &s = mView.seats[mView.mySeats[k]];
        characters[k] = static_cast<uint8_t>(s.character);
        teams[k] = static_cast<uint8_t>(s.team);
    }
    characters[mine] = static_cast<uint8_t>(character);
    teams[mine] = static_cast<uint8_t>(team);
    pbnet_lobby_picks(characters, teams, n);
}

void LobbyWindow::send_settings(int board, int turns, bool bonus, bool teams, const uint8_t *handicaps)
{
    if (!mView.host) {
        return;
    }
    pbnet_lobby_settings(board, turns, bonus ? 1 : 0, teams ? 1 : 0, handicaps);
}

} // namespace partyboard::ui
