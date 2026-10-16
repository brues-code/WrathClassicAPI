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

#include "Game.h"
#include "Offsets.h"
#include "event/Custom.h"
#include "item/Arg.h"
#include "item/Data.h"
#include "item/ID.h"
#include "item/Location.h"
#include "net/Packet.h"
#include "tick/FrameTick.h"

#include <cstdint>
#include <unordered_map>
#include <vector>

namespace Item::Data {

namespace {

// `DBCache<ItemStats_C>::GetRecord` — `__thiscall(this, itemID, *guid,
// callback, param, unique)`. See Offsets.h.
using GetItemRecord_t = const uint8_t *(__thiscall *)(void *cache, uint32_t itemID,
                                                      const uint64_t *guid, void *callback,
                                                      void *param, int unique);

// Two events fire on item-data arrival; they are **mutually exclusive**
// based on what triggered the cache fill. Mirrors ClassicAPI's split:
//
//   `GET_ITEM_INFO_RECEIVED(itemID, success)` — fires for **implicit /
//       transparent** cache fills: `GetItemInfo` warmup, hyperlink
//       hover, chat-link resolution, etc. The event most existing
//       addons listen to.
//
//   `ITEM_DATA_LOAD_RESULT(itemID, success)`  — fires for **explicit**
//       request paths only: `C_Item.RequestLoadItemData(ByID)` and
//       friends. The completion event for callers who explicitly asked
//       the modern Item-data API to load a specific item.
//
// We inject them via `Event::Custom::AutoReserve` at static-init time;
// they become live after the GameUI post-hook calls
// `Event::Custom::RegisterReservedEvents`.
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

void FireByTag(uint32_t itemID, bool success, bool implicit) {
    if (implicit)
        FireGetItemInfoReceived(static_cast<int>(itemID), success ? 1 : 0);
    else
        FireItemDataLoadResult(static_cast<int>(itemID), success ? 1 : 0);
}

// Item-load callback — `__cdecl(itemID, guid, param, success)`, 4 args /
// 16 bytes, caller-cleaned (FUN_0067CBD0's dispatch: PUSH success; PUSH
// param; PUSH guidPtr; PUSH itemID; CALL EDX; ADD ESP, 0x10). `success`
// is a C++ bool, so only its low byte is defined.
using ItemLoadCallback_t = void(__cdecl *)(uint32_t itemID, const uint64_t *guid,
                                           void *param, int success);

// Calls `DBCache<ItemStats_C>::GetRecord`. With `callback == nullptr`,
// performs only the hash-table lookup; returns the cached record or
// nullptr, no query. With a callback, also attaches it to the entry and,
// for a new entry, queries the server. This is the ONE primitive; every
// path into the cache goes through it.
const uint8_t *CacheFetch(uint32_t itemID, ItemLoadCallback_t callback) {
    auto fn = reinterpret_cast<GetItemRecord_t>(Offsets::FUN_DBCACHE_ITEMSTATS_GET_RECORD);
    auto *cache = reinterpret_cast<void *>(Offsets::VAR_ITEMDB_CACHE);
    const uint64_t zeroGuid = 0;
    void *cb = reinterpret_cast<void *>(callback);
    return fn(cache, itemID, &zeroGuid, cb, nullptr, /*unique=*/1);
}

bool InWorld() {
    return *reinterpret_cast<const volatile uint8_t *>(
               static_cast<uintptr_t>(Offsets::VAR_IN_WORLD)) != 0;
}

// --- Requests we owe a completion event for -------------------------------
//
// The cache does the work: it throttles its own queries (see Offsets.h)
// and calls our callback when the server answers, with success or with
// the server's "no such item" reply. The server answers every query it
// processes, so the callback is the single completion path.
//
// The one thing the cache can't handle is a lost query. The server drops
// item queries that arrive while the player isn't in the world (login
// and every loading screen), and the cache never asks again for an entry
// it already has. So:
//
//   - A request made outside the world waits here, unsent, and goes out
//     on the next frame in the world.
//   - A query sent just before a loading screen can still reach the server
//     after it has taken the player out of the world. On the first frame
//     back in the world, anything sent and still not loaded is asked for
//     again with a bare CMSG_ITEM_QUERY_SINGLE; the response fills the
//     existing entry and runs its callbacks.

struct Request {
    bool implicit; // true: fire GET_ITEM_INFO_RECEIVED; false: ITEM_DATA_LOAD_RESULT
    bool sent;     // our callback is attached and the cache has the query
};

std::unordered_map<uint32_t, Request> g_requests;
bool g_wasInWorld = false;
// Have we requested the player's carried items this session? The engine
// doesn't query bag item stats on enter-world or bag draw, so on a cold
// cache the bag and paperdoll UI shows "?" until something else asks.
bool g_ownedPrefetched = false;

// Fires and forgets the request for `itemID`, if we owe one. Erases before
// firing, so an OnEvent handler that requests the item again starts fresh.
void Complete(uint32_t itemID, bool success) {
    const auto it = g_requests.find(itemID);
    if (it == g_requests.end())
        return;
    const bool implicit = it->second.implicit;
    g_requests.erase(it);
    FireByTag(itemID, success, implicit);
}

void __cdecl OnItemLoaded(uint32_t itemID, const uint64_t *guid, void *param, int success) {
    (void)guid;
    (void)param;
    Complete(itemID, (success & 0xFF) != 0);
}

void Send(uint32_t itemID) {
    g_requests[itemID].sent = true;
    // Loaded between the request and now (a response that was already on
    // its way): the cache won't call back for a loaded entry.
    if (CacheFetch(itemID, &OnItemLoaded) != nullptr)
        Complete(itemID, true);
}

// Records an item we owe a completion event for, and hands it to the cache
// once the player is in the world.
void Track(uint32_t itemID, bool implicit) {
    if (CacheFetch(itemID, nullptr) != nullptr) {
        // Already loaded. Explicit RequestLoadItemData fires its
        // completion event synchronously (modern API contract). Implicit
        // warmup mirrors the engine's already-loaded shortcut, which does
        // not run a callback — so a cache hit fires nothing.
        if (!implicit)
            FireItemDataLoadResult(static_cast<int>(itemID), 1);
        return;
    }
    const auto [it, inserted] = g_requests.try_emplace(itemID, Request{implicit, false});
    // An explicit request supersedes a pending implicit one so the explicit
    // caller's ITEM_DATA_LOAD_RESULT is honored (one event per fill).
    if (!implicit)
        it->second.implicit = false;
    if (!it->second.sent && InWorld())
        Send(itemID);
}

std::vector<uint32_t> RequestIDs(bool sent) {
    std::vector<uint32_t> ids;
    for (const auto &[itemID, request] : g_requests)
        if (request.sent == sent)
            ids.push_back(itemID);
    return ids;
}

void ResendLost() {
    for (const uint32_t itemID : RequestIDs(/*sent=*/true)) {
        if (CacheFetch(itemID, nullptr) != nullptr)
            Complete(itemID, true);
        else
            Net::Packet::SendUint32(Offsets::OPCODE_CMSG_ITEM_QUERY_SINGLE, itemID);
    }
}

void SendWaiting() {
    for (const uint32_t itemID : RequestIDs(/*sent=*/false))
        Send(itemID);
}

void TrackID(uint32_t itemID) {
    if (itemID != 0)
        Track(itemID, /*implicit=*/true);
}

// Equipment + backpack + the four equipped bags, from the engine inventory
// helpers in `Item::Location`.
void PrefetchOwned() {
    for (int slot = Offsets::EQUIPMENT_SLOT_FIRST; slot <= Offsets::EQUIPMENT_SLOT_LAST; ++slot) {
        const uint8_t *item = Item::Location::ResolveEquipmentSlot(slot);
        if (item != nullptr)
            TrackID(static_cast<uint32_t>(Item::ID::FromCGItem(item)));
    }
    for (int bagID = 0; bagID <= 4; ++bagID) {
        const int n = Item::Location::GetBagNumSlots(bagID);
        for (int slot = 1; slot <= n; ++slot) {
            const uint8_t *item = Item::Location::ResolveBagSlot(bagID, slot);
            if (item != nullptr)
                TrackID(static_cast<uint32_t>(Item::ID::FromCGItem(item)));
        }
    }
}

// On the first frame in the world: re-ask for queries a loading screen
// swallowed, then the carried items, then everything that waited. Carried
// items go ahead of an addon's backlog so they fit in the cache's first
// throttle window.
void OnTick() {
    const bool inWorld = InWorld();
    const bool entered = inWorld && !g_wasInWorld;
    g_wasInWorld = inWorld;
    if (!inWorld)
        return;
    if (entered)
        ResendLost();
    // Gated on a live inventory manager so an early frame retries instead
    // of latching an empty walk.
    if (!g_ownedPrefetched && Item::Location::PlayerInventoryManager() != nullptr) {
        PrefetchOwned();
        g_ownedPrefetched = true;
    }
    if (entered)
        SendWaiting();
}

const Tick::FrameTick::AutoSubscribe _tickSub{&OnTick};

// The Lua state is going away (/reload or logout), and with it every
// listener for the events we owe. A callback still attached to a cache
// entry finds nothing here and does nothing; a request for the same item
// from the new UI reuses it.
void PrepareForReload() {
    g_requests.clear();
    g_ownedPrefetched = false;
}

const Game::ReloadAutoRegister _reload{&PrepareForReload};

int __cdecl Script_IsItemDataCachedByID(void *L) {
    const int itemID = Item::Arg::ResolveItemID(L, 1);
    const bool cached =
        (itemID > 0) && (CacheFetch(static_cast<uint32_t>(itemID), nullptr) != nullptr);
    Game::Lua::PushBool(L, cached);
    return 1;
}

// Resolves an itemLocation arg at stack idx to the itemID by walking
// `Item::Location::Resolve` -> CGItem -> instance block (+0x08) -> itemID
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

// Explicit-request path: completion fires ITEM_DATA_LOAD_RESULT. Returns
// false only for an argument that names no item.
int RequestExplicit(void *L, int itemID) {
    if (itemID > 0)
        Track(static_cast<uint32_t>(itemID), /*implicit=*/false);
    Game::Lua::PushBoolean(L, itemID > 0 ? 1 : 0);
    return 1;
}

int __cdecl Script_RequestLoadItemDataByID(void *L) {
    return RequestExplicit(L, Item::Arg::ResolveItemID(L, 1));
}

int __cdecl Script_RequestLoadItemData(void *L) {
    if (!Item::Location::IsLocationArg(L, 1))
        return Game::Lua::Error(L, "Usage: C_Item.RequestLoadItemData(itemLocation)");
    return RequestExplicit(L, ResolveLocationToItemID(L, 1));
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

// Public API — see Data.h. Implicit (transparent) warmup path used by the
// `Script_GetItemInfo` hook and every `GetItem*` getter's cache-miss
// warmup. A cache hit resolves for free; a miss goes to the cache, and
// GET_ITEM_INFO_RECEIVED fires when the response lands.
void WarmCache(uint32_t itemID) { Track(itemID, /*implicit=*/true); }

// Hook for `Script_GetItemInfo`. Pre-warms the cache from arg 1, then
// dispatches to the original. The original still returns nil for cache
// misses (vanilla behavior) but a query is now on its way, so subsequent
// calls return valid data and GET_ITEM_INFO_RECEIVED fires on response
// arrival.
Script_GetItemInfo_t Script_GetItemInfo_o = nullptr;

int __cdecl Script_GetItemInfo_h(void *L) {
    const int itemID = Item::Arg::ResolveItemID(L, 1);
    if (itemID > 0)
        WarmCache(static_cast<uint32_t>(itemID));
    return Script_GetItemInfo_o(L);
}

namespace {

// Auto-warm the item cache on `GetItemInfo(uncached_id)` calls so
// subsequent calls return valid data and `GET_ITEM_INFO_RECEIVED` fires
// when the response arrives. Without this, vanilla `GetItemInfo` returns
// nil for misses and never fires a query, forcing addons to roll their
// own warmup hacks.
const Game::HookAutoRegister _hookreg{
    Offsets::FUN_SCRIPT_GET_ITEM_INFO,
    reinterpret_cast<void *>(&Script_GetItemInfo_h),
    reinterpret_cast<void **>(&Script_GetItemInfo_o)};

} // namespace

} // namespace Item::Data
