// N42 scratch harness (not for commit), after N38's: drives the Online menu by pressing its real buttons.
// #include'd into src/port/ui/online.cpp after update_online_match's statics; n42_harness() at its top.
// PB_N42=host|guest, PB_N42_DIR=<dir shared by both peers>.
//   host:  Continue tab > the saved board > Host Match, writes the code, Start Match once the guest is ready;
//          after the match is over (lobby Open again) Start Match again once the guest is ready again.
//   guest: Join tab > Join Game (online.json last_code), Ready; after the match is over, Ready again.
// Logs every lobby JSON change, lobby state change and the visible toast/modal texts, with wall-clock ms.

extern "C" unsigned int netplay_frame(void);
namespace {
    long long n42_ms()
    {
        timeval tv;
        gettimeofday(&tv, nullptr);
        return (long long)tv.tv_sec * 1000 + tv.tv_usec / 1000;
    }

    Rml::Element *n42_find(Rml::Element *e, const std::string &text)
    {
        {
            const std::string rml = e->GetInnerRML();
            if (rml == text || (text.back() == '*' && rml.rfind(text.substr(0, text.size() - 1), 0) == 0)) {
                return e;
            }
        }
        for (int i = 0; i < e->GetNumChildren(); ++i) {
            if (auto *f = n42_find(e->GetChild(i), text)) {
                return f;
            }
        }
        return nullptr;
    }

    Rml::Element *n42_find_any(const std::string &text)
    {
        Rml::Context *c = Rml::GetContext(0);
        if (c == nullptr) {
            return nullptr;
        }
        for (int i = c->GetNumDocuments() - 1; i >= 0; --i) {
            Rml::ElementDocument *d = c->GetDocument(i);
            if (!d->IsVisible()) {
                continue;
            }
            if (auto *f = n42_find(d, text)) {
                return f;
            }
        }
        return nullptr;
    }

    // Button listens to Click, SelectButton/BoolButton to Submit (each ignores the other).
    bool n42_press(const std::string &text)
    {
        Rml::Element *e = n42_find_any(text);
        if (e == nullptr) {
            return false;
        }
        e->DispatchEvent(Rml::EventId::Click, {});
        e->DispatchEvent(Rml::EventId::Submit, {});
        std::fprintf(stderr, "n42: %lld frame %u pressed '%s'\n", n42_ms(), netplay_frame(), text.c_str());
        return true;
    }

    std::string n42_dir()
    {
        const char *d = std::getenv("PB_N42_DIR");
        return d ? d : "/tmp";
    }

    nlohmann::json n42_lobby()
    {
        return nlohmann::json::parse(pbnet_text(pbnet_lobby_json), nullptr, false);
    }

    size_t n42_ready(const nlohmann::json &j)
    {
        if (j.is_object() && j.contains("room") && j["room"].is_object() && j["room"].contains("ready") &&
            j["room"]["ready"].is_array()) {
            return j["room"]["ready"].size();
        }
        return 0;
    }
} // namespace

void n42_harness()
{
    static const char *role = std::getenv("PB_N42");
    static int loop = 0;
    static int step = 0;
    static int wait = 0;
    static int lastState = -1;
    static std::string lastLobby;
    static std::string lastUi;
    static int lastStep = -1;
    if (role == nullptr) {
        return;
    }
    ++loop;
    const std::string r = role;
    const int state = pbnet_lobby_state();
    if (state != lastState) {
        std::fprintf(stderr, "n42: %lld frame %u lobby state %d -> %d\n", n42_ms(), netplay_frame(), lastState, state);
        lastState = state;
    }
    {
        const std::string j = pbnet_text(pbnet_lobby_json);
        if (j != lastLobby) {
            std::fprintf(stderr, "n42: %lld frame %u lobby %s\n", n42_ms(), netplay_frame(), j.c_str());
            lastLobby = j;
        }
    }
    {
        std::string m;
        for (const char *t : { "The match is over*", "Lobby Closed*", "Keep Waiting", "Leave Match", "Waiting for*",
                 "Host left*", "Start Match", "Ready" }) {
            if (Rml::Element *e = n42_find_any(t)) {
                m += "[" + e->GetInnerRML() + "]";
            }
        }
        if (m != lastUi) {
            std::fprintf(stderr, "n42: %lld frame %u ui %s\n", n42_ms(), netplay_frame(), m.empty() ? "(none)" : m.c_str());
            lastUi = m;
        }
    }
    if (step != lastStep) {
        std::fprintf(stderr, "n42: %lld frame %u step %d state %d\n", n42_ms(), netplay_frame(), step, state);
        lastStep = step;
    }
    if (wait > 0) {
        --wait;
        return;
    }
    const bool host = r == "host";
    switch (step) {
        case 0:
            if (loop < 240 || state != None) {
                return;
            }
            {
                auto w = std::make_unique<OnlineWindow>();
                OnlineWindow *ow = w.get();
                push_document(std::move(w));
                ow->set_active_tab(host ? 3 : 1);
            }
            wait = 30;
            step = 1;
            return;
        case 1:
            if (host ? n42_press("Toad*") : n42_press("Join Game")) {
                wait = 30;
                step = host ? 2 : 4;
            }
            return;
        case 2:
            if (n42_press("Host Match")) {
                wait = 60;
                step = 3;
            }
            return;
        case 3: {
            const auto j = n42_lobby();
            if (j.is_object() && !j.value("code", "").empty()) {
                std::ofstream(n42_dir() + "/code") << j.value("code", "");
                std::fprintf(stderr, "n42: %lld code written\n", n42_ms());
                step = 4;
            }
            return;
        }
        case 4:
        case 7:
            // host: Start Match once the guest is ready; guest: Ready
            if (state == Started) {
                step += 1;
                return;
            }
            if (state != Open) {
                return;
            }
            if (host ? n42_ready(n42_lobby()) >= 1 && n42_press("Start Match")
                     : n42_ready(n42_lobby()) == 0 && n42_press("Ready")) {
                wait = 120;
            }
            return;
        case 5:
        case 8:
            // the match runs; its end opens the lobby again
            if (state == Open) {
                std::fprintf(stderr, "n42: %lld frame %u back in the lobby\n", n42_ms(), netplay_frame());
                wait = 180;
                step += 1;
            }
            return;
        case 6:
            if (host && n42_ready(n42_lobby()) != 0) {
                std::fprintf(stderr, "n42: someone is ready before the guest pressed Ready again\n");
            }
            step = 7;
            return;
        default:
            return;
    }
}
