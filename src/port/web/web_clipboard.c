/*
 * N92: the page's clipboard for the game. SDL 3.4 has no Emscripten clipboard (SDL_SetClipboardText keeps the text
 * inside the process), and online play in the browser needs codes copied both ways (net/src/rtc.rs). So a copy also
 * goes to navigator.clipboard, and the page hands what the player pastes (Ctrl+V, platforms/web/pb.js) to SDL's
 * clipboard, where the menus' Paste buttons read it.
 */
#include <SDL3/SDL_clipboard.h>
#include <emscripten.h>

EM_JS(void, web_clipboard_write, (const char *text), {
    const t = UTF8ToString(text);
    if (navigator.clipboard && navigator.clipboard.writeText) {
        navigator.clipboard.writeText(t).catch(e => console.warn('clipboard: ' + e));
    }
});

void pb_web_copy(const char *text)
{
    web_clipboard_write(text);
}

/* Called by the page (pb.js) with the text the player pasted. */
EMSCRIPTEN_KEEPALIVE void pb_web_paste(const char *text)
{
    SDL_SetClipboardText(text);
}
