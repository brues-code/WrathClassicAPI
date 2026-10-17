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

// `C_Container.GetContainerItemRepairCost(bagID, slot)` — copper to repair the
// item in a bag slot. Bag-side twin of GetInventoryItemRepairCost; ported from
// ClassicAPI's src/container/RepairCost.cpp. Same engine call as the bag item
// tooltip's repair line (GameTooltip:SetBagItem's repairCost return).

#include "Game.h"
#include "item/Location.h"
#include "item/RepairCost.h"

namespace Container::RepairCost {

namespace {

int __cdecl Script_C_Container_GetContainerItemRepairCost(void *L) {
    if (!Game::Lua::IsNumber(L, 1) || !Game::Lua::IsNumber(L, 2))
        return Game::Lua::Error(L, "Usage: C_Container.GetContainerItemRepairCost(bagID, slot)");
    const uint8_t *item = Item::Location::ResolveBagSlot(static_cast<int>(Game::Lua::ToNumber(L, 1)),
                                                         static_cast<int>(Game::Lua::ToNumber(L, 2)));
    Game::Lua::PushNumber(L, static_cast<double>(Item::RepairCost::Of(item)));
    return 1;
}

void RegisterLuaFunctions() {
    Game::Lua::RegisterTableFunction("C_Container", "GetContainerItemRepairCost",
                                     &Script_C_Container_GetContainerItemRepairCost);
}

const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace

} // namespace Container::RepairCost
