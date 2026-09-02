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
#include "tick/FrameTick.h"

#include <cstdint>

#include <windows.h>

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

// Item-load callback — `__cdecl(itemID, guid, userData, success)`, 4
// args / 16 bytes, caller-cleaned (verified at FUN_0067CBD0's callback
// dispatch: PUSH 1; PUSH userData; PUSH guidPtr; PUSH itemID; CALL EDX;
// ADD ESP, 0x10). The itemID arrives as the first arg directly, so we
// never smuggle it through `userData`.
using ItemLoadCallback_t = void(__cdecl *)(uint32_t itemID, const uint64_t *guid,
                                           void *userData, int success);

// Calls `DBCache_ItemStats_C::GetRecord`. With `callback == nullptr`,
// performs only the hash-table lookup; returns the cached record or
// nullptr, no SMSG. With a non-null callback, also kicks off the
// `SMSG_ITEM_QUERY_SINGLE` for an uncached item. This is the ONE
// primitive; every path into the cache goes through it.
const uint8_t *CacheFetch(uint32_t itemID, ItemLoadCallback_t callback) {
    auto fn = reinterpret_cast<GetItemRecord_t>(Offsets::FUN_DBCACHE_ITEMSTATS_GET_RECORD);
    auto *cache = reinterpret_cast<void *>(Offsets::VAR_ITEMDB_CACHE);
    const uint64_t zeroGuid = 0;
    void *cb = reinterpret_cast<void *>(callback);
    return fn(cache, itemID, &zeroGuid, cb, nullptr, 0);
}

// GetTickCount is unsigned and wraps every ~49 days; subtraction in
// DWORD arithmetic gives the right elapsed value across wraparound.
DWORD Elapsed(DWORD start, DWORD now) { return now - start; }

// --- Pending-request queue + throttled issue -----------------------------
//
// The naive path — fire `SMSG_ITEM_QUERY_SINGLE` the instant any
// `GetItemInfo` miss or `RequestLoadItemData` call lands — has two
// failure modes ClassicAPI hit in the field, both fixed here:
//
//   1. Network flood (ClassicAPI issue #25). One `/reload` can make
//      pfQuest / Questie / Bagnon look up hundreds of items in a single
//      frame. Unthrottled, every miss fired its own packet at once: a
//      reported 1,325 requests in ~14 ms exhausted a consumer router's
//      NAT table and dropped the machine's internet connectivity. So we
//      never issue from the call path — every request is queued, and one
//      per-frame tick is the SINGLE point that issues, rate-limited.
//
//   2. Startup orphan. Eagerly creating a cache entry (a `CacheFetch`
//      with a callback) before the engine's network/opcode dispatch has
//      settled leaves the entry permanently pending: the engine's own
//      prefetch skips already-existing entries, and our SMSG goes into a
//      hole while dispatch isn't ready. This left pfQuest/Questie item
//      lookups nil forever when they scanned at PLAYER_ENTERING_WORLD.
//      So the tick issues nothing until `g_settled` (see kSettleMs).
//
// `Track` records an item WITHOUT touching the cache; `OnTick` issues the
// query once settled and sweeps for fills. A single completion path (the
// sweep, `TryCompletePending`) fires exactly one event per item — the
// escalation callback only flags a server rejection, it never fires.

struct Pending {
    uint32_t itemID;
    int ticksWaiting;
    bool requestIssued;
    bool implicit; // true: fire GET_ITEM_INFO_RECEIVED; false: ITEM_DATA_LOAD_RESULT
    bool failed;   // set by the escalation callback on a server "no such item" reply
};

