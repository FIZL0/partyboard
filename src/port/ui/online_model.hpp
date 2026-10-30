#pragma once

// Data side of the online menu (src/port/ui/online.cpp), without RmlUi: this device's players, the
// pbnet_lobby_host/join setup JSON, the pbnet_lobby_json snapshot, and the online saves on disk.

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace partyboard::online {

constexpr int kSeats = 4;
constexpr int kCharacters = 8;
constexpr int kBoards = 6;
constexpr int kDifficulties = 4;
constexpr int kTurnsMin = 10;
constexpr int kTurnsMax = 50;
constexpr int kHandicapMax = 9;

// pbnet_lobby_state()
enum State {
    None = 0,
    Connecting = 1,
    Open = 2,
    Started = 3,
    Closed = 4,
};

// GWPlayerCfg.character order (net/src/lobby.rs CHARACTERS).
extern const char *const kCharacterNames[kCharacters];
// GWSystem.board order.
extern const char *const kBoardNames[kBoards];
extern const char *const kDifficultyNames[kDifficulties];

struct Player {
    std::string name;
    // Stable id of this player on this device: online saves give the seat back by it.
    std::string key;
};

// <config>/online.json
struct Profile {
    Player players[kSeats];
    // Players on this device (1..4); they take seats in order.
    int local = 1;
    // "n0" (public relays), "own" (relayUrl) or "off" (LAN and direct only).
    std::string relay = "n0";
    std::string relayUrl;
    std::string lastCode;
};

// The profile file this running copy of the game uses in config dir `dir`: online.json, or when another running
// copy holds it (two copies on one PC, N89), online-2.json, online-3.json, ... Each is claimed by a lock file
// (online.lock, online-2.lock, ...) held until the process ends. Call once per process.
std::filesystem::path claim_profile(const std::filesystem::path &dir);

// Missing or broken file: defaults. Every player gets a name and a fresh random key if it has none.
Profile load_profile(const std::filesystem::path &path);
bool save_profile(const std::filesystem::path &path, const Profile &profile);

// Setup JSON for pbnet_lobby_host (host = true) and pbnet_lobby_join. resume: the online save a host continues
// (a SaveInfo::path; empty: a new match).
std::string setup_json(const Profile &profile, std::string_view build, std::string_view disc, bool host,
    std::string_view resume = {});

struct Seat {
    bool open = true;
    std::string peer;
    std::string name;
    std::string key;
    int character = 0;
    bool cpu = false;
    int difficulty = 1;
    int team = 0;
};

// pbnet_lobby_json, parsed.
struct View {
    int state = None;
    bool host = false;
    std::string me;
    std::string code;
    std::string shortCode;
    std::string error;
    // N92, a browser guest while it connects: its answer for the host (empty otherwise).
    std::string answer;
    // N96: a WebRTC lobby (always in the browser; on the desktop hosted for browsers or joined with a browser's code):
    // its code is an offer and a joining guest's answer goes back to the host.
    bool webrtc = false;
    // N149: joined a match already running; it takes a seat the computer stands in for at the next resync point.
    bool joining = false;
    // This device's seat indices, in the order of Profile::players.
    std::vector<int> mySeats;
    bool hasRoom = false;
    std::string id;
    Seat seats[kSeats];
    int board = 0;
    int turns = 20;
    bool bonus = true;
    bool teams = false;
    uint8_t handicaps[kSeats] = {};
    std::vector<std::string> ready;
    bool started = false;
    // A continued online save (the room has a checkpoint): seats, characters and rules are the save's.
    bool resume = false;
    int resumeTurn = 0;
    int resumeMaxTurn = 0;
    uint32_t gen = 0;
};

// False (and view untouched) if json is not a lobby snapshot.
bool parse_view(std::string_view json, View &view);

// A guest's seats keys as the host gave them (a fresh one when its Hello's key was a player's already here, N89)
// into profile's players, in seat order. Whether any changed. Nothing for the host.
bool adopt_keys(Profile &profile, const View &view);

// The peer has nothing left to confirm: the host, a CPU, or a guest in the ready list.
bool peer_ready(const View &view, const std::string &peer);
// Names of the seated players whose peers are not ready, in seat order (no repeats).
std::vector<std::string> not_ready(const View &view);
// Whether a seat other than `except` has character c.
bool character_taken(const View &view, int c, int except);

// A seat of an online save.
struct SavedSeat {
    std::string name;
    int character = 0;
    bool cpu = false;
    // The player key that gets the seat back on a continue ("" for a seat the computer played).
    std::string key;
};

// An online save (<config>/online/<match id>.json, written at board checkpoints), as pbnet_save_list lists it.
struct SaveInfo {
    std::string id;
    std::string path;
    // When it was written (Unix seconds).
    int64_t time = 0;
    int board = 0;
    int turn = 0;
    int maxTurn = 0;
    std::vector<SavedSeat> seats;
};

// pbnet_save_list's JSON array, parsed (newest first, as listed). False (and saves untouched) if it is not an
// array; entries without a path are left out.
bool parse_saves(std::string_view json, std::vector<SaveInfo> &saves);
// Fills each seat's key from the save file's "keys" (pbnet_save_list doesn't list them). False if the file can't
// be read.
bool load_save_keys(SaveInfo &save);
// The seats of save whose keys are this device's players' (Profile::local of them), in seat order: the seats they
// get back when they continue it.
std::vector<int> own_seats(const SaveInfo &save, const Profile &profile);

// A lobby heard on the LAN (pbnet_lan_browse).
struct LanGame {
    // The host's endpoint ID: pass to pbnet_lobby_join.
    std::string code;
    std::string id;
    std::string name;
    int open = 0;
};

// pbnet_lan_browse, parsed.
struct LanList {
    // "binding", "open" or "closed" ("" before the first answer).
    std::string state;
    bool lan = false;
    std::string error;
    std::vector<LanGame> games;
};

// False (and list untouched) if json is not a LAN list. Games without a code are left out.
bool parse_lan(std::string_view json, LanList &list);
// Changes when a game comes, goes or changes its name or open seats.
std::string lan_key(const LanList &list);

} // namespace partyboard::online
