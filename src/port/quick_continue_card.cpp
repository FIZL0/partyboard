// N132: the launcher's read of the memory card for Party Board menu > Continue (include/port/quick_continue.h).
// The game's card (CARDInit) only opens when the game starts, so this reads slot A's three save files with aurora's
// own card classes, at the path CARDInit would use, and only reads: nothing is committed back.

#include "quick_continue_card.hpp"

#include <filesystem>

#include "port/main.h"
#include "port/settings.h"

#include <aurora/lib/card/CardGciFolder.hpp>
#include <aurora/lib/card/CardRawFile.hpp>
#include <aurora/lib/logging.hpp>

#include <array>
#include <memory>
#include <system_error>

extern "C" {
#include <game/saveload.h>
}

namespace partyboard::quick_continue {
namespace {
    aurora::Module Log { "partyboard::quick_continue" };

    // aurora card.cpp GetCardFullPath for slot A.
    std::filesystem::path card_path(CARDFileType type, bool pal)
    {
        const char *region = pal ? "EUR" : "USA";
        if (type == CARD_GCIFOLDER) {
            return PartyBoard_ConfigPath / region / "Card A";
        }
        return PartyBoard_ConfigPath / (std::string("MemoryCardA.") + region + ".raw");
    }
} // namespace

std::vector<QuickContinueEntry> scan(bool pal)
{
    const CARDFileType type = partyboard_settings_card_file_type();
    const auto path = card_path(type, pal);
    std::array<std::array<u8, SAVE_BUF_SIZE>, 3> files {};
    const u8 *list[3] = {};

    std::error_code ec;
    if (std::filesystem::exists(path, ec)) {
        std::unique_ptr<aurora::card::ICard> card;
        if (type == CARD_GCIFOLDER) {
            card = std::make_unique<aurora::card::CardGciFolder>();
        }
        else {
            card = std::make_unique<aurora::card::CardRawFile>();
        }
        // HuCardInit's.
        card->InitCard("Party Board", "mariopartyrd");
        if (card->open(path)) {
            for (int box = 0; box < 3; box++) {
                aurora::card::FileHandle handle;
                if (card->openFile(SaveFileNameTbl[box], handle) != aurora::card::ECardResult::READY) {
                    continue;
                }
                card->seek(handle, 0, aurora::card::SeekOrigin::Begin);
                if (card->fileRead(handle, files[box].data(), SAVE_BUF_SIZE) == aurora::card::ECardResult::READY) {
                    list[box] = files[box].data();
                }
                card->closeFile(handle);
            }
            card->close();
        }
    }

    std::vector<QuickContinueEntry> entries(QUICK_CONTINUE_MAX);
    entries.resize(quick_continue_list(list, entries.data()));
    Log.info("{} game(s) in progress ({})", entries.size(), path.string());
    return entries;
}

} // namespace partyboard::quick_continue