constexpr int kMaxPending = 512;
constexpr int kMaxWaitTicks = 1200;
// Wall-clock delay past world-live before we assume the engine's startup
// network/opcode dispatch has settled and eager queries are safe.
// Anchored to VAR_IN_WORLD (the same flag bag/UpdateDelayed trusts), NOT
// a raw frame count: a fixed tick count is FPS-dependent, so it could
// fire before dispatch was ready on a fast machine (orphan risk) yet
// leave bag icons as "?" far longer than intended on a slow login.
constexpr DWORD kSettleMs = 1000;
// Minimum spacing between outgoing *background* SMSG_ITEM_QUERY_SINGLE
// requests — the flood cap (issue #25). 50 ms caps the sustained
// background rate at 20 requests / second.
constexpr DWORD kMinRequestSpacingMs = 50;
// Priority tier. The player's carried items (equipment + backpack +
// equipped bags) are what the bag / paperdoll UI shows immediately on
// login; behind the background floor they showed as "?" for a second or
// two while a bulk addon scan drained ahead of them. They get a separate,
// more generous per-tick budget. The set is bounded, so worst case is
// kOwnedPerTick + 1 packets per frame — far below the flood floor.
constexpr int kOwnedPerTick = 6;
constexpr int kMaxOwned = 256;
constexpr DWORD kOwnedRefreshMs = 1000; // carried set changes slowly

Pending g_pending[kMaxPending];
int g_pendingCount = 0;
bool g_settled = false;
// Tick timestamp when we first saw the world go live (VAR_IN_WORLD == 1);
// 0 = not yet. Anchors the kSettleMs delay. Only meaningful until the
// one-way g_settled latch flips, so it is never reset.
DWORD g_inWorldSinceMs = 0;
DWORD g_lastRequestMs = 0;

// Snapshot of the itemIDs the player currently carries. Rebuilt from the
// engine inventory helpers (no Lua stack — safe at tick time) at most
// once per kOwnedRefreshMs, and only while there is queued work.
uint32_t g_owned[kMaxOwned];
int g_ownedCount = 0;
DWORD g_ownedBuiltMs = 0;
bool g_ownedEverBuilt = false;
// One-shot latch: have we proactively seeded the queue with the player's
// carried items this enter-world session? Re-armed on in-game init.
bool g_ownedPrefetched = false;

int FindPending(uint32_t itemID) {
    for (int i = 0; i < g_pendingCount; ++i)
        if (g_pending[i].itemID == itemID)
            return i;
    return -1;
}

void RemovePendingAt(int idx) {
    --g_pendingCount;
    if (idx != g_pendingCount)
        g_pending[idx] = g_pending[g_pendingCount];
}

// Callback for the escalation path. The engine queues the query when we
// pass a non-null callback to `CacheFetch`; it then invokes this with the
// itemID directly. On a successful fill the engine has already set the
// entry's loaded flag, so the sweep reports success — we do nothing here.
// On a server failure (item does not exist) the engine does NOT set the
// loaded flag, so the sweep can't detect it; we flag the pending entry so
// the sweep fires failure immediately instead of waiting out kMaxWaitTicks.
void __cdecl ItemLoadCallback_Escalated(uint32_t itemID, const uint64_t *guid,
                                        void *userData, int success) {
    (void)guid;
    (void)userData;
    if (success)
        return;
    const int idx = FindPending(itemID);
    if (idx >= 0)
        g_pending[idx].failed = true;
}

// Records an item we owe a completion event for, WITHOUT eagerly creating
// the cache entry (see the block comment above — doing so during startup
// orphans it). Returns false only when the request was dropped because
// the pending set is full; all other outcomes return true.
bool Track(uint32_t itemID, bool implicit) {
    if (CacheFetch(itemID, nullptr) != nullptr) {
        // Already loaded. Explicit RequestLoadItemData fires its
        // completion event synchronously (modern API contract). Implicit
        // warmup mirrors the engine's already-loaded shortcut, which does
        // not re-invoke a callback — so a cache hit fires nothing.
        if (!implicit)
            FireItemDataLoadResult(static_cast<int>(itemID), 1);
        return true;
    }
    const int idx = FindPending(itemID);
    if (idx >= 0) {
        // Already tracking. An explicit request supersedes a pending
        // implicit one so the explicit caller's ITEM_DATA_LOAD_RESULT is
        // honored (the two events are mutually exclusive per fill).
        if (!implicit)
            g_pending[idx].implicit = false;
        return true;
    }
    if (g_pendingCount >= kMaxPending)
        return false;
    g_pending[g_pendingCount].itemID = itemID;
    g_pending[g_pendingCount].ticksWaiting = 0;
    g_pending[g_pendingCount].requestIssued = false;
    g_pending[g_pendingCount].implicit = implicit;
    g_pending[g_pendingCount].failed = false;
    ++g_pendingCount;
    return true;
}

