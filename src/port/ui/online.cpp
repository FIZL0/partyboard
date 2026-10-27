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

#include <algorithm>
#include <chrono>
#include <ctime>

namespace partyboard::ui {
namespace {

    using namespace online;

    std::filesystem::path profile_path()
    {
        return PartyBoard_ConfigPath / "online.json";
    }

    std::filesystem::path saves_dir()
    {
        return PartyBoard_ConfigPath / "online";
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

    // Not std::chrono::clock_cast: the NDK's and Apple's libc++ don't have it. Off by the few microseconds
    // between the two now() calls, which a minute-resolution date doesn't show.
    std::chrono::system_clock::time_point file_time_to_system(std::filesystem::file_time_type t)
    {
        using std::chrono::system_clock;
        return system_clock::now()
            + std::chrono::duration_cast<system_clock::duration>(t - std::filesystem::file_time_type::clock::now());
    }

} // namespace

OnlineWindow::OnlineWindow()
    : mProfile(load_profile(profile_path()))
{
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
        add_child<Pane>(content, Pane::Type::Uncontrolled);

        left.add_section("Games on This Network");
        left.add_text("Finding games on this network is not available in this build yet.");
        left.add_text("Ask the host for the code and use Join. Hosting with the relay off works on a "
                      "shared network without internet.");
        left.finalize();
    });

    add_tab("Continue", [this](Rml::Element *content) {
        auto &left = add_child<Pane>(content, Pane::Type::Controlled);
        auto &right = add_child<Pane>(content, Pane::Type::Uncontrolled);

        left.add_section("Saved Online Matches");
        const auto saves = list_saves(saves_dir());
        if (saves.empty()) {
            left.add_text("No saved online matches. A match is saved after every minigame.");
        }
        for (const auto &save : saves) {
            const std::time_t when = std::chrono::system_clock::to_time_t(file_time_to_system(save.modified));
            std::tm local {};
            if (const std::tm *tm = std::localtime(&when)) {
                local = *tm;
            }
            const Rml::String date = fmt::format("{:%Y-%m-%d %H:%M}", local);
            auto &button = left.add_select_button({
                .key = save.id,
                .getValue = [date] { return date; },
                .submit = false,
            });
            help(left, right, button,
                fmt::format("Saved {}.<br/><br/>Continuing a saved match is not available in this build yet.", escape(date)));
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
    const std::string setup = setup_json(mProfile, build_id(), disc_id(), false);
    if (mProfile.lastCode.empty() || !pbnet_lobby_join(setup.c_str(), mProfile.lastCode.c_str())) {
        show_message(*this, "Can't Join", "Enter the code the host sent you. Check the relay setting in the Players tab.");
        return;
    }
    open_lobby();
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
                        pane.add_text(s.cpu ? "The computer plays this seat." : "Open: a friend can join here. "
                                                                                 "Open seats are played by the computer.");
                        return;
                    }
                    pane.add_button({
                                        .text = "Open",
                                        .isSelected = [this, i] { return mView.seats[i].open; },
                                    })
                        .on_pressed([i] { pbnet_lobby_cpu(i, -1); });
                    for (int d = 0; d < kDifficulties; ++d) {
                        pane.add_button({
                                            .text = fmt::format("CPU ({})", kDifficultyNames[d]),
                                            .isSelected = [this, i, d] { return mView.seats[i].cpu && mView.seats[i].difficulty == d; },
                                        })
                            .on_pressed([i, d] { pbnet_lobby_cpu(i, d); });
                    }
                    pane.add_rml("<br/>An open seat waits for a friend to join; at the start the computer plays it.");
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
                .isDisabled = [this, seat] { return seat() < 0 || mView.state != Open; },
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
        const auto locked = [this] { return !mView.host || mView.state != Open || !mView.hasRoom; };

        left.add_section(mView.host ? "Rules" : "Rules (set by the host)");
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
