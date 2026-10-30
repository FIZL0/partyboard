// N149 scratch (not committed into the build), from N148's n148_harness.inc. Driven through files in $PB_N149 (a
// dir); PB_N149_MODE picks the copy's part:
// - make: hosts a new match for two local players (Hosty, Pal; seats 3 and 4 the computer's) and starts it at once,
//   so a per-turn online save with a player seat to stand in for gets written.
// - host: hosts the continue of $PB_N149_SAVE with Hosty alone and opens the real LobbyWindow. Without PB_N149_MID
//   it waits for start.txt, then clicks Start Match (and a confirm dialog's Start); with it, it starts at once.
// - guest: browses the LAN (pbnet_lan_browse, as the LAN tab), joins the first lobby listed with a free seat as Carl
//   (pbnet_lobby_join with its code, as join_lan) and opens LobbyWindow. In a continue lobby it takes seat 4 through
//   the seat's "Take This Seat" button (focus 4P, click), pbnet_lobby_take if that button can't be found, gets ready
//   and writes guestwait.txt. With PB_N149_MID it joins the running match and logs the joining/started states.
// #included into src/port/ui/online.cpp and called at the top of update_online_match().
namespace {
Rml::Element *n149_find(Rml::Element *e, const std::function<bool(const std::string &)> &match)
{
    if (e == nullptr || !e->IsVisible()) {
        return nullptr;
    }
    if (e->GetTagName() == "button" || e->GetTagName() == "select-button") {
        std::string t;
        bool tag = false;
        for (char c : e->GetInnerRML()) {
            if (c == '<') tag = true;
            else if (c == '>') tag = false;
            else if (!tag && c != '\n' && c != ' ') t += c;
        }
        if (match(t)) {
            return e;
        }
    }
    for (int i = 0; i < e->GetNumChildren(); ++i) {
        if (auto *r = n149_find(e->GetChild(i), match)) {
            return r;
        }
    }
    return nullptr;
}

Rml::Element *n149_button(const std::function<bool(const std::string &)> &match)
{
    auto *ctx = aurora::rmlui::get_context();
    for (int i = ctx ? ctx->GetNumDocuments() - 1 : -1; i >= 0; --i) {
        if (auto *doc = ctx->GetDocument(i); doc->IsVisible()) {
            if (auto *b = n149_find(doc, match)) {
                return b;
            }
        }
    }
    return nullptr;
}

bool n149_click(const std::string &text)
{
    if (auto *b = n149_button([&](const std::string &t) { return t == text; })) {
        b->Click();
        std::fprintf(stderr, "n149: clicked %s\n", text.c_str());
        return true;
    }
    return false;
}

std::vector<int> n149_seats(const nlohmann::json &j)
{
    std::vector<int> v;
    if (j.contains("seats") && j["seats"].is_array()) {
        for (const auto &s : j["seats"]) {
            v.push_back(s.get<int>());
        }
    }
    return v;
}

void n149_harness()
{
    static int phase = 0;
    static std::string code, lastJson;
    static auto t0 = std::chrono::steady_clock::now();
    static auto tp = t0;
    const char *dir = getenv("PB_N149");
    const char *mode = getenv("PB_N149_MODE");
    if (dir == nullptr || mode == nullptr) {
        return;
    }
    const std::string d = dir, m = mode;
    const bool mid = getenv("PB_N149_MID") != nullptr;
    const auto readf = [&](const char *n) {
        std::ifstream f(d + "/" + n);
        return f ? trim(std::string((std::istreambuf_iterator<char>(f)), {})) : std::string();
    };
    const auto writef = [&](const char *n, const std::string &s) {
        const std::string tmp = d + "/" + n + ".tmp";
        std::ofstream(tmp) << s << "\n";
        std::rename(tmp.c_str(), (d + "/" + n).c_str());
    };
    const auto now = std::chrono::steady_clock::now();
    if (phase == 0 && now - t0 < std::chrono::seconds(m == "guest" ? 25 : 20)) {
        return;
    }
    const std::string js = pbnet_text(pbnet_lobby_json);
    const auto j = nlohmann::json::parse(js, nullptr, false);
    // every change of state, seats, joining or the room's seats, logged once
    if (j.is_object()) {
        std::string key = j.value("state", std::string()) + (j.value("joining", false) ? "J" : "");
        if (j.contains("seats")) key += j["seats"].dump();
        if (j.contains("room") && j["room"].is_object()) key += j["room"]["seats"].dump() + j["room"]["ver"].dump();
        if (key != lastJson) {
            lastJson = key;
            std::fprintf(stderr, "n149: %s lobby: %s\n", m.c_str(), js.c_str());
        }
    }
    if (m == "make") {
        if (phase == 0) {
            const std::string setup = R"({"build":"n149-pair","disc":"GMPE01-01","relay":"off",)"
                                      R"("players":[{"name":"Hosty","key":"key-host"},{"name":"Pal","key":"key-pal"}]})";
            std::fprintf(stderr, "n149: make host %d\n", pbnet_lobby_host(setup.c_str()));
            phase = 1;
            tp = now;
        }
        if (phase == 1 && pbnet_lobby_state() == 2 && now - tp > std::chrono::seconds(2)) {
            char why[256] = "";
            std::fprintf(stderr, "n149: make start %d %s\n", pbnet_lobby_start(1490, why, sizeof(why)), why);
            phase = 2;
        }
        return;
    }
    if (m == "host") {
        if (phase == 0) {
            const char *save = getenv("PB_N149_SAVE");
            nlohmann::json setup = {{"build", "n149-pair"}, {"disc", "GMPE01-01"}, {"relay", "off"},
                {"players", nlohmann::json::array({{{"name", "Hosty"}, {"key", "key-host"}}})},
                {"resume", save ? save : ""}};
            std::fprintf(stderr, "n149: host resume %d\n", pbnet_lobby_host(setup.dump().c_str()));
            push_document(std::make_unique<LobbyWindow>());
            phase = 1;
            tp = now;
            return;
        }
        if (phase == 1 && mid && pbnet_lobby_state() == 2 && now - tp > std::chrono::seconds(3)) {
            std::fprintf(stderr, "n149: start match %d\n", (int)n149_click("StartMatch"));
            phase = 2;
            tp = now;
        }
        if (phase == 1 && !mid && !readf("start.txt").empty()) {
            std::fprintf(stderr, "n149: start match %d\n", (int)n149_click("StartMatch"));
            phase = 2;
            tp = now;
        }
        if (phase == 2 && now - tp > std::chrono::milliseconds(800)) {
            if (pbnet_lobby_state() != 3) {
                std::fprintf(stderr, "n149: confirm start %d\n", (int)n149_click("Start"));
            }
            phase = 3;
        }
        if (phase == 3 && pbnet_lobby_state() == 3) {
            writef("started.txt", "1");
            phase = 4;
        }
        return;
    }
    // guest
    static std::string setup = R"({"build":"n149-pair","disc":"GMPE01-01","relay":"off",)"
                               R"("players":[{"name":"Carl","key":"key-carl"}]})";
    if (phase == 0) {
        static auto lastPoll = t0;
        if (mid && readf("started.txt").empty()) {
            return;
        }
        if (now - lastPoll < std::chrono::milliseconds(500)) {
            return;
        }
        lastPoll = now;
        const int len = pbnet_lan_browse(setup.c_str(), nullptr, 0);
        if (len <= 0) {
            return;
        }
        std::string buf(static_cast<size_t>(len) + 1, '\0');
        pbnet_lan_browse(setup.c_str(), buf.data(), static_cast<int>(buf.size()));
        const auto lan = nlohmann::json::parse(buf.c_str(), nullptr, false);
        if (!lan.is_object() || !lan.contains("games")) {
            return;
        }
        for (const auto &g : lan["games"]) {
            if (g.value("open", 0) > 0) {
                std::fprintf(stderr, "n149: LAN lists %s\n", g.dump().substr(0, 400).c_str());
                const std::string c = g.value("code", "");
                std::fprintf(stderr, "n149: join over LAN %d\n", pbnet_lobby_join(setup.c_str(), c.c_str()));
                push_document(std::make_unique<LobbyWindow>());
                phase = 1;
                tp = now;
                return;
            }
        }
        return;
    }
    if (!j.is_object()) {
        return;
    }
    const auto seats = n149_seats(j);
    if (mid) {
        if (phase == 1 && j.value("joining", false)) {
            std::fprintf(stderr, "n149: guest waits for the next point\n");
            phase = 2;
        }
        if (phase <= 2 && pbnet_lobby_state() == 3) {
            std::fprintf(stderr, "n149: guest in the match, seats %s\n", j["seats"].dump().c_str());
            phase = 3;
        }
        return;
    }
    if (phase == 1 && pbnet_lobby_state() == 2 && seats.size() == 1 && now - tp > std::chrono::seconds(2)) {
        std::fprintf(stderr, "n149: guest seated in %d\n", seats[0] + 1);
        if (seats[0] == 3) {
            phase = 4;
        } else if (auto *b = n149_button([](const std::string &t) { return t.rfind("4P", 0) == 0; })) {
            b->Focus();
            std::fprintf(stderr, "n149: focused 4P: %s\n", b->GetInnerRML().substr(0, 200).c_str());
            phase = 2;
        } else {
            phase = 3;
        }
        tp = now;
    }
    if (phase == 2 && now - tp > std::chrono::milliseconds(600)) {
        phase = n149_click("TakeThisSeat") ? 4 : 3;
        tp = now;
    }
    if (phase == 3) {
        std::fprintf(stderr, "n149: no Take This Seat button: pbnet_lobby_take %d\n", pbnet_lobby_take(seats.empty() ? 0 : seats[0], 3));
        phase = 4;
        tp = now;
    }
    if (phase == 4 && seats.size() == 1 && seats[0] == 3 && now - tp > std::chrono::seconds(1)) {
        std::fprintf(stderr, "n149: guest in seat 4, ready %d\n", pbnet_lobby_ready(1));
        writef("guestwait.txt", "1");
        phase = 5;
    }
}
} // namespace
