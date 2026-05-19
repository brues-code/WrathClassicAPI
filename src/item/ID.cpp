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

#include "ID.h"

#include "Game.h"
#include "Offsets.h"
#include "item/Location.h"

#include <cstdint>

namespace Item::ID {

int FromCGItem(const uint8_t *item) {
    if (item == nullptr)
        return 0;
    auto *instance = *reinterpret_cast<const uint8_t *const *>(
        item + Offsets::OFF_ITEM_INSTANCE_BLOCK);
    if (instance == nullptr)
        return 0;
    return static_cast<int>(*reinterpret_cast<const uint32_t *>(
        instance + Offsets::OFF_INSTANCE_BLOCK_ITEM_ID));
}

namespace {

// `C_Item.GetItemID(itemLocation)` — itemID of the item at the given
// equipment-pane or bag slot, or nil for an empty slot / invalid arg.
// Routes through `Item::Location::Resolve` → CGItem → instance block
// → itemID.
int __cdecl Script_C_Item_GetItemID(void *L) {
    if (!Item::Location::IsLocationArg(L, 1))
        return Game::Lua::Error(L, "Usage: C_Item.GetItemID(itemLocation)");

    const int itemID = FromCGItem(Item::Location::Resolve(L, 1));
    if (itemID == 0) {
        Game::Lua::PushNil(L);
        return 1;
    }
    Game::Lua::PushNumber(L, static_cast<double>(itemID));
    return 1;
}

using Guid2HexString_t = void (__cdecl *)(uint32_t lo, uint32_t hi, char *buf);

// `C_Item.GetItemGUID(itemLocation)` — stable per-instance identifier
// as `"0xHHHHHHHHHHHHHHHH"` (engine GUID hex format), or nil for an
// empty slot / invalid arg. Pairs with `Item::Location::Resolve`'s
// string-GUID branch so addons can capture a reference here and feed
// it back to any `C_Item.*` accessor that takes an `itemLocation`.
int __cdecl Script_C_Item_GetItemGUID(void *L) {
    if (!Item::Location::IsLocationArg(L, 1))
        return Game::Lua::Error(L, "Usage: C_Item.GetItemGUID(itemLocation)");

    const uint8_t *item = Item::Location::Resolve(L, 1);
    if (item == nullptr) {
        Game::Lua::PushNil(L);
        return 1;
    }
    auto *instance = *reinterpret_cast<const uint8_t *const *>(
        item + Offsets::OFF_ITEM_INSTANCE_BLOCK);
    if (instance == nullptr) {
        Game::Lua::PushNil(L);
        return 1;
    }
    const uint64_t guid = *reinterpret_cast<const uint64_t *>(
        instance + Offsets::OFF_INSTANCE_BLOCK_GUID);
    if (guid == 0) {
        Game::Lua::PushNil(L);
        return 1;
    }

    // Engine formatter writes "0x" + 16 uppercase hex + NUL = 19 bytes.
    char buf[20] = {0};
    auto fn = reinterpret_cast<Guid2HexString_t>(Offsets::FUN_GUID_TO_HEXSTRING);
    fn(static_cast<uint32_t>(guid), static_cast<uint32_t>(guid >> 32), buf);
    Game::Lua::PushString(L, buf);
    return 1;
}

void RegisterLuaFunctions() {
    Game::Lua::RegisterTableFunction("C_Item", "GetItemID", &Script_C_Item_GetItemID);
    Game::Lua::RegisterTableFunction("C_Item", "GetItemGUID", &Script_C_Item_GetItemGUID);
}

const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace
} // namespace Item::ID
