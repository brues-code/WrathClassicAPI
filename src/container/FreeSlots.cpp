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

// `C_Container.CalculateTotalNumberOfFreeBagSlots()` — free slots across the
// backpack and the four equipped bags, counting only general-purpose bags (bag
// family 0), the same total FrameXML's CalculateTotalNumberOfFreeBagSlots
// builds from GetContainerNumFreeSlots.
//
// Reads what Script_GetContainerNumFreeSlots (0x005D7590) reads: the backpack's
// empty slots, and for each bag CGContainer_C::GetFreeSlots plus
// CGItem_C::GetBagFamily.

#include "Game.h"
#include "Offsets.h"
#include "item/Location.h"

#include <cstdint>

namespace Container::FreeSlots {

namespace {

// `__thiscall(this)`, no stack args.
using ContainerCount_t = int32_t(__fastcall *)(const void *container);

int __cdecl Script_CalculateTotalNumberOfFreeBagSlots(void *L) {
    int total = 0;
    const int backpackSlots = Item::Location::GetBagNumSlots(0);
    for (int slot = 1; slot <= backpackSlots; ++slot)
        if (Item::Location::ResolveBagSlot(0, slot) == nullptr)
            ++total;
    for (int bagID = 1; bagID <= 4; ++bagID) {
        const uint8_t *bag = Item::Location::ResolveEquippedBag(bagID);
        if (bag == nullptr ||
            reinterpret_cast<ContainerCount_t>(Offsets::FUN_ITEM_GET_BAG_FAMILY)(bag) != 0)
            continue;
        total += reinterpret_cast<ContainerCount_t>(Offsets::FUN_CONTAINER_GET_FREE_SLOTS)(bag);
    }
    Game::Lua::PushNumber(L, static_cast<double>(total));
    return 1;
}

void RegisterLuaFunctions() {
    Game::Lua::RegisterTableFunction("C_Container", "CalculateTotalNumberOfFreeBagSlots",
                                     &Script_CalculateTotalNumberOfFreeBagSlots);
}

const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace

} // namespace Container::FreeSlots
