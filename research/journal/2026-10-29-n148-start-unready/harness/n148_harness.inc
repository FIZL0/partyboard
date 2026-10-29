// N148 scratch (not committed into the build), from N96's n96_harness.inc: two desktop copies (iroh, PB_N148_GUEST
// on the joiner) or a desktop host for one browser tab (PB_N148_RTC), driven through files in $PB_N148 (a dir). The
// host opens the real LobbyWindow, gives seats 2 and 3 to the computer once the guest is seated, then (start.txt)
// presses Start Match and the confirm modal's Start. The guest never readies; a desktop guest opens Leave Lobby's
// confirm modal before the start, which must close with the lobby. #included into src/port/ui/online.cpp and called
// at the top of update_online_match().
namespace {
Rml::Element *n148_button(Rml::Element *e, const std::string &text)
{
    if (e == nullptr || !e->IsVisible()) {
        return nullptr;
    }
    if (e->GetTagName() == "button") {
        std::string t;
        bool tag = false;
        for (char c : e->GetInnerRML()) {
            if (c == '<') tag = true;
            else if (c == '>') tag = false;
            else if (!tag && c != '\n' && c != ' ') t += c;
        }
        if (t == text) {
            return e;
        }
    }
    for (int i = 0; i < e->GetNumChildren(); ++i) {
        if (auto *r = n148_button(e->GetChild(i), text)) {
            return r;
        }
    }
    return nullptr;
}

// Clicks the visible button whose text (spaces dropped) is `text`, topmost document first. Whether one was found.
bool n148_click(const std::string &text)
{
    auto *ctx = aurora::rmlui::get_context();
    for (int i = ctx ? ctx->GetNumDocuments() - 1 : -1; i >= 0; --i) {
        if (auto *doc = ctx->GetDocument(i); doc->IsVisible()) {
            if (auto *b = n148_button(doc, text)) {
                b->Click();
                std::fprintf(stderr, "n148: clicked %s in rml doc %d\n", text.c_str(), i);
                return true;
            }
        }
    }
    return false;
}

void n148_stack(const char *when)
{
    std::string s;
    for (auto &d : get_document_stack()) {
        s += std::string(typeid(*d).name()) + (d->closed() ? "(closed)" : d->pending_close() ? "(closing)" : d->visible() ? "(visible)" : "(hidden)") + " ";
    }
    std::fprintf(stderr, "n148: stack %s: %s\n", when, s.c_str());
}

void n148_harness()
{
    static int phase = 0;
    static std::string code;
    static auto t0 = std::chrono::steady_clock::now();
    static auto tp = t0;
    const char *dir = getenv("PB_N148");
    if (dir == nullptr) {
        return;
    }
    const std::string d = dir;
    const bool rtc = getenv("PB_N148_RTC") != nullptr;
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
    if (getenv("PB_N148_GUEST") != nullptr) {
        if (phase == 0) {
            const std::string c = readf("code.txt");
            if (now - t0 < std::chrono::seconds(20) || c.empty()) {
                return;
            }
            const std::string setup = R"({"build":"n148-pair","disc":"GMPE01-01","relay":"off",)"
                                      R"("players":[{"name":"Bob","key":"key-bob"}]})";
            std::fprintf(stderr, "n148: join %d\n", pbnet_lobby_join(setup.c_str(), c.c_str()));
            push_document(std::make_unique<LobbyWindow>());
            phase = 1;
            return;
        }
        const auto j = nlohmann::json::parse(pbnet_text(pbnet_lobby_json), nullptr, false);
        if (!j.is_object()) {
            return;
        }
        int seated = 0;
        if (j.contains("room") && j["room"].is_object() && j["room"]["seats"].is_array()) {
            for (const auto &s : j["room"]["seats"]) {
                seated += !s.is_null();
            }
        }
        if (phase == 1 && seated == 4) {
            // a pick it then shows (Wario), never Ready; Leave Lobby's confirm open when the start comes
            const uint8_t ch = 4, team = 0;
            std::fprintf(stderr, "n148: guest seated, picks %d\n", pbnet_lobby_picks(&ch, &team, 1));
            phase = 2;
            tp = now;
        }
        if (phase == 2 && now - tp > std::chrono::seconds(2)) {
            std::fprintf(stderr, "n148: leave modal %d\n", (int)n148_click("LeaveLobby"));
            n148_stack("guest before start");
            writef("guestwait.txt", "1");
            phase = 3;
        }
        if (phase == 3 && pbnet_lobby_state() == 3) {
            std::fprintf(stderr, "n148: guest started: %s\n", pbnet_text(pbnet_lobby_json).c_str());
            phase = 4;
            tp = now;
        }
        if (phase == 4 && now - tp > std::chrono::seconds(2)) {
            n148_stack("guest after start");
            phase = 5;
        }
        return;
    }
    if (phase == 0) {
        if (now - t0 < std::chrono::seconds(20)) {
            return;
        }
        const std::string setup = rtc ? R"({"build":"web-pair","disc":"GMPE01-01","relay":"off","short":true,"webrtc":true,)"
                                        R"("players":[{"name":"Desk","key":"key-desk"}]})"
                                      : R"({"build":"n148-pair","disc":"GMPE01-01","relay":"off",)"
                                        R"("players":[{"name":"Hosty","key":"key-host"}]})";
        const int ok = pbnet_lobby_host(setup.c_str());
        std::fprintf(stderr, "n148: host %d (rtc %d)\n", ok, (int)rtc);
        push_document(std::make_unique<LobbyWindow>());
        phase = 1;
        return;
    }
    const auto j = nlohmann::json::parse(pbnet_text(pbnet_lobby_json), nullptr, false);
    if (!j.is_object()) {
        return;
    }
    if (const std::string s = j.value(rtc ? "short" : "code", ""); !s.empty() && s != code) {
        code = s;
        writef("code.txt", code);
        std::fprintf(stderr, "n148: code %zu chars\n", code.size());
    }
    if (const std::string e = j.value("error", ""); !e.empty()) {
        static std::string last;
        if (e != last) {
            std::fprintf(stderr, "n148: lobby error: %s\n", e.c_str());
            last = e;
        }
    }
    if (const std::string a = readf("answer.txt"); !a.empty()) {
        char why[256] = "";
        const int ok = pbnet_lobby_answer(a.c_str(), why, sizeof(why));
        std::fprintf(stderr, "n148: answer taken %d %s\n", ok, why);
        std::rename((d + "/answer.txt").c_str(), (d + "/answer.done").c_str());
    }
    if (phase == 1 && j.contains("room") && j["room"].is_object() && j["room"].contains("seats")) {
        const auto &seats = j["room"]["seats"];
        if (seats.is_array() && seats.size() == 4 && !seats[1].is_null() && seats[2].is_null()) {
            pbnet_lobby_cpu(2, 1);
            pbnet_lobby_cpu(3, 1);
        }
    }
    if (phase == 1 && !readf("start.txt").empty()) {
        std::fprintf(stderr, "n148: host before start: %s\n", pbnet_text(pbnet_lobby_json).c_str());
        std::fprintf(stderr, "n148: start match %d\n", (int)n148_click("StartMatch"));
        phase = 2;
        tp = now;
    }
    if (phase == 2 && now - tp > std::chrono::milliseconds(800)) {
        n148_stack("host confirm");
        auto *ctx = aurora::rmlui::get_context();
        for (int i = 0; ctx && i < ctx->GetNumDocuments(); ++i) {
            if (ctx->GetDocument(i)->IsVisible()) {
                std::string t = ctx->GetDocument(i)->GetInnerRML();
                if (const auto at = t.find("Start without"); at != std::string::npos) {
                    std::fprintf(stderr, "n148: modal says '%s'\n", t.substr(at, 60).c_str());
                }
            }
        }
        std::fprintf(stderr, "n148: confirm start %d\n", (int)n148_click("Start"));
        phase = 3;
    }
    if (phase == 3 && pbnet_lobby_state() == 3) {
        std::fprintf(stderr, "n148: host started: %s\n", pbnet_text(pbnet_lobby_json).c_str());
        writef("started.txt", "1");
        phase = 4;
    }
}
} // namespace
