// N59: serves the N49 glyphs to RmlUi through aurora's runtime texture providers. A source is
// "pbicon://<family>/<glyph>"; each one is drawn once and kept, since RmlUi may reload a texture after a device loss.

#include "button_glyphs.hpp"

#include "port/button_icons.h"

#include <aurora/rmlui.hpp>
#include <fmt/format.h>

#include <algorithm>
#include <charconv>
#include <cstddef>
#include <map>
#include <mutex>
#include <optional>
#include <string_view>
#include <utility>
#include <vector>

namespace partyboard::ui {
namespace {

    constexpr std::string_view kScheme = "pbicon";

    // message icon entries (window.c's spcFontTbl), in the order a GameCube player knows them
    constexpr std::pair<int, const char *> kPromptEntries[] = {
        { 2, "A" },
        { 3, "B" },
        { 4, "X" },
        { 5, "Y" },
        { 11, "Z" },
        { 12, "Start" },
        { 8, "L" },
        { 6, "R" },
        { 0, "Control Stick" },
        { 10, "C Stick" },
        { 1, "D-Pad" },
    };

    std::optional<aurora::rmlui::RuntimeTexture> load_glyph(std::string_view source)
    {
        static std::mutex mutex;
        static std::map<std::pair<int, int>, std::vector<std::byte>> cache;

        source.remove_prefix(std::min(source.size(), kScheme.size() + 3));
        const auto slash = source.find('/');
        if (slash == std::string_view::npos) {
            return std::nullopt;
        }
        int family = -1, glyph = -1;
        const auto f = source.substr(0, slash), g = source.substr(slash + 1);
        if (std::from_chars(f.data(), f.data() + f.size(), family).ec != std::errc {}
            || std::from_chars(g.data(), g.data() + g.size(), glyph).ec != std::errc {}) {
            return std::nullopt;
        }

        const std::lock_guard lock(mutex);
        auto it = cache.find({ family, glyph });
        if (it == cache.end()) {
            std::vector<std::byte> rgba(PB_GLYPH_SIZE * PB_GLYPH_SIZE * 4);
            if (family < 0 || family >= PB_FAMILY_COUNT
                || pb_button_icons_glyph_rgba(static_cast<PbIconFamily>(family), glyph, reinterpret_cast<u8 *>(rgba.data())) != 0) {
                return std::nullopt;
            }
            it = cache.emplace(std::pair { family, glyph }, std::move(rgba)).first;
        }
        return aurora::rmlui::RuntimeTexture {
            .width = PB_GLYPH_SIZE,
            .height = PB_GLYPH_SIZE,
            .rgba8 = it->second,
            .premultipliedAlpha = false,
        };
    }

    void ensure_registered()
    {
        static std::once_flag once;
        std::call_once(once, [] { aurora::rmlui::register_texture_provider(Rml::String(kScheme), load_glyph); });
    }

} // namespace

Rml::String button_glyph_src(int port, int glyph)
{
    const PbIconFamily family = pb_button_icons_family(port);
    if (glyph < 0 || (glyph >= pb_button_icons_glyph_count() && !pb_button_icons_glyph_is_key(glyph)) || family == PB_FAMILY_GAMECUBE) {
        return {};
    }
    ensure_registered();
    return fmt::format("{}://{}/{}", kScheme, static_cast<int>(family), glyph);
}

Rml::String button_glyph_rml(int port, int glyph)
{
    const Rml::String src = button_glyph_src(port, glyph);
    return src.empty() ? Rml::String {} : fmt::format("<img class=\"glyph\" src=\"{}\"/>", src);
}

Rml::String button_glyph_preview_rml(int port)
{
    if (pb_button_icons_family(port) == PB_FAMILY_GAMECUBE) {
        return "<div class=\"glyph-preview\">GameCube: the game's own icons.</div>";
    }
    Rml::String rml = fmt::format("<div class=\"glyph-preview\">{}:", pb_button_icons_family_name(pb_button_icons_family(port)));
    for (const auto &[entry, name] : kPromptEntries) {
        rml += fmt::format("<br/>{}&nbsp;{}", button_glyph_rml(port, pb_button_icons_glyph_for_entry(port, entry)), name);
    }
    return rml + "</div>";
}

Rml::String button_glyph_strip_rml(int port)
{
    Rml::String rml;
    for (const auto &[entry, name] : kPromptEntries) {
        rml += button_glyph_rml(port, pb_button_icons_glyph_for_entry(port, entry));
    }
    return rml;
}

} // namespace partyboard::ui
