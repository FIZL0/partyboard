// N142: N89/N122's harness plus "<frame> shotui <png>" (aurora_capture_frame_ui: game + RmlUi) and
// "<frame> ovl" (omcurovl / omovlevtno). Every UI frame logs an overlay change as "n142: ovl".
//
// N89 temporary test harness, from N88's (research/journal/2026-10-28-n88-online-launcher-button/harness). Added
// lines: "<frame> lobby" (pbnet_lobby_json), "<frame> writecode <file>" (host's long code to a file),
// "<frame> prepjoin <codefile> <profile>" (waits for the code, puts it in <profile>'s last_code),
// "<frame> jointab" (Online window's Join tab), "<frame> ready", "<frame> players" (GWPlayerCfg), "<frame> cat <file>".
//
// Original header: N88 temporary test harness: PB_N88_TEST=<file> pushes SDL key / gamepad / finger events at UI frames and dumps
// the document stack. Lines: "<frame> key <scancode>", "<frame> pad <button>", "<frame> tap <button text>",
// "<frame> dump", "<frame> quit".
#include "port/pbnet.h"
#include <aurora/frame_capture_ui.h>
extern "C" {
#include "game/object.h"
}
#include <fstream>
#include <nlohmann/json.hpp>
#include <thread>
extern "C" {
#include "game/gamework_data.h"
}
#include <aurora/gfx.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <typeinfo>
#include <vector>
namespace {
struct N88Ev { unsigned frame; std::string type; std::string arg; };
std::vector<N88Ev> sN88Evs;
size_t sN88Next = 0;
unsigned sN88Frame = 0;
bool sN88Loaded = false;

Rml::Element *n88_find(Rml::Element *e, const std::string &text)
{
    if (e == nullptr || !e->IsVisible()) {
        return nullptr;
    }
    if ((e->GetTagName() == "button") && e->GetInnerRML().find(text) != std::string::npos) {
        return e;
    }
    for (int i = 0; i < e->GetNumChildren(); ++i) {
        if (auto *r = n88_find(e->GetChild(i), text)) {
            return r;
        }
    }
    return nullptr;
}

std::string n88_text(Rml::Element *e)
{
    if (e == nullptr) return "(null)";
    std::string s = e->GetInnerRML();
    std::string out;
    bool tag = false;
    for (char c : s) {
        if (c == '<') tag = true;
        else if (c == '>') { tag = false; out += ' '; }
        else if (!tag && c != '\n') out += c;
    }
    if (out.size() > 80) out.resize(80);
    return e->GetTagName() + "#" + e->GetId() + " '" + out + "'";
}

void n88_dump()
{
    auto *ctx = aurora::rmlui::get_context();
    fprintf(stderr, "n88: frame %u launched %d lobby_state %d focus %s\n", sN88Frame, (int)PartyBoard_IsGameLaunched,
        pbnet_lobby_state(), ctx ? n88_text(ctx->GetFocusElement()).c_str() : "-");
    for (auto &d : sDocumentStack) {
        fprintf(stderr, "n88:   doc %s visible %d closed %d pending %d\n", typeid(*d).name(), d->visible(), d->closed(),
            d->pending_close());
    }
    if (auto *top = top_document()) {
        fprintf(stderr, "n88:   top %s\n", typeid(*top).name());
    }
    for (int i = 0; ctx && i < ctx->GetNumDocuments(); ++i) {
        auto *doc = ctx->GetDocument(i);
        if (!doc->IsVisible()) continue;
        std::vector<Rml::Element *> buttons;
        doc->GetElementsByTagName(buttons, "button");
        std::string names;
        for (auto *b : buttons) {
            if (b->IsVisible(true)) { names += "[" + n88_text(b).substr(7) + "] "; }
        }
        if (names.size() > 600) names.resize(600);
        fprintf(stderr, "n88:   rml doc %d buttons %s\n", i, names.c_str());
    }
}

void n88_tick()
{
    if (!sN88Loaded) {
        sN88Loaded = true;
        if (const char *path = getenv("PB_N89_TEST")) {
            if (FILE *f = fopen(path, "r")) {
                char line[1200];
                while (fgets(line, sizeof line, f)) {
                    unsigned fr; char type[16]; char arg[1000] = "";
                    int n = sscanf(line, "%u %15s %999[^\n]", &fr, type, arg);
                    if (n >= 2) sN88Evs.push_back({ fr, type, arg });
                }
                fclose(f);
            }
            fprintf(stderr, "n88: %zu events\n", sN88Evs.size());
        }
    }
    sN88Frame++;
    {
        static int lastOvl = -2;
        if ((int)omcurovl != lastOvl) {
            lastOvl = (int)omcurovl;
            fprintf(stderr, "n142: ovl frame %u omcurovl %d evt %d\n", sN88Frame, (int)omcurovl, (int)omovlevtno);
        }
    }
    while (sN88Next < sN88Evs.size() && sN88Evs[sN88Next].frame <= sN88Frame) {
        const N88Ev &e = sN88Evs[sN88Next++];
        fprintf(stderr, "n88: frame %u %s %s\n", sN88Frame, e.type.c_str(), e.arg.c_str());
        SDL_Event ev {};
        if (e.type == "key") {
            ev.type = SDL_EVENT_KEY_DOWN;
            ev.key.scancode = (SDL_Scancode)atoi(e.arg.c_str());
            ev.key.key = SDL_GetKeyFromScancode(ev.key.scancode, SDL_KMOD_NONE, false);
            ev.key.down = true;
            ev.key.windowID = SDL_GetWindowID(aurora::window::get_sdl_window());
            SDL_PushEvent(&ev);
            ev.type = SDL_EVENT_KEY_UP;
            ev.key.down = false;
            SDL_PushEvent(&ev);
        }
        else if (e.type == "pad") {
            ev.type = SDL_EVENT_GAMEPAD_BUTTON_DOWN;
            ev.gbutton.which = 0x7777;
            ev.gbutton.button = (Uint8)atoi(e.arg.c_str());
            ev.gbutton.down = true;
            SDL_PushEvent(&ev);
            ev.type = SDL_EVENT_GAMEPAD_BUTTON_UP;
            ev.gbutton.down = false;
            SDL_PushEvent(&ev);
        }
        else if (e.type == "tap") {
            auto *ctx = aurora::rmlui::get_context();
            Rml::Element *b = nullptr;
            if (auto *top = top_document()) {
                for (int i = ctx->GetNumDocuments() - 1; i >= 0 && b == nullptr; --i) {
                    if (ctx->GetDocument(i)->IsVisible()) b = n88_find(ctx->GetDocument(i), e.arg);
                }
            }
            if (b == nullptr) { fprintf(stderr, "n88: tap target '%s' not found\n", e.arg.c_str()); continue; }
            auto o = b->GetAbsoluteOffset(Rml::BoxArea::Border);
            auto sz = b->GetBox().GetSize(Rml::BoxArea::Border);
            auto dim = ctx->GetDimensions();
            // GetAbsoluteOffset ignores transforms: scan the button's column for a point that hits it.
            float px = o.x + sz.x / 2, py = o.y + sz.y / 2;
            for (float yy = 0; yy < dim.y; yy += 4) {
                auto *h = ctx->GetElementAtPoint({ px, yy });
                if (h == b || (h && b->IsPointWithinElement({ px, yy }) && h->GetParentNode() == b)) { py = yy + 8; break; }
            }
            float x = px / dim.x, y = py / dim.y;
            fprintf(stderr, "n88: tap %s at %.3f,%.3f (off %.0f,%.0f size %.0f,%.0f dim %d,%d) hit %s\n", n88_text(b).c_str(), x, y,
                o.x, o.y, sz.x, sz.y, dim.x, dim.y, n88_text(ctx->GetElementAtPoint({ px, py })).c_str());
            ev.type = SDL_EVENT_FINGER_DOWN;
            ev.tfinger.touchID = 1;
            ev.tfinger.fingerID = 5;
            ev.tfinger.x = x;
            ev.tfinger.y = y;
            ev.tfinger.windowID = SDL_GetWindowID(aurora::window::get_sdl_window());
            SDL_PushEvent(&ev);
            ev.type = SDL_EVENT_FINGER_UP;
            SDL_PushEvent(&ev);
        }
        else if (e.type == "shotui") {
            aurora_capture_frame_ui(e.arg.c_str(), 0);
        }
        else if (e.type == "shot") {
            aurora_capture_frame(e.arg.c_str(), 0);
        }
        else if (e.type == "lobby" || e.type == "writecode") {
            char buf[16384];
            int n = pbnet_lobby_json(buf, sizeof buf);
            std::string js(buf, n > 0 ? std::min<int>(n, sizeof buf - 1) : 0);
            if (e.type == "lobby") {
                fprintf(stderr, "n89: lobby %s\n", js.c_str());
            } else {
                auto j = nlohmann::json::parse(js, nullptr, false);
                std::string code = j.is_object() ? j.value("code", "") : "";
                std::ofstream(e.arg) << code;
                fprintf(stderr, "n89: wrote code '%s'\n", code.c_str());
            }
        }
        else if (e.type == "prepjoin") {
            const std::string codefile = e.arg.substr(0, e.arg.find(' '));
            const std::string prof = e.arg.substr(e.arg.find(' ') + 1);
            std::string code;
            for (int i = 0; i < 600 && code.empty(); ++i) {
                std::ifstream in(codefile);
                std::getline(in, code);
                if (code.empty()) std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
            nlohmann::json j = nlohmann::json::object();
            { std::ifstream in(prof); if (in) { j = nlohmann::json::parse(in, nullptr, false); if (!j.is_object()) j = nlohmann::json::object(); } }
            j["last_code"] = code;
            std::ofstream(prof) << j.dump();
            fprintf(stderr, "n89: prepjoin code '%s' into %s\n", code.substr(0, 20).c_str(), prof.c_str());
        }
        else if (e.type == "jointab") {
            for (auto &d : sDocumentStack) {
                if (auto *w = dynamic_cast<OnlineWindow *>(d.get())) {
                    fprintf(stderr, "n89: jointab %d\n", (int)w->set_active_tab(1));
                }
            }
        }
        else if (e.type == "ready") {
            fprintf(stderr, "n89: ready %d\n", pbnet_lobby_ready(1));
        }
        else if (e.type == "players") {
            for (int i = 0; i < 4; ++i) {
                fprintf(stderr, "n89: GWPlayerCfg[%d] iscom %d character %d pad %d\n", i, GWPlayerCfg[i].iscom, GWPlayerCfg[i].character,
                    GWPlayerCfg[i].pad_idx);
            }
        }
        else if (e.type == "cat") {
            std::ifstream in(e.arg);
            std::string all((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
            fprintf(stderr, "n89: cat %s: %s\n", e.arg.c_str(), all.c_str());
        }
        else if (e.type == "dump") {
            n88_dump();
        }
        else if (e.type == "quit") {
            fflush(stderr);
            _exit(0);
        }
    }
}
}
