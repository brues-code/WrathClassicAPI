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

// `C_Container.MoveItem(srcBag, srcSlot, dstBag, dstSlot, count)` — splits
// `count` off a stack and places it in one call, cursor uncontrolled.
// Otherwise this takes a SplitContainerItem plus a PickupContainerItem that
// have to land in the same hardware event, and leaves the cursor holding the
// split if anything between them fails.
//
// Asking for the whole stack works and moves it — see `Item::Swap::MoveCount`
// for why that takes a different route on the wire.
//
// Returns true once the request is sent, false if it couldn't be built. As
// with SwapItems, true says the request went out, not that it was accepted.

#include "Game.h"
#include "item/Swap.h"

namespace Container::MoveItem {

namespace {

int __cdecl Script_C_Container_MoveItem(void *L) {
    if (!Game::Lua::IsNumber(L, 1) || !Game::Lua::IsNumber(L, 2) ||
        !Game::Lua::IsNumber(L, 3) || !Game::Lua::IsNumber(L, 4) ||
        !Game::Lua::IsNumber(L, 5)) {
        Game::Lua::Error(L,
            "Usage: C_Container.MoveItem(srcBag, srcSlot, dstBag, dstSlot, count)");
        return 0;
    }
    const int srcBag = static_cast<int>(Game::Lua::ToNumber(L, 1));
    const int srcSlot = static_cast<int>(Game::Lua::ToNumber(L, 2));
    const int dstBag = static_cast<int>(Game::Lua::ToNumber(L, 3));
    const int dstSlot = static_cast<int>(Game::Lua::ToNumber(L, 4));
    const int count = static_cast<int>(Game::Lua::ToNumber(L, 5));

    Game::Lua::PushBool(
        L, Item::Swap::MoveCount(srcBag, srcSlot, dstBag, dstSlot, count));
    return 1;
}

void RegisterLuaFunctions() {
    Game::Lua::RegisterTableFunction("C_Container", "MoveItem",
                                     &Script_C_Container_MoveItem);
}

const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace

} // namespace Container::MoveItem
