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

// `GetInventoryItemRepairCost(invSlot)` — copper to repair the player's
// equipped item in `invSlot`. Ported from ClassicAPI's src/item/RepairCost.cpp.
//
// The client only exposes this as GameTooltip:SetInventoryItem's third return.
// We compute it with the same engine call that return uses,
// CGMerchantInfo::GetRepairCost: the item's base cost, less the open
// merchant's reputation discount.

#include "item/RepairCost.h"

#include "Game.h"
#include "Offsets.h"
#include "item/Location.h"

#include <cstdint>

namespace Item::RepairCost {

uint32_t Of(const uint8_t *item) {
    using GetRepairCost_t = int32_t(__cdecl *)(const void *item);
    if (item == nullptr)
        return 0;
    return static_cast<uint32_t>(
        reinterpret_cast<GetRepairCost_t>(Offsets::FUN_MERCHANT_GET_REPAIR_COST)(item));
}

namespace {

int __cdecl Script_GetInventoryItemRepairCost(void *L) {
    if (!Game::Lua::IsNumber(L, 1))
        return Game::Lua::Error(L, "Usage: GetInventoryItemRepairCost(invSlot)");
    const uint8_t *item =
        Item::Location::ResolveEquipmentSlot(static_cast<int>(Game::Lua::ToNumber(L, 1)));
    Game::Lua::PushNumber(L, static_cast<double>(Of(item)));
    return 1;
}

void RegisterLuaFunctions() {
    Game::Lua::RegisterGlobalFunction("GetInventoryItemRepairCost",
                                      &Script_GetInventoryItemRepairCost);
}

const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace

} // namespace Item::RepairCost