// Fires + removes a pending entry if it has resolved either way: loaded
// (engine set the flag) -> success, or `failed` (escalation callback saw a
// server rejection) -> failure. Returns true if it fired and removed the
// entry at `i`, so callers must NOT advance their index when it does.
bool TryCompletePending(int i) {
    const uint32_t itemID = g_pending[i].itemID;
    const bool implicit = g_pending[i].implicit;
    if (CacheFetch(itemID, nullptr) != nullptr) {
        RemovePendingAt(i);
        FireByTag(itemID, true, implicit);
        return true;
    }
    if (g_pending[i].failed) {
        RemovePendingAt(i);
        FireByTag(itemID, false, implicit);
        return true;
    }
    return false;
}

// --- Owned-item priority set ---------------------------------------------
//
// The player's carried inventory, so OnTick can issue those ahead of
// background bulk lookups. Built from the engine inventory helpers in
// `Item::Location` (equipment + backpack + equipped bags) — pure engine
// calls, no Lua stack, safe at tick time.

void AddOwned(uint32_t itemID) {
    if (itemID == 0)
        return;
    for (int i = 0; i < g_ownedCount; ++i)
        if (g_owned[i] == itemID)
            return; // dedup — stacks / duplicates collapse to one entry
    if (g_ownedCount < kMaxOwned)
        g_owned[g_ownedCount++] = itemID;
}

void RebuildOwned() {
    g_ownedCount = 0;
    for (int slot = Offsets::EQUIPMENT_SLOT_FIRST; slot <= Offsets::EQUIPMENT_SLOT_LAST; ++slot) {
        const uint8_t *item = Item::Location::ResolveEquipmentSlot(slot);
        if (item != nullptr)
            AddOwned(static_cast<uint32_t>(Item::ID::FromCGItem(item)));
    }
    // Backpack (bagID 0) + the four equipped bags (bagID 1..4).
    for (int bagID = 0; bagID <= 4; ++bagID) {
        const int n = Item::Location::GetBagNumSlots(bagID);
        for (int slot = 1; slot <= n; ++slot) {
            const uint8_t *item = Item::Location::ResolveBagSlot(bagID, slot);
            if (item != nullptr)
                AddOwned(static_cast<uint32_t>(Item::ID::FromCGItem(item)));
        }
    }
    g_ownedEverBuilt = true;
}

bool IsOwned(uint32_t itemID) {
    for (int i = 0; i < g_ownedCount; ++i)
        if (g_owned[i] == itemID)
            return true;
    return false;
}

