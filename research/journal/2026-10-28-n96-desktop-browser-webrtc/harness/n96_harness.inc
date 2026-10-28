// N96 scratch (not committed into the build): a desktop host for browsers, driven through files in $PB_N96 (a dir),
// as a player would: Host for Browsers (setup "webrtc": true), its code to code.txt, the browser guest's answer from
// answer.txt (Paste Answer), CPUs in seats 2 and 3 and Start Match once start.txt exists. #included into
// src/port/ui/online.cpp and called at the top of update_online_match().
namespace {
void n96_harness()
{
    static int phase = 0;
    static std::string code;
    static auto t0 = std::chrono::steady_clock::now();
    const char *dir = getenv("PB_N96");
    if (dir == nullptr) {
        return;
    }
    const std::string d = dir;
    const auto readf = [&](const char *n) {
        std::ifstream f(d + "/" + n);
        return f ? trim(std::string((std::istreambuf_iterator<char>(f)), {})) : std::string();
    };
    const auto writef = [&](const char *n, const std::string &s) {
        const std::string tmp = d + "/" + n + ".tmp";
        std::ofstream(tmp) << s << "\n";
        std::rename(tmp.c_str(), (d + "/" + n).c_str());
    };
    // PB_N96_GUEST: the browser hosts; this desktop joins with its code (code.txt), its answer to answer.txt, ready
    // once the room has its CPUs
    if (getenv("PB_N96_GUEST") != nullptr) {
        if (phase == 0) {
            const std::string c = readf("code.txt");
            if (std::chrono::steady_clock::now() - t0 < std::chrono::seconds(20) || c.empty()) {
                return;
            }
            const std::string setup = R"({"build":"web-pair","disc":"GMPE01-01","relay":"off",)"
                                      R"("players":[{"name":"Desk","key":"key-desk"}]})";
            std::fprintf(stderr, "n96: join %d\n", pbnet_lobby_join(setup.c_str(), c.c_str()));
            phase = 1;
            return;
        }
        const auto j = nlohmann::json::parse(pbnet_text(pbnet_lobby_json), nullptr, false);
        if (!j.is_object()) {
            return;
        }
        if (const std::string a = j.value("answer", ""); phase == 1 && !a.empty()) {
            writef("answer.txt", a);
            std::fprintf(stderr, "n96: answer %zu chars, webrtc %d: %s\n", a.size(), (int)j.value("webrtc", false), a.c_str());
            phase = 2;
        }
        int seated = 0;
        if (j.contains("room") && j["room"].is_object() && j["room"]["seats"].is_array()) {
            for (const auto &s : j["room"]["seats"]) {
                seated += !s.is_null();
            }
        }
        if (phase == 2 && seated == 4) {
            std::fprintf(stderr, "n96: seated, ready %d\n", pbnet_lobby_ready(1));
            phase = 3;
        }
        if (const std::string e = j.value("error", ""); !e.empty()) {
            static std::string last;
            if (e != last) {
                std::fprintf(stderr, "n96: lobby error: %s\n", e.c_str());
                last = e;
            }
        }
        return;
    }
    if (phase == 0) {
        if (std::chrono::steady_clock::now() - t0 < std::chrono::seconds(20)) {
            return;
        }
        const std::string setup = R"({"build":"web-pair","disc":"GMPE01-01","relay":"off","short":true,"webrtc":true,)"
                                  R"("players":[{"name":"Desk","key":"key-desk"}]})";
        const int ok = pbnet_lobby_host(setup.c_str());
        std::fprintf(stderr, "n96: host for browsers: %d\n", ok);
        phase = 1;
        return;
    }
    const auto j = nlohmann::json::parse(pbnet_text(pbnet_lobby_json), nullptr, false);
    if (!j.is_object()) {
        return;
    }
    if (const std::string s = j.value("short", ""); !s.empty() && s != code) {
        code = s;
        writef("code.txt", code);
        std::fprintf(stderr, "n96: code %zu chars, webrtc %d: %s\n", code.size(), (int)j.value("webrtc", false), code.c_str());
    }
    if (const std::string e = j.value("error", ""); !e.empty()) {
        static std::string last;
        if (e != last) {
            std::fprintf(stderr, "n96: lobby error: %s\n", e.c_str());
            last = e;
        }
    }
    if (const std::string a = readf("answer.txt"); !a.empty()) {
        char why[256] = "";
        const int ok = pbnet_lobby_answer(a.c_str(), why, sizeof(why));
        std::fprintf(stderr, "n96: answer taken %d %s\n", ok, why);
        std::rename((d + "/answer.txt").c_str(), (d + "/answer.done").c_str());
    }
    // the CPUs once the guest is seated (before it readies: a new line-up takes ready back)
    if (phase == 1 && j.contains("room") && j["room"].is_object() && j["room"].contains("seats")) {
        const auto &seats = j["room"]["seats"];
        if (seats.is_array() && seats.size() == 4 && !seats[1].is_null() && seats[2].is_null()) {
            pbnet_lobby_cpu(2, 1);
            pbnet_lobby_cpu(3, 1);
        }
    }
    if (phase == 1 && !readf("start.txt").empty()) {
        char why[256] = "";
        if (pbnet_lobby_start(std::strtoull(readf("start.txt").c_str(), nullptr, 0), why, sizeof(why))) {
            std::fprintf(stderr, "n96: started\n");
            writef("started.txt", "1");
            phase = 2;
        }
        else {
            static std::string last;
            if (last != why) {
                std::fprintf(stderr, "n96: start: %s\n", why);
                last = why;
            }
        }
    }
}
} // namespace
