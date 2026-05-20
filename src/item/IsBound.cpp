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

// `C_Item.IsBound(itemLocation)` — returns `true` if the item is
// currently bound to the player in any way the modern API
// considers "bound": soulbound (regular BoP/BoE-after-equip) OR
// account-bound (heirloom). Modern `C_Item.IsBound` semantically
// is "the item can no longer leave the player's account," and
// heirlooms satisfy that even though they can still mail between
// the player's own characters.
//
// Two-part check:
//   1. Engine `CGItem::IsSoulbound` (`FUN_ITEM_IS_SOULBOUND`)
//      handles per-instance soulbound state, including the
//      uncommon case where an applied enchantment carries the
//      bind-on-apply flag. This is the same predicate the
//      tooltip builder uses to gate the bind-label line.
//   2. The proto flag at `stats_record + OFF_ITEMSTATS_FLAGS`
//      bit 27 (`ITEM_PROTO_FLAG_ACCOUNT_BOUND`) identifies
//      heirlooms. The engine consults this separately in its
//      tooltip path to decide between the "Soulbound" and
//      "Binds to Account" labels — but for our purposes, either
//      condition counts as `IsBound = true`.
//
// The pure-Lua compat layer in !!!ClassicAPI does this via a
// tooltip text scan ("Soulbound" / "Binds to Account" line
// detection). Going native lets us skip the tooltip frame
// allocation, text matching, and locale-sensitive string
// lookups.

#include "Game.h"
#include "Offsets.h"
#include "item/Location.h"

#include <cstdint>

namespace Item::IsBound {

namespace {

// `__thiscall(CGItem)` — Ghidra inferred fastcall (same register
// layout — ECX = this, no EDX). We mirror that here.
using IsSoulbound_t = bool(__fastcall *)(const uint8_t *item, void *edx);

// `DBCache_ItemStats_C::GetRecord` — same signature as in
// `src/item/Data.cpp`; we pass a null callback to do a pure
// lookup (no SMSG_ITEM_QUERY_SINGLE side effect — if the data
// isn't cached we return "not bound" rather than triggering a
// network request).
using GetItemRecord_t = const uint8_t *(__thiscall *)(void *cache, uint32_t itemID,
                                                      const uint64_t *guid, void *callback,
                                                      void *userData, int unused);

bool CallIsSoulbound(const uint8_t *item) {
    auto fn = reinterpret_cast<IsSoulbound_t>(
        static_cast<uintptr_t>(Offsets::FUN_ITEM_IS_SOULBOUND));
    return fn(item, nullptr);
}

// Returns the itemID for the CGItem, or 0 if the instance block
// is unset / the item is in a half-loaded state.
uint32_t ItemIDFromCGItem(const uint8_t *item) {
    if (item == nullptr)
        return 0;
    const uint8_t *instance = *reinterpret_cast<const uint8_t *const *>(
        item + Offsets::OFF_ITEM_INSTANCE_BLOCK);
    if (instance == nullptr)
        return 0;
    return *reinterpret_cast<const uint32_t *>(
        instance + Offsets::OFF_INSTANCE_BLOCK_ITEM_ID);
}

// True if the item TYPE is heirloom (proto flags bit 27). Hits
// the item-stats cache directly with no callback — if the record
// isn't loaded yet, returns false. Heirloom protos are core game
// data that's loaded early, so this almost never matters in
// practice; addons calling IsBound on uncached items can pair
// with `C_Item.RequestLoadItemData` and retry.
bool IsHeirloomByItemID(uint32_t itemID) {
    if (itemID == 0)
        return false;
    auto fn = reinterpret_cast<GetItemRecord_t>(
        static_cast<uintptr_t>(Offsets::FUN_DBCACHE_ITEMSTATS_GET_RECORD));
    auto *cache = reinterpret_cast<void *>(
        static_cast<uintptr_t>(Offsets::VAR_ITEMDB_CACHE));
    const uint64_t zeroGuid = 0;
    const uint8_t *record = fn(cache, itemID, &zeroGuid, nullptr, nullptr, 0);
    if (record == nullptr)
        return false;
    const uint32_t flags = *reinterpret_cast<const uint32_t *>(
        record + Offsets::OFF_ITEMSTATS_FLAGS);
    return (flags & Offsets::ITEM_PROTO_FLAG_ACCOUNT_BOUND) != 0;
}

int __cdecl Script_IsBound(void *L) {
    if (!Item::Location::IsLocationArg(L, 1))
        return Game::Lua::Error(L, "Usage: C_Item.IsBound(itemLocation)");

    const uint8_t *item = Item::Location::Resolve(L, 1);
    if (item == nullptr) {
        Game::Lua::PushBool(L, false);
        return 1;
    }

    bool bound = CallIsSoulbound(item);
    if (!bound)
        bound = IsHeirloomByItemID(ItemIDFromCGItem(item));

    Game::Lua::PushBool(L, bound);
    return 1;
}

void RegisterLuaFunctions() {
    Game::Lua::RegisterTableFunction("C_Item", "IsBound", &Script_IsBound);
}

const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace
} // namespace Item::IsBound
