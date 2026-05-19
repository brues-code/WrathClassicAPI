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

void RegisterLuaFunctions() {
    Game::Lua::RegisterTableFunction("C_Item", "GetItemID", &Script_C_Item_GetItemID);
}

const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace
} // namespace Item::ID
