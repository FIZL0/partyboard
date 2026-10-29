// N144: copy of N139a's; with PB_N96_GUEST the desktop joins the tab's lobby from its LAN list, not code.txt.
// N139: N96's harness with the relay setting from PB_N139_RELAY (default n0): a short code (4 symbols) is written once
// it shows, and a short-code guest has no answer to write.
// N96 scratch (not committed into the build): a desktop host for browsers, driven through files in $PB_N96 (a dir),
// as a player would: Host for Browsers (setup "webrtc": true), its code to code.txt, the browser guest's answer from
// answer.txt (Paste Answer), CPUs in seats 2 and 3 and Start Match once start.txt exists. #included into
// src/port/ui/online.cpp and called at the top of update_online_match().
namespace {
const char *n139_relay()
{
    const char *r = getenv("PB_N139_RELAY");
    return r != nullptr ? r : "n0";
}

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
        // N144: the menu joins with this build's id: the tab hosts with it (else "a different version")
        static bool built = false;
        if (!built) {
            writef("build.txt", build_id());
            built = true;
        }
        if (phase == 0) {
            const std::string c = readf("code.txt");
            if (std::chrono::steady_clock::now() - t0 < std::chrono::seconds(20) || c.empty()) {
                return;
            }
            const std::string setup = std::string(R"({"build":"web-pair","disc":"GMPE01-01","relay":")") + n139_relay() + "\"," +
                                      R"("players":[{"name":"Desk","key":"key-desk"}]})";
            // N144: the LAN tab as a player opens it, by keys: F1, Right, Enter (Online), PageDown twice (LAN), then
            // Down until the focused button is a game's ("N seats open"), a shot, Enter. With PB_N144_ABI: its list
            // through pbnet_lan_browse (poll_lan) and pbnet_lobby_join with the listed code (join_lan) instead.
            static auto seen = std::chrono::steady_clock::now();
            static int polls = 0;
            if (++polls % 30 != 0) {
                return;
            }
            const double after = std::chrono::duration<double>(std::chrono::steady_clock::now() - seen).count();
            if (getenv("PB_N144_ABI") == nullptr) {
                static int step = 0, downs = 0;
                static std::string lastFocus;
                Rml::Context *ctx = Rml::GetContext(0);
                if (ctx == nullptr) {
                    return;
                }
                const auto key = [&](Rml::Input::KeyIdentifier k) {
                    ctx->ProcessKeyDown(k, 0);
                    ctx->ProcessKeyUp(k, 0);
                };
                Rml::Element *f = ctx->GetFocusElement();
                const std::string focus = f != nullptr ? f->GetInnerRML() : std::string();
                if (focus != lastFocus) {
                    std::fprintf(stderr, "n144: %.1f s: step %d, focus [%s]\n", after, step, focus.c_str());
                    lastFocus = focus;
                }
                const Rml::Input::KeyIdentifier open[] = {Rml::Input::KI_F1, Rml::Input::KI_RIGHT, Rml::Input::KI_RETURN, Rml::Input::KI_NEXT, Rml::Input::KI_NEXT};
                if (step < 5) {
                    key(open[step++]);
                    return;
                }
                if (focus.find("seats open") == std::string::npos && focus.find("seat open") == std::string::npos) {
                    if (after > 180) {
                        static bool said = false;
                        if (!said) {
                            std::fprintf(stderr, "n144: %s never in the LAN tab\n", c.c_str());
                            said = true;
                        }
                        return;
                    }
                    // down the list, and back to its top now and then (the list rebuilds as games come and go)
                    if (++downs % 8 == 0) {
                        for (int i = 0; i < 8; i++) {
                            key(Rml::Input::KI_UP);
                        }
                    }
                    else {
                        key(Rml::Input::KI_DOWN);
                    }
                    return;
                }
                if (step == 5) {
                    aurora_capture_frame_ui((d + "/lan.png").c_str(), 2);
                    std::fprintf(stderr, "n144: the game in the LAN tab %.1f s after the code: [%s]; shot\n", after, focus.c_str());
                    step = 6;
                    return;
                }
                if (step < 9) {
                    step++;
                    return;
                }
                key(Rml::Input::KI_RETURN);
                std::fprintf(stderr, "n144: pressed, lobby state %d\n", pbnet_lobby_state());
                writef("listed.txt", std::to_string(after));
                phase = 1;
                return;
            }
            std::string list(pbnet_lan_browse(setup.c_str(), nullptr, 0) + 1, '\0');
            list.resize(std::max(0, pbnet_lan_browse(setup.c_str(), list.data(), (int)list.size())));
            static std::string last;
            if (list != last) {
                std::fprintf(stderr, "n144: %.1f s after the code: LAN list %s\n", after, list.c_str());
                last = list;
            }
            const auto j = nlohmann::json::parse(list, nullptr, false);
            if (!j.is_object() || !j.contains("games") || !j["games"].is_array()) {
                return;
            }
            for (const auto &g : j["games"]) {
                if (g.is_object() && g.value("code", "") == c) {
                    std::fprintf(stderr, "n144: %s listed %.1f s after the code (lan %d): press, join %d\n", c.c_str(), after,
                        (int)j.value("lan", false), pbnet_lobby_join(setup.c_str(), c.c_str()));
                    writef("listed.txt", std::to_string(after));
                    phase = 1;
                    return;
                }
            }
            if (after > 180) {
                static bool said = false;
                if (!said) {
                    std::fprintf(stderr, "n144: %s never listed\n", c.c_str());
                    said = true;
                }
            }
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
        if (phase == 1 && seated >= 2 && j.value("answer", "").empty()) {
            std::fprintf(stderr, "n96: seated by the short code, webrtc %d\n", (int)j.value("webrtc", false));
            phase = 2;
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
        const std::string setup = std::string(R"({"build":"web-pair","disc":"GMPE01-01","relay":")") + n139_relay() +
                                  R"(","short":true,"webrtc":true,"players":[{"name":"Desk","key":"key-desk"}]})";
        const int ok = pbnet_lobby_host(setup.c_str());
        std::fprintf(stderr, "n96: host for browsers: %d\n", ok);
        phase = 1;
        return;
    }
    const auto j = nlohmann::json::parse(pbnet_text(pbnet_lobby_json), nullptr, false);
    if (!j.is_object()) {
        return;
    }
    if (const std::string s = j.value("short", ""); !s.empty() && s != code && (s.size() == 4 || std::string(n139_relay()) == "off")) {
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
