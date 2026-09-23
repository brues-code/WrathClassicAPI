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

#pragma once

#include <cstdint>

namespace Item::Location {

// Resolves an item-location arg at Lua stack `idx` to a `CGItem *`
// pointer (returned as raw bytes for use with the `OFF_*_INSTANCE_BLOCK`
// offsets). Accepts location tables only in this build:
//   - `{ equipmentSlotIndex = N }`     — paperdoll slot 1..19
//   - `{ bagID = B, slotIndex = S }`   — bag (0=backpack, 1..4=bags) +
//                                          slot (1-based)
//
// Returns `nullptr` for missing/invalid args, empty slots, or
// unsupported shapes. The GUID-string form (`"0xHHHHHHHHLLLLLLLL"`)
// is **not yet supported** — needs the engine's CGItemMgr GUID
// resolver re-derived for 3.3.5; deferred to a follow-up.
//
// Does not raise a Lua error — callers should validate the argument
// type beforehand with `IsLocationArg` if they want a typed error.
//
// Bag-slot resolution stomps Lua stack positions 1 and 2 (the engine's
// PackBagSlot helper reads them via lua_tonumber). Only safe to call
// from inside a Lua callback.
const uint8_t *Resolve(void *L, int idx);

// Type-check helper: returns true if the value at `idx` could be a
// valid location arg. In this build, that's a table (string GUID
// form deferred). Does not dereference the value.
bool IsLocationArg(void *L, int idx);

// Resolves a 1-based character-pane equipment slot directly to a
// `CGItem *`. Player-only — walks the local player's invMgr via
// ResolveUnitToken("player") + OFF_PLAYER_INVENTORY_MANAGER. Returns
// nullptr for out-of-range slots or empty equipment. No Lua stack
// interaction — safe outside a callback.
const uint8_t *ResolveEquipmentSlot(int slot1Based);

// Resolves the bag ITEM occupying a bag slot — the container object itself,
// not anything inside it. `bagID 1..4` are the equipped bags, `5..11` the
// bank's. Returns nullptr for a non-bag bagID or an empty bag slot.
//
// Deliberately not `ResolveEquipmentSlot`: bags live past
// `EQUIPMENT_SLOT_LAST` in the same linear index space, and widening that
// helper's bounds would silently let every one of its callers address bag
// slots, which is not what "equipment slot" means there.
//
// Bank bags only resolve while the bank window is open — the client holds no
// synced copy of them before then.
const uint8_t *ResolveEquippedBag(int bagID);

// Resolves a `(bagID, slotIndex)` pair to a `CGItem *`. Pure
// resolution — does NOT touch the Lua stack and does NOT depend on
// any Lua-callback context. Safe to call from anywhere once the
// in-game world is loaded.
//
// bagID  0     → backpack (slotIndex 1..16)
// bagID -1     → bank, main window (1..28)
// bagID -2     → keyring (1..32)
// bagID  1..4  → equipped bag (1..bagItem->numSlots)
// bagID  5..11 → bank bag (1..bagItem->numSlots)
// anything else → nullptr
//
// Bank-side slots resolve only while the bank window is open.
const uint8_t *ResolveBagSlot(int bagID, int slotIndex);

// Returns the number of slots the container at `bagID` holds. The player's
// own ranges (backpack, bank, keyring) are the fixed width of their
// linear-slot range; a bag (`1..11`) reads `numSlots` off its `CGContainer`
// (via `CGItem::GetContainer`, vtable slot 10). Returns 0 if the bag slot is
// empty, the item isn't a container, or `bagID` is out of range. Same data
// the engine's `GetContainerNumSlots` Lua C function returns, exposed here
// for inventory walks.
int GetBagNumSlots(int bagID);

// Returns the local player's `CInventoryMgr *` (the player CGUnit +
// `OFF_PLAYER_INVENTORY_MANAGER`), or nullptr if the player object
// isn't resolvable yet (pre-login / loading screen). Pure engine call
// — no Lua stack. Used as an inventory-readiness gate for tick-time
// inventory walks (see `Item::Data`'s owned-item prefetch).
void *PlayerInventoryManager();

} // namespace Item::Location
