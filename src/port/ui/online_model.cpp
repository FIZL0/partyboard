#include "online_model.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstdint>
#include <fstream>
#include <random>
#include <system_error>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/file.h>
#include <unistd.h>
#endif

namespace partyboard::online {

const char *const kCharacterNames[kCharacters] = {
    "Mario",
    "Luigi",
    "Peach",
    "Yoshi",
    "Wario",
    "Donkey Kong",
    "Daisy",
    "Waluigi",
};

const char *const kBoardNames[kBoards] = {
    "Toad's Midway Madness",
    "Goomba's Greedy Gala",
    "Shy Guy's Jungle Jam",
    "Boo's Haunted Bash",
    "Koopa's Seaside Soiree",
    "Bowser's Gnarly Party",
};

const char *const kDifficultyNames[kDifficulties] = {
    "Easy",
    "Normal",
    "Hard",
    "Very Hard",
};

namespace {

    std::string random_key()
    {
        std::random_device rd;
        static constexpr char kHex[] = "0123456789abcdef";
        std::string key;
        for (int i = 0; i < 8; ++i) {
            const uint32_t word = rd();
            for (int j = 0; j < 4; ++j) {
                const uint8_t b = static_cast<uint8_t>(word >> (j * 8));
                key += kHex[b >> 4];
                key += kHex[b & 15];
            }
        }
        return key;
    }

    std::string str(const nlohmann::json &j, const char *field)
    {
        auto it = j.find(field);
        return it != j.end() && it->is_string() ? it->get<std::string>() : std::string {};
    }

    int num(const nlohmann::json &j, const char *field, int fallback)
    {
        auto it = j.find(field);
        return it != j.end() && it->is_number_integer() ? it->get<int>() : fallback;
    }

    bool flag(const nlohmann::json &j, const char *field, bool fallback)
    {
        auto it = j.find(field);
        return it != j.end() && it->is_boolean() ? it->get<bool>() : fallback;
    }

    int parse_state(const std::string &s)
    {
        if (s == "connecting") {
            return Connecting;
        }
        if (s == "open") {
            return Open;
        }
        if (s == "started") {
            return Started;
        }
        if (s == "closed") {
            return Closed;
        }
        return None;
    }

} // namespace

Profile load_profile(const std::filesystem::path &path)
{
    Profile profile;
    std::ifstream in(path);
    if (in) {
        const auto j = nlohmann::json::parse(in, nullptr, false);
        if (j.is_object()) {
            if (auto it = j.find("players"); it != j.end() && it->is_array()) {
                for (int i = 0; i < kSeats && i < static_cast<int>(it->size()); ++i) {
                    if ((*it)[i].is_object()) {
                        profile.players[i].name = str((*it)[i], "name");
                        profile.players[i].key = str((*it)[i], "key");
                    }
                }
            }
            profile.local = std::clamp(num(j, "local", 1), 1, kSeats);
            profile.relay = str(j, "relay");
            profile.relayUrl = str(j, "relay_url");
            profile.lastCode = str(j, "last_code");
        }
    }
    if (profile.relay != "own" && profile.relay != "off") {
        profile.relay = "n0";
    }
    for (int i = 0; i < kSeats; ++i) {
        if (profile.players[i].name.empty()) {
            profile.players[i].name = "Player " + std::to_string(i + 1);
        }
        if (profile.players[i].key.empty()) {
            profile.players[i].key = random_key();
        }
    }
    return profile;
}

namespace {

