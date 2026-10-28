// N98: what the menu has focused, for page scripts that drive the menus with real key presses (tools/web_pair.mjs:
// the Online menu's buttons, the clipboard bridge). Read only; the keys go in as a player's would.
#include <RmlUi/Core.h>
#include <aurora/rmlui.hpp>
#include <emscripten.h>

#include <algorithm>
#include <cstring>
#include <string>

namespace {

void text_of(Rml::Element *e, std::string &out)
{
    if (auto *t = rmlui_dynamic_cast<Rml::ElementText *>(e)) {
        out += t->GetText();
        out += ' ';
    }
    for (int i = 0; i < e->GetNumChildren(); ++i) {
        text_of(e->GetChild(i), out);
    }
}

} // namespace

// The focused element's text (its own and its children's, in order; empty if nothing has focus), as much of it as
// fits in `cap` bytes with its NUL. Returns the whole text's length.
extern "C" EMSCRIPTEN_KEEPALIVE int pb_web_ui_focus(char *buf, int cap)
{
    std::string text;
    if (auto *context = aurora::rmlui::get_context()) {
        if (auto *e = context->GetFocusElement(); e != nullptr && e != context->GetRootElement()) {
            text_of(e, text);
        }
    }
    if (buf != nullptr && cap > 0) {
        const size_t n = std::min(text.size(), static_cast<size_t>(cap - 1));
        std::memcpy(buf, text.data(), n);
        buf[n] = '\0';
    }
    return static_cast<int>(text.size());
}
