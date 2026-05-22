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

#include "Game.h"
#include "Offsets.h"
#include "event/Custom.h"
#include "item/Arg.h"
#include "item/Data.h"
#include "item/Location.h"

#include <cstdint>

namespace Item::Data {

namespace {

// `DBCache_ItemStats_C::GetRecord` — `__thiscall(this, itemID, *guid,
// callback, userData, unused)`. With `callback == nullptr`, hash-table
// lookup only (returns cached record or NULL). With non-null callback,
// also kicks off `SMSG_ITEM_QUERY_SINGLE` for uncached items; the
// engine fills the cache asynchronously and invokes the callback when
// the response arrives.
using GetItemRecord_t = const uint8_t *(__thiscall *)(void *cache, uint32_t itemID,
                                                      const uint64_t *guid, void *callback,
                                                      void *userData, int unused);

// Two events fire on item-data arrival; they are **mutually exclusive**
// based on what triggered the cache fill. Mirrors ClassicAPI's split:
//
//   `GET_ITEM_INFO_RECEIVED(itemID, success)` — added in 5.4. Fires for
//       **implicit / transparent** cache fills: `GetItemInfo` warmup,
//       hyperlink hover, chat-link resolution, etc. The event most
//       existing addons listen to.
//
//   `ITEM_DATA_LOAD_RESULT(itemID, success)`  — added in 8.0. Fires for
//       **explicit** request paths only: `C_Item.RequestLoadItemData(ByID)`
//       and friends. The completion event for callers who explicitly
//       asked the modern Item-data API to load a specific item.
//
// Neither exists in 3.3.5 natively (verified by string scan in the
// stripped binary). We inject them via `Event::Custom::AutoReserve`
// at static-init time; they become live after the GameUI post-hook
// calls `Event::Custom::RegisterReservedEvents`.
constexpr const char *kGetItemInfoReceived = "GET_ITEM_INFO_RECEIVED";
constexpr const char *kItemDataLoadResult = "ITEM_DATA_LOAD_RESULT";
const Event::Custom::AutoReserve _reserveGetItemInfoReceived{kGetItemInfoReceived};
const Event::Custom::AutoReserve _reserveItemDataLoadResult{kItemDataLoadResult};

void FireGetItemInfoReceived(int itemID, int success) {
    const int eventID = Event::Custom::Lookup(kGetItemInfoReceived);
    Event::Custom::Fire(eventID, "%d%d", itemID, success);
}

void FireItemDataLoadResult(int itemID, int success) {
    const int eventID = Event::Custom::Lookup(kItemDataLoadResult);
    Event::Custom::Fire(eventID, "%d%d", itemID, success);
}

// Engine callback for **implicit** cache fills (transparent warmup
// triggered by our `Script_GetItemInfo` hook). Fires only the broad
// `GET_ITEM_INFO_RECEIVED` event — `ITEM_DATA_LOAD_RESULT` is reserved
// for explicit `RequestLoadItemData(ByID)` paths to match modern API
// semantics.
//
// Calling convention is __cdecl, 4 args / 16 bytes. Verified at
// `FUN_0067CBD0`'s callback dispatch (0x0067CD3F):
//   PUSH 1; PUSH userData; PUSH guidPtr; PUSH itemID;
//   CALL EDX;
//   ADD ESP, 0x10        <-- CALLER cleans up = __cdecl, not __stdcall
// Signature: `void(__cdecl *)(uint32_t itemID, const uint64_t *guid,
//                             void *userData, int success)`.
// A `__stdcall ret 16` callback into this site double-pops by 16
// (callee ret 16 + caller add esp 16), each invocation lifts ESP by
// 16 bytes; the loop's `param_1` stream pointer comes back as garbage
// and the next FUN_0098D910 call crashes in FUN_0047B3C0 reading
// [esi+0x14].
void __cdecl ItemLoadCallback_Implicit(uint32_t itemID, const uint64_t *guid,
                                       void *userData, int success) {
    (void)guid;
    (void)userData;
    FireGetItemInfoReceived(static_cast<int>(itemID), success != 0 ? 1 : 0);
}

// Engine callback for **explicit** cache fills
// (`C_Item.RequestLoadItemData(ByID)`). Fires ITEM_DATA_LOAD_RESULT
// only.
void __cdecl ItemLoadCallback_Explicit(uint32_t itemID, const uint64_t *guid,
                                       void *userData, int success) {
    (void)guid;
    (void)userData;
    FireItemDataLoadResult(static_cast<int>(itemID), success != 0 ? 1 : 0);
}

using ItemLoadCallback_t = void(__cdecl *)(uint32_t itemID, const uint64_t *guid,
                                           void *userData, int success);

// Calls `DBCache_ItemStats_C::GetRecord`. With `callback == nullptr`,
// performs only the hash-table lookup; returns the cached record or
// nullptr. With a non-null callback, also kicks off the
// `SMSG_ITEM_QUERY_SINGLE` if the item isn't cached — the engine fills
// the cache asynchronously and invokes
// `callback(itemID, guid, userData, success)` from the batch handler
// (FUN_0067CBD0). The itemID is supplied as the first arg directly,
// so we don't need to smuggle anything through `userData`.
const uint8_t *CacheFetch(uint32_t itemID, ItemLoadCallback_t callback) {
    auto fn = reinterpret_cast<GetItemRecord_t>(Offsets::FUN_DBCACHE_ITEMSTATS_GET_RECORD);
    auto *cache = reinterpret_cast<void *>(Offsets::VAR_ITEMDB_CACHE);
    const uint64_t zeroGuid = 0;
    void *cb = reinterpret_cast<void *>(callback);
    return fn(cache, itemID, &zeroGuid, cb, nullptr, 0);
}

int __cdecl Script_IsItemDataCachedByID(void *L) {
    const int itemID = Item::Arg::ResolveItemID(L, 1);
    const bool cached =
        (itemID > 0) && (CacheFetch(static_cast<uint32_t>(itemID), nullptr) != nullptr);
    Game::Lua::PushBool(L, cached);
    return 1;
}

// Resolves an itemLocation arg at stack idx to the itemID by walking
// `Item::Location::Resolve` → CGItem → instance block (+0x08) → itemID
// (+0x0C). Returns 0 if the slot is empty or the table is malformed.
int ResolveLocationToItemID(void *L, int idx) {
    const uint8_t *item = Item::Location::Resolve(L, idx);
    if (item == nullptr)
        return 0;
    auto *instance = *reinterpret_cast<const uint8_t *const *>(
        item + Offsets::OFF_ITEM_INSTANCE_BLOCK);
    if (instance == nullptr)
        return 0;
    return static_cast<int>(*reinterpret_cast<const uint32_t *>(
        instance + Offsets::OFF_INSTANCE_BLOCK_ITEM_ID));
}

int __cdecl Script_IsItemDataCached(void *L) {
    if (!Item::Location::IsLocationArg(L, 1))
        return Game::Lua::Error(L, "Usage: C_Item.IsItemDataCached(itemLocation)");
    const int itemID = ResolveLocationToItemID(L, 1);
    const bool cached =
        (itemID > 0) && (CacheFetch(static_cast<uint32_t>(itemID), nullptr) != nullptr);
    Game::Lua::PushBool(L, cached);
    return 1;
}

// Explicit-request path: kicks off the cache fill via the explicit
// callback (which fires ITEM_DATA_LOAD_RESULT). If the item is already
// cached, the engine won't invoke our callback — synthesize the event
// so addons get the same notification regardless of cache state,
// matching modern `C_Item.RequestLoadItemData(ByID)` semantics.
void RequestAndMaybeNotify(uint32_t itemID) {
    const bool wasCached = (CacheFetch(itemID, nullptr) != nullptr);
    CacheFetch(itemID, &ItemLoadCallback_Explicit);
    if (wasCached)
        FireItemDataLoadResult(static_cast<int>(itemID), 1);
}

int __cdecl Script_RequestLoadItemDataByID(void *L) {
    const int itemID = Item::Arg::ResolveItemID(L, 1);
    if (itemID <= 0) {
        Game::Lua::PushBoolean(L, 0);
        return 1;
    }
    RequestAndMaybeNotify(static_cast<uint32_t>(itemID));
    Game::Lua::PushBoolean(L, 1);
    return 1;
}

int __cdecl Script_RequestLoadItemData(void *L) {
    if (!Item::Location::IsLocationArg(L, 1))
        return Game::Lua::Error(L, "Usage: C_Item.RequestLoadItemData(itemLocation)");
    const int itemID = ResolveLocationToItemID(L, 1);
    if (itemID <= 0) {
        Game::Lua::PushBoolean(L, 0);
        return 1;
    }
    RequestAndMaybeNotify(static_cast<uint32_t>(itemID));
    Game::Lua::PushBoolean(L, 1);
    return 1;
}

void RegisterLuaFunctions() {
    Game::Lua::RegisterTableFunction("C_Item", "IsItemDataCachedByID",
                                     &Script_IsItemDataCachedByID);
    Game::Lua::RegisterTableFunction("C_Item", "RequestLoadItemDataByID",
                                     &Script_RequestLoadItemDataByID);
    Game::Lua::RegisterTableFunction("C_Item", "IsItemDataCached",
                                     &Script_IsItemDataCached);
    Game::Lua::RegisterTableFunction("C_Item", "RequestLoadItemData",
                                     &Script_RequestLoadItemData);
    // String GUID variants (`"0xHHHHHHHHLLLLLLLL"`) deferred — needs
    // the engine's CGItemMgr GUID resolver re-derived for 3.3.5.
}

const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace

// Public API — see Data.h.
void WarmCache(uint32_t itemID) {
    CacheFetch(itemID, &ItemLoadCallback_Implicit);
}

// Hook for `Script_GetItemInfo`. Pre-warms the cache from arg 1, then
// dispatches to the original. The original still returns nil for
// cache misses (vanilla behavior) but a query is now in flight, so
// subsequent calls will return valid data and GET_ITEM_INFO_RECEIVED
// will fire on response arrival.
Script_GetItemInfo_t Script_GetItemInfo_o = nullptr;

int __cdecl Script_GetItemInfo_h(void *L) {
    const int itemID = Item::Arg::ResolveItemID(L, 1);
    if (itemID > 0)
        WarmCache(static_cast<uint32_t>(itemID));
    return Script_GetItemInfo_o(L);
}

namespace {

// Auto-warm the item cache on `GetItemInfo(uncached_id)` calls so
// subsequent calls return valid data and `GET_ITEM_INFO_RECEIVED`
// fires when the response arrives — matches modern WoW (5.x+)
// behavior. Without this, vanilla `GetItemInfo` returns nil for
// misses and never fires a query, forcing addons to roll their own
// warmup hacks.
const Game::HookAutoRegister _hookreg{
    Offsets::FUN_SCRIPT_GET_ITEM_INFO,
    reinterpret_cast<void *>(&Script_GetItemInfo_h),
    reinterpret_cast<void **>(&Script_GetItemInfo_o)};

} // namespace

} // namespace Item::Data
