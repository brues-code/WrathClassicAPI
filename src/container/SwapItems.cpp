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

// `C_Container.SwapItems(srcBag, srcSlot, dstBag, dstSlot)` — swaps two bag
// slots in one call, without touching the cursor. There's no built-in
// equivalent: moving an item otherwise means driving the cursor with
// PickupContainerItem twice, which needs the two calls to land in the same
// hardware event and leaves the cursor holding the item if anything in
// between fails.
//
// Returns true once the request is sent, false if it couldn't be built (see
// `Item::Swap::Containers`). A true return says the request went out, not that
// the server accepted it — that arrives as BAG_UPDATE, or an error toast.

#include "Game.h"
#include "item/Swap.h"

namespace Container::SwapItems {

namespace {

int __cdecl Script_C_Container_SwapItems(void *L) {
    if (!Game::Lua::IsNumber(L, 1) || !Game::Lua::IsNumber(L, 2) ||
        !Game::Lua::IsNumber(L, 3) || !Game::Lua::IsNumber(L, 4)) {
        Game::Lua::Error(L,
            "Usage: C_Container.SwapItems(srcBag, srcSlot, dstBag, dstSlot)");
        return 0;
    }
    const int srcBag = static_cast<int>(Game::Lua::ToNumber(L, 1));
    const int srcSlot = static_cast<int>(Game::Lua::ToNumber(L, 2));
    const int dstBag = static_cast<int>(Game::Lua::ToNumber(L, 3));
    const int dstSlot = static_cast<int>(Game::Lua::ToNumber(L, 4));

    Game::Lua::PushBool(L, Item::Swap::Containers(srcBag, srcSlot, dstBag, dstSlot));
    return 1;
}

void RegisterLuaFunctions() {
    Game::Lua::RegisterTableFunction("C_Container", "SwapItems",
                                     &Script_C_Container_SwapItems);
}

const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace

} // namespace Container::SwapItems