// The ONE place any item query is issued, so the rate limiter here governs
// every path into the cache. Per tick:
//   1. Latch the startup gate a fixed wall-clock delay past world-live.
//   2. Once settled, seed the queue with the player's carried items
//      (once per session) and refresh that set slowly.
//   3. Issue queries — carried items first (up to kOwnedPerTick), then one
//      background item per kMinRequestSpacingMs.
//   4. Fire completion for fills (cache hit) and server rejections
//      (`failed`), and give up after kMaxWaitTicks for a query that was
//      sent but never resolved.
void OnTick() {
    const DWORD now = GetTickCount();

    if (!g_settled) {
        const bool inWorld = *reinterpret_cast<const volatile uint8_t *>(
                                 static_cast<uintptr_t>(Offsets::VAR_IN_WORLD)) != 0;
        if (inWorld && g_inWorldSinceMs == 0)
            g_inWorldSinceMs = now;
        if (g_inWorldSinceMs != 0 && Elapsed(g_inWorldSinceMs, now) >= kSettleMs)
            g_settled = true;
    }

    // Proactive carried-item prefetch (once per enter-world session). The
    // engine does NOT query bag item stats on enter-world or bag draw, so
    // on a cold WDB the player's carried items draw as "?" until something
    // else queries them. Seed the queue so they issue on the owned lane
    // ahead of any addon's background flood. Deferred to post-settle so it
    // can't orphan an entry, and gated on a live inventory manager so a
    // too-early settle retries next tick instead of latching an empty walk.
    if (g_settled && !g_ownedPrefetched &&
        Item::Location::PlayerInventoryManager() != nullptr) {
        RebuildOwned();
        g_ownedBuiltMs = now;
        for (int i = 0; i < g_ownedCount; ++i)
            Track(g_owned[i], /*implicit=*/true);
        g_ownedPrefetched = true;
    }

    if (g_pendingCount == 0)
        return;

    // Refresh the carried set while there is queued work, at most once per
    // kOwnedRefreshMs — it changes slowly and the walk is pointer-chasing.
    if (g_settled &&
        (!g_ownedEverBuilt || Elapsed(g_ownedBuiltMs, now) >= kOwnedRefreshMs)) {
        RebuildOwned();
        g_ownedBuiltMs = now;
    }

    // Two independent issue budgets this tick: carried items (up to
    // kOwnedPerTick, no spacing floor) and background (one, once the
    // spacing floor elapses). The background floor is wall-clock, not a
    // tick count, so its rate is bounded regardless of FPS.
    int ownedBudget = g_settled ? kOwnedPerTick : 0;
    bool bgAvailable =
        g_settled && Elapsed(g_lastRequestMs, now) >= kMinRequestSpacingMs;

    for (int i = 0; i < g_pendingCount;) {
        if (g_settled && !g_pending[i].requestIssued) {
            const bool owned = IsOwned(g_pending[i].itemID);
            bool issue = false;
            if (owned && ownedBudget > 0) {
                issue = true;
                --ownedBudget;
            } else if (!owned && bgAvailable) {
                issue = true;
                bgAvailable = false;
                g_lastRequestMs = now;
            }
            if (issue) {
                CacheFetch(g_pending[i].itemID, &ItemLoadCallback_Escalated);
                g_pending[i].requestIssued = true;
            }
        }
        // A fill (engine response) or a server rejection resolves the item.
        if (TryCompletePending(i))
            continue;
        // Only time out items whose query is actually in flight. Counting
        // from track-time would fire spurious failures for items still
        // waiting their turn behind the rate limiter.
        if (g_pending[i].requestIssued) {
            if (g_pending[i].ticksWaiting >= kMaxWaitTicks) {
                const uint32_t itemID = g_pending[i].itemID;
                const bool implicit = g_pending[i].implicit;
                RemovePendingAt(i);
                FireByTag(itemID, false, implicit);
                continue;
            }
            ++g_pending[i].ticksWaiting;
        }
        ++i;
    }
}

const Tick::FrameTick::AutoSubscribe _tickSub{&OnTick};

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

// Explicit-request path: queues the item tagged explicit (so completion
// fires ITEM_DATA_LOAD_RESULT). `Track` resolves an already-cached item
// for free (fires the event synchronously per the modern contract) and
// queues only a genuine miss. Returns false only if the queue is full.
bool RequestAndMaybeNotify(uint32_t itemID) { return Track(itemID, /*implicit=*/false); }

int __cdecl Script_RequestLoadItemDataByID(void *L) {
    const int itemID = Item::Arg::ResolveItemID(L, 1);
    if (itemID <= 0) {
        Game::Lua::PushBoolean(L, 0);
        return 1;
    }
    const bool accepted = RequestAndMaybeNotify(static_cast<uint32_t>(itemID));
    Game::Lua::PushBoolean(L, accepted ? 1 : 0);
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
    const bool accepted = RequestAndMaybeNotify(static_cast<uint32_t>(itemID));
    Game::Lua::PushBoolean(L, accepted ? 1 : 0);
    return 1;
}

void RegisterLuaFunctions() {
    // Re-arm the proactive carried-item prefetch for this enter-world
    // session. g_settled is deliberately NOT reset — it is a one-way
    // process latch; on a warm cache the re-run resolves as cache hits.
    g_ownedPrefetched = false;
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
// warmup. Routes through the pending queue so OnTick's single rate-limited
// issue point governs all outgoing item queries (issue #25). A cache hit
// resolves for free; a genuine miss queues, and the sweep fires
// GET_ITEM_INFO_RECEIVED (implicit) when the response lands.
void WarmCache(uint32_t itemID) { Track(itemID, /*implicit=*/true); }

// Hook for `Script_GetItemInfo`. Pre-warms the cache from arg 1, then
// dispatches to the original. The original still returns nil for cache
// misses (vanilla behavior) but a query is now queued, so subsequent
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
