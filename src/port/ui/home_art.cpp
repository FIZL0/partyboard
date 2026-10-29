// N141: the home screen's backdrop from the disc: the title screen's background (data/title.bin, the starburst behind
// the Mario Party 4 logo), read and decoded here when the menu asks for it and served to RmlUi through aurora's runtime
// texture providers as "pbdisc://title-bg". Nothing is written to disk and nothing of the game's state is touched: the
// archive is read through the DVD API (the disc the game booted from) and decoded with the game's own HuDecodeData
// into buffers of ours.

#include "home_art.hpp"

#include <aurora/lib/logging.hpp>
#include <aurora/rmlui.hpp>

extern "C" {
#include <dolphin/dvd.h>
#include <game/data.h>
}

#include <cstddef>
#include <cstdint>
#include <mutex>
#include <optional>
#include <string_view>
#include <vector>

namespace partyboard::ui {
namespace {
    aurora::Module Log { "partyboard::ui::home_art" };

    constexpr std::string_view kScheme = "pbdisc";
    constexpr const char *kTitleArchive = "/data/title.bin";
    // title.bin's background on the US disc (bootDll TITLE_ANM_VER_ADJUSTED(TITLE_BG_ANM)); the others are scanned
    // for the same kind of picture (one full-screen RGBA8 bitmap) if it is not there.
    constexpr uint32_t kTitleBgFile = 8;
    // ANIM_BMP_RGBA8 / ANIM_BMP_RGB5A3 (game/animdata.h)
    constexpr uint8_t kFmtRgba8 = 0;
    constexpr uint8_t kFmtRgb5a3 = 1;

    struct Picture {
        uint32_t width = 0;
        uint32_t height = 0;
        std::vector<std::byte> rgba;
    };

    uint32_t be32(const std::vector<uint8_t> &b, size_t o)
    {
        return o + 4 <= b.size() ? (uint32_t(b[o]) << 24) | (uint32_t(b[o + 1]) << 16) | (uint32_t(b[o + 2]) << 8) | b[o + 3] : 0;
    }

    uint16_t be16(const std::vector<uint8_t> &b, size_t o)
    {
        return o + 2 <= b.size() ? uint16_t((b[o] << 8) | b[o + 1]) : 0;
    }

    std::optional<std::vector<uint8_t>> read_file(const char *path)
    {
        DVDFileInfo file {};
        if (!DVDOpen(path, &file)) {
            return std::nullopt;
        }
        std::vector<uint8_t> data(file.length);
        const s32 read = DVDReadPrio(&file, data.data(), static_cast<s32>(data.size()), 0, 2);
        DVDClose(&file);
        if (read < 0 || static_cast<size_t>(read) != data.size()) {
            return std::nullopt;
        }
        return data;
    }

    // HuData archive (data.c GetFileInfo): u32 count, u32 offsets; at each: u32 raw size, u32 decode type, the data.
    std::optional<std::vector<uint8_t>> archive_file(const std::vector<uint8_t> &archive, uint32_t index)
    {
        if (index >= be32(archive, 0)) {
            return std::nullopt;
        }
        const uint32_t offset = be32(archive, 4 + index * 4);
        const uint32_t rawLen = be32(archive, offset);
        const uint32_t decodeType = be32(archive, offset + 4);
        if (offset + 8 >= archive.size() || rawLen == 0 || rawLen > 16 * 1024 * 1024 || decodeType > DATA_DECODE_RLE) {
            return std::nullopt;
        }
        // the decoders read past the end of short files and may write a little past rawLen (a slide's last copy)
        std::vector<uint8_t> src(archive.begin() + offset + 8, archive.end());
        src.resize(src.size() + 0x400);
        std::vector<uint8_t> out(rawLen + 0x200);
        HuDecodeData(src.data(), out.data(), rawLen, static_cast<s32>(decodeType));
        out.resize(rawLen);
        return out;
    }

