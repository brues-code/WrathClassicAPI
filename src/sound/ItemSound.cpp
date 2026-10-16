// This file is part of WrathClassicAPI.
//
// WrathClassicAPI is free software: you can redistribute it and/or modify it under the terms
// of the GNU General Public License as published by the Free Software Foundation, either
// version 3 of the License, or (at your option) any later version.
//
// WrathClassicAPI is distributed in the hope that it will be useful, but WITHOUT ANY
// WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE. See the GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License along with
// WrathClassicAPI. If not, see <https://www.gnu.org/licenses/>.

// `C_Sound.PlayItemSound(soundType, item)` and `Enum.ItemSoundType` — the
// noise an item makes when picked up, dropped, used or closed, without the
// action. Ported from ClassicAPI's src/sound/ItemSound.cpp.
//
// SI2::PlayItemSound does the lookup: ItemDisplayInfo -> its ItemGroupSounds
// row -> the row's four SoundEntries ids, in the modern enum's order (Pickup,
// Drop, Use, Close). This module only turns the item argument into a display
// id. The display id comes from the client's Item.dbc, so any stock item works
// whether or not its stats are cached; the item cache covers items the client
// doesn't ship.
//
// The type passes straight through: on stock data only Pickup and Drop have
// sounds, but a server that fills in Use and Close gets them with no change
// here.

#include "Game.h"
#include "Offsets.h"
#include "item/Arg.h"
#include "item/Dbc.h"
#include "item/ID.h"
#include "item/Location.h"

#include <cstdint>

namespace Sound::ItemSound {

namespace {

using PlayItemSound_t = void(__cdecl *)(int type, int displayInfoID);
using GetItemRecord_t = const uint8_t *(__thiscall *)(void *cache, uint32_t itemID,
                                                      const uint64_t *guid, void *callback,
                                                      void *userData, int unused);

const Game::Lua::EnumIntegerEntry kItemSoundTypeEntries[] = {
    {"Pickup", 0},
    {"Drop", 1},
    {"Use", 2},
    {"Close", 3},
};

constexpr int kSoundTypeMax = 3;

// Item-stats cache peek — no query fired on a miss (callback = nullptr).
const uint8_t *PeekItemRecord(uint32_t itemID) {
    auto fn = reinterpret_cast<GetItemRecord_t>(Offsets::FUN_DBCACHE_ITEMSTATS_GET_RECORD);
    auto *cache = reinterpret_cast<void *>(Offsets::VAR_ITEMDB_CACHE);
    const uint64_t zeroGuid = 0;
    return fn(cache, itemID, &zeroGuid, nullptr, nullptr, 0);
}

uint32_t DisplayInfoID(int itemID) {
    if (itemID <= 0)
        return 0;
    if (const uint8_t *rec = Item::Dbc::GetRecord(static_cast<uint32_t>(itemID)))
        return *reinterpret_cast<const uint32_t *>(rec + Offsets::OFF_ITEMDBC_DISPLAY_INFO_ID);
    if (const uint8_t *rec = PeekItemRecord(static_cast<uint32_t>(itemID)))
        return *reinterpret_cast<const uint32_t *>(rec + Offsets::OFF_ITEMSTATS_DISPLAY_INFO_ID);
    return 0;
}

// C_Sound.PlayItemSound(soundType, item)
//
// `item` is an item location, or any item argument the C_Item ByID calls
// take. Plays nothing for an item that can't be found.
int __cdecl Script_PlayItemSound(void *L) {
    if (Game::Lua::Type(L, 1) != Game::Lua::TYPE_NUMBER)
        return Game::Lua::Error(L, "Usage: C_Sound.PlayItemSound(soundType, item)");
    // Read before resolving: the bag-slot resolver overwrites stack slots 1-2.
    const int soundType = static_cast<int>(Game::Lua::ToNumber(L, 1));
    if (soundType < 0 || soundType > kSoundTypeMax)
        return 0;

    const int itemID = Game::Lua::Type(L, 2) == Game::Lua::TYPE_TABLE
                           ? Item::ID::FromCGItem(Item::Location::Resolve(L, 2))
                           : Item::Arg::ResolveItemID(L, 2);
    const uint32_t displayInfoID = DisplayInfoID(itemID);
    if (displayInfoID != 0)
        reinterpret_cast<PlayItemSound_t>(Offsets::FUN_SI2_PLAY_ITEM_SOUND)(
            soundType, static_cast<int>(displayInfoID));
    return 0;
}

void RegisterLuaFunctions() {
    Game::Lua::RegisterTableFunction("C_Sound", "PlayItemSound", &Script_PlayItemSound);
    Game::Lua::RegisterIntegerEnum("Enum", "ItemSoundType", kItemSoundTypeEntries,
                                   static_cast<int>(sizeof(kItemSoundTypeEntries) /
                                                    sizeof(kItemSoundTypeEntries[0])));
}

const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace

} // namespace Sound::ItemSound
