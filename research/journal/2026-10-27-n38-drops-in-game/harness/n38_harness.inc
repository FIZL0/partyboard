// N38 scratch harness (not for commit): drives the Online menu by pressing its real buttons, logs the hold status.
// PB_N38=host|guest|rejoin, PB_N38_DIR=<dir shared by both peers>.


extern "C" unsigned int netplay_frame(void);
namespace {
    long long n38_ms()
    {
        timeval tv;
        gettimeofday(&tv, nullptr);
        return (long long)tv.tv_sec * 1000 + tv.tv_usec / 1000;
    }

    Rml::Element *n38_find(Rml::Element *e, const std::string &text)
    {
        {
            const std::string rml = e->GetInnerRML();
            if (rml == text || (text.back() == '*' && rml.rfind(text.substr(0, text.size() - 1), 0) == 0)) {
                return e;
            }
        }
        for (int i = 0; i < e->GetNumChildren(); ++i) {
            if (auto *f = n38_find(e->GetChild(i), text)) {
                return f;
            }
        }
        return nullptr;
    }

    Rml::Element *n38_find_any(const std::string &text)
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
            if (auto *f = n38_find(d, text)) {
                return f;
            }
        }
        return nullptr;
    }

    // Press like a click or a pad confirm: Button listens to Click, SelectButton to Submit (each ignores the other).
    bool n38_press(const std::string &text)
    {
        Rml::Element *e = n38_find_any(text);
        if (e == nullptr) {
            return false;
        }
        // Click and Submit both bubble to the control's root
        e->DispatchEvent(Rml::EventId::Click, {});
        e->DispatchEvent(Rml::EventId::Submit, {});
        std::fprintf(stderr, "n38: %lld pressed '%s'\n", n38_ms(), text.c_str());
        return true;
    }

    std::string n38_dir()
    {
        const char *d = std::getenv("PB_N38_DIR");
        return d ? d : "/tmp";
    }

    bool n38_take(const std::string &name)
    {
        const std::string p = n38_dir() + "/" + name;
        if (std::FILE *f = std::fopen(p.c_str(), "r")) {
            std::fclose(f);
            std::remove(p.c_str());
            return true;
        }
        return false;
    }
} // namespace

void n38_harness()
{
    static const char *role = std::getenv("PB_N38");
    static int loop = 0;
    static int step = 0;
    static int wait = 0;
    static std::string lastRecover;
    static std::string lastModal;
    if (role == nullptr) {
        return;
    }
    ++loop;
    const std::string r = role;
    const int state = pbnet_lobby_state();

    // status log
    if (state == Started) {
        std::string j = pbnet_text(pbnet_recover_json);
        for (size_t q; (q = j.find("\"quiet_ms\":")) != std::string::npos;) {
            size_t e = j.find_first_of(",}", q);
            j.erase(q, e - q);
        }
        if (j != lastRecover) {
            std::fprintf(stderr, "n38: %lld frame %u pbnet %u recover %s\n", n38_ms(), netplay_frame(), pbnet_frame(),
                j.c_str());
            lastRecover = j;
        }
    }
    {
        std::string m;
        for (const char *t : { "Keep Waiting", "CPU Plays for*", "Save and Quit", "Leave Match", "Waiting for*",
                 "Host left*" }) {
            if (Rml::Element *e = n38_find_any(t)) {
                m += "[" + e->GetInnerRML() + "]";
            }
        }
        if (m != lastModal) {
            std::fprintf(stderr, "n38: %lld modal %s\n", n38_ms(), m.empty() ? "(none)" : m.c_str());
            lastModal = m;
        }
    }
    if (wait > 0) {
        --wait;
        return;
    }
    if (n38_take("cmd-cpu")) {
        if (!n38_press("CPU Plays for*")) {
            std::fprintf(stderr, "n38: no CPU Plays for button\n");
        }
    }
    if (n38_take("cmd-keep")) {
        n38_press("Keep Waiting");
    }
    static int lastStep = -1;
    if (step != lastStep) {
        std::fprintf(stderr, "n38: %lld loop %d step %d state %d\n", n38_ms(), loop, step, state);
        lastStep = step;
    }
    if (step == 1 && loop % 300 == 0) {
        Rml::Context *c = Rml::GetContext(0);
        for (int i = 0; c && i < c->GetNumDocuments(); ++i) {
            std::fprintf(stderr, "n38: doc %d visible %d\n", i, c->GetDocument(i)->IsVisible());
        }
    }
    switch (step) {
        case 0:
            if (loop < 240 || state != None) {
                return;
            }
            {
                auto w = std::make_unique<OnlineWindow>();
                OnlineWindow *ow = w.get();
                push_document(std::move(w));
                ow->set_active_tab(r == "host" ? 0 : 1);
            }
            wait = 30;
            step = 1;
            return;
        case 1:
            if (n38_press(r == "host" ? "Host Game" : r == "guest" ? "Join Game" : "Rejoin Match")) {
                wait = 60;
                step = 2;
            }
            return;
        case 2:
            if (r == "host") {
                const auto j = nlohmann::json::parse(pbnet_text(pbnet_lobby_json), nullptr, false);
                if (j.is_object() && !j.value("code", "").empty()) {
                    std::ofstream(n38_dir() + "/code") << j.value("code", "");
                    std::fprintf(stderr, "n38: %lld code written\n", n38_ms());
                    step = 3;
                }
            }
            else if (r == "guest") {
                if (state == Started || n38_press("Ready")) {
                    wait = 60;
                    step = 9;
                }
            }
            else {
                step = 9;
            }
            return;
        case 3: {
            // host: start when the guest's seat is ready
            const auto j = nlohmann::json::parse(pbnet_text(pbnet_lobby_json), nullptr, false);
            if (j.is_object() && j.contains("room") && j["room"].contains("ready") && j["room"]["ready"].size() >= 1 &&
                (++wait, n38_press("Start Match"))) {
                wait = 120;
                step = 4;
            }
            return;
        }
        case 4:
            if (state != Started) {
                step = 3;
            }
            else {
                step = 9;
            }
            return;
        default:
            return;
    }
}