    // Holds `path` locked for the rest of the process (the handle is never closed); false if another process
    // holds it. The OS drops the lock when the process ends, crashed or not.
    bool lock_for_process(const std::filesystem::path &path)
    {
#ifdef _WIN32
        // No sharing: a second open fails while the first copy's handle is open.
        const HANDLE h = CreateFileW(path.wstring().c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_ALWAYS,
            FILE_ATTRIBUTE_NORMAL, nullptr);
        return h != INVALID_HANDLE_VALUE;
#else
        const int fd = open(path.c_str(), O_RDWR | O_CREAT | O_CLOEXEC, 0644);
        if (fd < 0) {
            return false;
        }
        if (flock(fd, LOCK_EX | LOCK_NB) != 0) {
            close(fd);
            return false;
        }
        return true;
#endif
    }

} // namespace

std::filesystem::path claim_profile(const std::filesystem::path &dir)
{
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    for (int n = 1; n <= 8; ++n) {
        const std::string stem = n == 1 ? std::string("online") : "online-" + std::to_string(n);
        if (lock_for_process(dir / (stem + ".lock"))) {
            return dir / (stem + ".json");
        }
    }
    return dir / "online.json";
}

bool adopt_keys(Profile &profile, const View &view)
{
    bool changed = false;
    for (size_t p = 0; !view.host && p < view.mySeats.size() && p < kSeats; ++p) {
        const int i = view.mySeats[p];
        if (i < 0 || i >= kSeats || view.seats[i].open || view.seats[i].cpu || view.seats[i].key.empty()) {
            continue;
        }
        if (profile.players[p].key != view.seats[i].key) {
            profile.players[p].key = view.seats[i].key;
            changed = true;
        }
    }
    return changed;
}

bool save_profile(const std::filesystem::path &path, const Profile &profile)
{
    nlohmann::json players = nlohmann::json::array();
    for (const auto &p : profile.players) {
        players.push_back({ { "name", p.name }, { "key", p.key } });
    }
    const nlohmann::json j = {
        { "players", players },
        { "local", profile.local },
        { "relay", profile.relay },
        { "relay_url", profile.relayUrl },
        { "last_code", profile.lastCode },
    };
    std::error_code ec;
    if (path.has_parent_path()) {
        std::filesystem::create_directories(path.parent_path(), ec);
    }
    std::ofstream out(path, std::ios::trunc);
    if (!out) {
        return false;
    }
    out << j.dump(2) << '\n';
    return static_cast<bool>(out);
}

std::string setup_json(const Profile &profile, std::string_view build, std::string_view disc, bool host,
    std::string_view resume)
{
    nlohmann::json players = nlohmann::json::array();
    for (int i = 0; i < std::clamp(profile.local, 1, kSeats); ++i) {
        players.push_back({ { "name", profile.players[i].name }, { "key", profile.players[i].key } });
    }
    nlohmann::json j = {
        { "build", std::string(build) },
        { "disc", std::string(disc) },
        { "players", players },
        { "relay", profile.relay },
    };
    if (profile.relay == "own") {
        j["relay_url"] = profile.relayUrl;
    }
    if (host) {
        j["short"] = true;
        if (!resume.empty()) {
            j["resume"] = std::string(resume);
        }
    }
    return j.dump();
}

bool parse_view(std::string_view json, View &view)
{
    const auto j = nlohmann::json::parse(json.begin(), json.end(), nullptr, false);
    if (!j.is_object() || !j.contains("state")) {
        return false;
    }
    View v;
    v.state = parse_state(str(j, "state"));
    v.host = flag(j, "host", false);
    v.me = str(j, "me");
    v.code = str(j, "code");
    v.shortCode = str(j, "short");
    v.error = str(j, "error");
    v.answer = str(j, "answer");
    v.webrtc = flag(j, "webrtc", false);
    v.gen = static_cast<uint32_t>(num(j, "gen", 0));
    if (auto it = j.find("seats"); it != j.end() && it->is_array()) {
        for (const auto &s : *it) {
            if (s.is_number_integer() && s.get<int>() >= 0 && s.get<int>() < kSeats) {
                v.mySeats.push_back(s.get<int>());
            }
        }
    }
    if (auto it = j.find("room"); it != j.end() && it->is_object()) {
        const auto &room = *it;
        v.hasRoom = true;
        v.id = str(room, "id");
        v.board = std::clamp(num(room, "board", 0), 0, kBoards - 1);
        v.turns = std::clamp(num(room, "turns", 20), kTurnsMin, kTurnsMax);
        v.bonus = flag(room, "bonus", true);
        v.teams = flag(room, "teams", false);
        v.started = flag(room, "started", false);
        if (auto cp = room.find("checkpoint"); cp != room.end() && cp->is_object()) {
            v.resume = true;
            v.resumeTurn = num(*cp, "turn", 0);
            v.resumeMaxTurn = num(*cp, "max_turn", 0);
        }
        if (auto seats = room.find("seats"); seats != room.end() && seats->is_array()) {
            for (int i = 0; i < kSeats && i < static_cast<int>(seats->size()); ++i) {
                const auto &s = (*seats)[i];
                if (!s.is_object()) {
                    continue;
                }
                Seat &seat = v.seats[i];
                seat.open = false;
                seat.peer = str(s, "peer");
                seat.name = str(s, "name");
                seat.key = str(s, "key");
                seat.character = std::clamp(num(s, "character", 0), 0, kCharacters - 1);
                seat.cpu = flag(s, "cpu", false);
                seat.difficulty = std::clamp(num(s, "difficulty", 1), 0, kDifficulties - 1);
                seat.team = std::clamp(num(s, "team", 0), 0, 1);
            }
        }
        if (auto h = room.find("handicaps"); h != room.end() && h->is_array()) {
            for (int i = 0; i < kSeats && i < static_cast<int>(h->size()); ++i) {
                if ((*h)[i].is_number_integer()) {
                    v.handicaps[i] = static_cast<uint8_t>(std::clamp((*h)[i].get<int>(), 0, kHandicapMax));
                }
            }
        }
        if (auto r = room.find("ready"); r != room.end() && r->is_array()) {
            for (const auto &p : *r) {
                if (p.is_string()) {
                    v.ready.push_back(p.get<std::string>());
                }
            }
        }
    }
    view = std::move(v);
    return true;
}

bool peer_ready(const View &view, const std::string &peer)
{
    return peer == "host" || peer == "cpu" || std::find(view.ready.begin(), view.ready.end(), peer) != view.ready.end();
}

std::vector<std::string> not_ready(const View &view)
{
    std::vector<std::string> names;
    std::vector<std::string> peers;
    for (const auto &seat : view.seats) {
        if (seat.open || seat.cpu || peer_ready(view, seat.peer)
            || std::find(peers.begin(), peers.end(), seat.peer) != peers.end()) {
            continue;
        }
        peers.push_back(seat.peer);
        names.push_back(seat.name);
    }
    return names;
}

bool character_taken(const View &view, int c, int except)
{
    for (int i = 0; i < kSeats; ++i) {
        if (i != except && !view.seats[i].open && view.seats[i].character == c) {
            return true;
        }
    }
    return false;
}

bool parse_saves(std::string_view json, std::vector<SaveInfo> &saves)
{
    const auto j = nlohmann::json::parse(json.begin(), json.end(), nullptr, false);
    if (!j.is_array()) {
        return false;
    }
    std::vector<SaveInfo> out;
    for (const auto &e : j) {
        if (!e.is_object() || str(e, "path").empty()) {
            continue;
        }
        SaveInfo save;
        save.id = str(e, "id");
        save.path = str(e, "path");
        if (auto t = e.find("time"); t != e.end() && t->is_number_unsigned()) {
            save.time = static_cast<int64_t>(std::min<uint64_t>(t->get<uint64_t>(), INT64_MAX));
        }
        save.board = std::clamp(num(e, "board", 0), 0, kBoards - 1);
        save.turn = num(e, "turn", 0);
        save.maxTurn = num(e, "max_turn", 0);
        if (auto seats = e.find("seats"); seats != e.end() && seats->is_array()) {
            for (const auto &s : *seats) {
                if (!s.is_object() || static_cast<int>(save.seats.size()) >= kSeats) {
                    continue;
                }
                save.seats.push_back({
                    .name = str(s, "name"),
                    .character = std::clamp(num(s, "character", 0), 0, kCharacters - 1),
                    .cpu = flag(s, "cpu", false),
                });
            }
        }
        out.push_back(std::move(save));
    }
    saves = std::move(out);
    return true;
}

bool load_save_keys(SaveInfo &save)
{
    // pbnet's paths are UTF-8
    std::ifstream in(std::filesystem::path(std::u8string(save.path.begin(), save.path.end())));
    if (!in) {
        return false;
    }
    const auto j = nlohmann::json::parse(in, nullptr, false);
    const auto keys = j.is_object() ? j.find("keys") : j.end();
    if (!j.is_object() || keys == j.end() || !keys->is_array()) {
        return false;
    }
    for (size_t i = 0; i < save.seats.size() && i < keys->size(); ++i) {
        save.seats[i].key = (*keys)[i].is_string() ? (*keys)[i].get<std::string>() : std::string {};
    }
    return true;
}

std::vector<int> own_seats(const SaveInfo &save, const Profile &profile)
{
    std::vector<int> seats;
    for (int i = 0; i < static_cast<int>(save.seats.size()); ++i) {
        const std::string &key = save.seats[i].key;
        for (int p = 0; p < std::clamp(profile.local, 1, kSeats) && !key.empty(); ++p) {
            if (profile.players[p].key == key) {
                seats.push_back(i);
                break;
            }
        }
    }
    return seats;
}

bool parse_lan(std::string_view json, LanList &list)
{
    const auto j = nlohmann::json::parse(json.begin(), json.end(), nullptr, false);
    if (!j.is_object() || !j.contains("state")) {
        return false;
    }
    LanList l;
    l.state = str(j, "state");
    l.lan = flag(j, "lan", false);
    l.error = str(j, "error");
    if (auto it = j.find("games"); it != j.end() && it->is_array()) {
        for (const auto &g : *it) {
            if (!g.is_object()) {
                continue;
            }
            LanGame game {
                .code = str(g, "code"),
                .id = str(g, "id"),
                .name = str(g, "name"),
                .open = std::clamp(num(g, "open", 0), 0, kSeats),
            };
            if (!game.code.empty()) {
                l.games.push_back(std::move(game));
            }
        }
    }
    list = std::move(l);
    return true;
}

std::string lan_key(const LanList &list)
{
    std::string key;
    for (const auto &g : list.games) {
        key += g.code + '\n' + g.id + '\n' + g.name + '\n' + std::to_string(g.open) + '\n';
    }
    return key;
}

} // namespace partyboard::online
