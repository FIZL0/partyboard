#pragma once

// N59: the N49 button glyphs (port/button_icons.h) as RmlUi images.

#include <RmlUi/Core.h>

namespace partyboard::ui {

// The image source of `glyph` (PB_GLYPH_* / a pb_button_icons_glyph_for_* result) in the family pad port `port`
// shows, e.g. "pbicon://2/0". Empty when there is nothing to draw: no glyph, or a GameCube family (the game keeps
// its own icons there, which are not ours to draw).
Rml::String button_glyph_src(int port, int glyph);

// An <img> of the glyph for inline RML (sized by the "glyph" class), or empty like button_glyph_src.
Rml::String button_glyph_rml(int port, int glyph);

// RML listing what each of the game's button prompts shows for `port` (GameCube A ... D-pad, each with its glyph).
Rml::String button_glyph_preview_rml(int port);
// The same glyphs in one row without names (empty for GameCube).
Rml::String button_glyph_strip_rml(int port);

} // namespace partyboard::ui
