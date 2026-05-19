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

#pragma once

#include <cstdint>

namespace Item::Data {

// Warm the item-stats cache for `itemID`. If already cached this is a
// no-op (caller can read item info immediately). If not cached,
// triggers the engine's `SMSG_ITEM_QUERY_SINGLE_RESPONSE` round-trip
// and fires `GET_ITEM_INFO_RECEIVED` when the response arrives.
//
// This is the **implicit** warmup path used by our `Script_GetItemInfo`
// hook (so 3.3.5's `GetItemInfo` becomes transparent like 5.x+). For
// the modern explicit-request semantics that fires
// `ITEM_DATA_LOAD_RESULT`, use `C_Item.RequestLoadItemData(ByID)` from
// Lua.
void WarmCache(uint32_t itemID);

// Hook for `Script_GetItemInfo` (`FUN_SCRIPT_GET_ITEM_INFO`). Pre-warms
// the cache from arg 1 (number or `"item:NNN..."` string), then runs
// the original. The original still returns nil this call for cache
// misses, but the query is now in flight; subsequent calls return
// valid data and `GET_ITEM_INFO_RECEIVED` fires when ready.
using Script_GetItemInfo_t = int(__cdecl *)(void *L);
extern Script_GetItemInfo_t Script_GetItemInfo_o;
int __cdecl Script_GetItemInfo_h(void *L);

} // namespace Item::Data