    // ANIMDATA as on the disc (big-endian, offsets for pointers): its first bitmap (ANIMBMP: u8 pixSize, u8 dataFmt,
    // s16 palNum, s16 sizeX, s16 sizeY, u32 dataSize, u32 palData, u32 data), if it is a full-screen RGBA8/RGB5A3 one.
    std::optional<Picture> anim_picture(const std::vector<uint8_t> &anim)
    {
        const auto bmpNum = static_cast<int16_t>(be16(anim, 4));
        const uint32_t bmp = be32(anim, 0x10);
        if (bmpNum != 1 || bmp + 20 > anim.size()) {
            return std::nullopt;
        }
        const uint8_t fmt = anim[bmp + 1] & 0xF;
        const auto w = static_cast<int16_t>(be16(anim, bmp + 4));
        const auto h = static_cast<int16_t>(be16(anim, bmp + 6));
        const uint32_t data = be32(anim, bmp + 16);
        if ((fmt != kFmtRgba8 && fmt != kFmtRgb5a3) || w < 512 || h < 400 || w > 1024 || h > 1024 || (w & 3) || (h & 3)) {
            return std::nullopt;
        }
        const size_t need = size_t(w) * h * (fmt == kFmtRgba8 ? 4 : 2);
        if (data + need > anim.size()) {
            return std::nullopt;
        }
        Picture pic { uint32_t(w), uint32_t(h), std::vector<std::byte>(size_t(w) * h * 4) };
        const auto put = [&pic](uint32_t x, uint32_t y, uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
            std::byte *p = &pic.rgba[(size_t(y) * pic.width + x) * 4];
            p[0] = std::byte(r);
            p[1] = std::byte(g);
            p[2] = std::byte(b);
            p[3] = std::byte(a);
        };
        // GX tiles of 4x4 texels: RGBA8 has the AR pairs of a tile, then its GB pairs; RGB5A3 one u16 per texel
        size_t o = data;
        for (uint32_t ty = 0; ty < uint32_t(h); ty += 4) {
            for (uint32_t tx = 0; tx < uint32_t(w); tx += 4) {
                for (uint32_t i = 0; i < 16; ++i) {
                    const uint32_t x = tx + i % 4, y = ty + i / 4;
                    if (fmt == kFmtRgba8) {
                        put(x, y, anim[o + i * 2 + 1], anim[o + 32 + i * 2], anim[o + 32 + i * 2 + 1], anim[o + i * 2]);
                    }
                    else {
                        const uint16_t v = be16(anim, o + i * 2);
                        if (v & 0x8000) {
                            put(x, y, uint8_t(((v >> 10) & 31) * 255 / 31), uint8_t(((v >> 5) & 31) * 255 / 31),
                                uint8_t((v & 31) * 255 / 31), 255);
                        }
                        else {
                            put(x, y, uint8_t(((v >> 8) & 15) * 17), uint8_t(((v >> 4) & 15) * 17), uint8_t((v & 15) * 17),
                                uint8_t(((v >> 12) & 7) * 255 / 7));
                        }
                    }
                }
                o += fmt == kFmtRgba8 ? 64 : 32;
            }
        }
        return pic;
    }

    std::optional<Picture> load_title_background()
    {
        const auto archive = read_file(kTitleArchive);
        if (!archive) {
            Log.warn("{} not found on the disc", kTitleArchive);
            return std::nullopt;
        }
        const uint32_t count = be32(*archive, 0);
        for (uint32_t pass = 0; pass <= count; ++pass) {
            // the known file first, then the others in order
            const uint32_t index = pass == 0 ? kTitleBgFile : pass - 1;
            if (pass > 0 && index == kTitleBgFile) {
                continue;
            }
            if (auto anim = archive_file(*archive, index)) {
                if (auto pic = anim_picture(*anim)) {
                    Log.info("title background: {} file {} ({}x{})", kTitleArchive, index, pic->width, pic->height);
                    return pic;
                }
            }
        }
        Log.warn("no title background in {}", kTitleArchive);
        return std::nullopt;
    }

    std::mutex sMutex;
    bool sTried = false;
    std::optional<Picture> sTitleBackground;

    std::optional<aurora::rmlui::RuntimeTexture> provide(std::string_view source)
    {
        if (source.substr(kScheme.size()) != "://title-bg") {
            return std::nullopt;
        }
        const std::lock_guard lock(sMutex);
        if (!sTitleBackground) {
            return std::nullopt;
        }
        return aurora::rmlui::RuntimeTexture {
            .width = sTitleBackground->width,
            .height = sTitleBackground->height,
            .rgba8 = sTitleBackground->rgba,
            .premultipliedAlpha = false,
        };
    }

} // namespace

Rml::String home_art_title_background()
{
    {
        const std::lock_guard lock(sMutex);
        if (!sTried) {
            sTried = true;
            sTitleBackground = load_title_background();
            if (sTitleBackground) {
                aurora::rmlui::register_texture_provider(Rml::String(kScheme), provide);
            }
        }
        if (!sTitleBackground) {
            return {};
        }
    }
    return Rml::String(kScheme) + "://title-bg";
}

} // namespace partyboard::ui
