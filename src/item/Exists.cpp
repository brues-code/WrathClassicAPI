// This file is part of WrathClassicAPI.
//
// WrathClassicAPI is free software: you can redistribute it and/or modify it under the terms
// of the GNU Lesser General Public License as published by the Free Software Foundation, either
// version 3 of the License, or (at your option) any later version.
//
// WrathClassicAPI is distributed in the hope that it will be useful, but WITHOUT ANY
// WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE. See the GNU Lesser General Public License for more details.
//
// You should have received a copy of the GNU Lesser General Public License along with
// WrathClassicAPI. If not, see <https://www.gnu.org/licenses/>.

#include "Game.h"
#include "Offsets.h"
#include "item/Arg.h"
#include "item/Data.h"
#include "item/Location.h"

#include <cstdint>

namespace Item::Exists {

namespace {

using GetItemRecord_t = const uint8_t *(__thiscall *)(void *cache, uint32_t itemID,
                                                      const uint64_t *guid, void *callback,
                                                      void *userData, int unused);

const uint8_t *PeekItemRecord(uint32_t itemID) {
    auto fn = reinterpret_cast<GetItemRecord_t>(Offsets::FUN_DBCACHE_ITEMSTATS_GET_RECORD);
    auto *cache = reinterpret_cast<void *>(Offsets::VAR_ITEMDB_CACHE);
    const uint64_t zeroGuid = 0;
    return fn(cache, itemID, &zeroGuid, nullptr, nullptr, 0);
}

// `C_Item.DoesItemExist(itemLocation)` — true iff the location resolves
// to a populated inventory slot on the active player. Equipment-slot
// locations check the character pane; bag-slot locations walk the
// player's inventory manager. Empty slots and invalid tables return
// false without raising.
int __cdecl Script_C_Item_DoesItemExist(void *L) {
    if (!Item::Location::IsLocationArg(L, 1)) {
        Game::Lua::PushBool(L, false);
        return 1;
    }
    const uint8_t *item = Item::Location::Resolve(L, 1);
    Game::Lua::PushBool(L, item != nullptr);
    return 1;
}

// `C_Item.DoesItemExistByID(itemInfo)` — true iff the cache currently
// has data for this itemID (i.e. `GetItemInfo` would return non-nil
// right now). Same semantics as the modern function: cache miss →
// false, but we kick off the network query so a subsequent call after
// `GET_ITEM_INFO_RECEIVED` will succeed.
//
// Accepts numeric itemID or `"item:NNN..."` string (including full
// chat links). Returns false for any input we can't resolve to an
// itemID; never raises.
int __cdecl Script_C_Item_DoesItemExistByID(void *L) {
    const int itemID = Item::Arg::ResolveItemID(L, 1);
    if (itemID <= 0) {
        Game::Lua::PushBool(L, false);
        return 1;
    }
    if (PeekItemRecord(static_cast<uint32_t>(itemID)) == nullptr) {
        Item::Data::WarmCache(static_cast<uint32_t>(itemID));
        Game::Lua::PushBool(L, false);
        return 1;
    }
    Game::Lua::PushBool(L, true);
    return 1;
}

void RegisterLuaFunctions() {
    Game::Lua::RegisterTableFunction("C_Item", "DoesItemExist", &Script_C_Item_DoesItemExist);
    Game::Lua::RegisterTableFunction("C_Item", "DoesItemExistByID", &Script_C_Item_DoesItemExistByID);
}

const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace
} // namespace Item::Exists
