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

// `C_Item.GetItemLocation(itemGUID)` — modern WoW's lookup that
// goes from a GUID string back to an `ItemLocation` table. The
// engine's `ObjectMgr::Get` resolves the GUID to the CGItem
// pointer (handled by our existing `Item::Location::Resolve`'s
// string branch); we then walk the local player's inventory
// slots comparing pointer identity to find where that item lives,
// and emit the matching `{equipmentSlotIndex = N}` or
// `{bagID, slotIndex}` table that mirrors the other modern
// `C_Item.*` accessors.
//
// Returns nil if:
//   * the GUID string is malformed / resolves to nothing
//   * the GUID resolves but the item isn't in the player's
//     inventory (e.g. it's a trade-window item, an auction
//     listing, or a recently destroyed item still in the ObjMgr)
//
// This matches modern semantics: GetItemLocation only succeeds
// for items the player actually holds.

#include "Game.h"
#include "Offsets.h"
#include "item/Location.h"

#include <cstdint>

namespace Item::GetLocation {

namespace {

// Upper bound for per-bag slot iteration. 3.3.5's largest bag is
// 22 slots (Wormhole Generator: Northrend doesn't qualify; the
// real upper bound is the 22-slot frostweave-style bags). The
// engine bounds-checks each `GetItemBySlot` call internally and
// returns nullptr for out-of-range slots, so iterating past a
// bag's actual size is harmless — just wastes a few iterations.
constexpr int kMaxBagSlots = 36;

void PushEquipmentSlot(void *L, int slot1Based) {
    Game::Lua::NewTable(L);
    Game::Lua::SetFieldNumber(L, "equipmentSlotIndex",
                              static_cast<double>(slot1Based));
}

void PushBagSlot(void *L, int bagID, int slotIndex) {
    Game::Lua::NewTable(L);
    Game::Lua::SetFieldNumber(L, "bagID", static_cast<double>(bagID));
    Game::Lua::SetFieldNumber(L, "slotIndex",
                              static_cast<double>(slotIndex));
}

int __cdecl Script_GetItemLocation(void *L) {
    if (Game::Lua::Type(L, 1) != Game::Lua::TYPE_STRING)
        return Game::Lua::Error(L, "Usage: C_Item.GetItemLocation(itemGUID)");

    // Reuse the string-branch of Item::Location::Resolve — runs
    // `HexString2Guid` + `ObjectMgr::Get` with the ITEM type mask.
    // Returns nullptr for malformed input or non-item GUIDs.
    const uint8_t *target = Item::Location::Resolve(L, 1);
    if (target == nullptr)
        return 0;

    // Character-pane equipment (paperdoll). Slots 1..19 are the
    // visible equipment slots; bags themselves (slots 20..23)
    // resolve through the bagID=1..4 path below, not here.
    for (int slot = Offsets::EQUIPMENT_SLOT_FIRST;
         slot <= Offsets::EQUIPMENT_SLOT_LAST; ++slot) {
        if (Item::Location::ResolveEquipmentSlot(slot) == target) {
            PushEquipmentSlot(L, slot);
            return 1;
        }
    }

    // Backpack (bagID 0, 16 slots) + four equipped bags (bagID 1..4).
    for (int bagID = 0; bagID <= 4; ++bagID) {
        const int upper = (bagID == 0) ? 16 : kMaxBagSlots;
        for (int slotIndex = 1; slotIndex <= upper; ++slotIndex) {
            if (Item::Location::ResolveBagSlot(bagID, slotIndex) == target) {
                PushBagSlot(L, bagID, slotIndex);
                return 1;
            }
        }
    }

    return 0; // GUID resolves to a CGItem but item isn't in inventory
}

void RegisterLuaFunctions() {
    Game::Lua::RegisterTableFunction("C_Item", "GetItemLocation",
                                     &Script_GetItemLocation);
}

const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace
} // namespace Item::GetLocation
