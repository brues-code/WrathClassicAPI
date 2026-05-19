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

// `C_Item.GetItemInfoInstant(item)` — modern WoW backport.
//
// Returns 7 values without requiring an item-stats network query
// beyond the basic DBC lookups the engine already has resident:
//   itemID, itemType, itemSubType, itemEquipLoc, icon, classID, subClassID
//
// Important caveat for 3.3.5: unlike modern WoW, the engine here has
// no separate "instant" cache. We read the same item-stats record
// `Script_GetItemInfo` reads, which means an UNCACHED item returns
// 0 results (nil to Lua) — same shape `GetItemInfo` itself does for
// cache misses. The existing `GetItemInfo` hook in `item/Data.cpp`
// transparently kicks off the network query on miss, so a follow-up
// call after `GET_ITEM_INFO_RECEIVED` lands the data. To match that
// behavior, we also call `WarmCache` on the miss path here.

#include "Game.h"
#include "Offsets.h"
#include "item/Arg.h"
#include "item/Data.h"
#include "item/ID.h"
#include "item/Location.h"

#include <cstdint>
#include <cstdio>

namespace Item::Info {

namespace {

using GetItemRecord_t = const uint8_t *(__thiscall *)(void *cache, uint32_t itemID,
                                                      const uint64_t *guid, void *callback,
                                                      void *userData, int unused);

using IconBasenameByDisplayID_t = const char *(__cdecl *)(uint32_t displayInfoID);
using ItemLinkFormatter_t = const char *(__cdecl *)(uint32_t itemID);

const char *EmptyIfNull(const char *s) {
    return (s != nullptr) ? s : "";
}

const uint8_t *FetchItemRecord(uint32_t itemID) {
    auto fn = reinterpret_cast<GetItemRecord_t>(Offsets::FUN_DBCACHE_ITEMSTATS_GET_RECORD);
    auto *cache = reinterpret_cast<void *>(Offsets::VAR_ITEMDB_CACHE);
    const uint64_t zeroGuid = 0;
    return fn(cache, itemID, &zeroGuid, nullptr, nullptr, 0);
}

// Resolves an ItemClass.dbc row's name. The table is bounded by
// [VAR_ITEMCLASS_MIN, VAR_ITEMCLASS_MAX] and stored sparsely — index
// is `records[id - min]`. `VAR_ITEMCLASS_RECORDS_BASE` is a *pointer*
// to the records-pointer array (verified at `Script_GetItemInfo`
// 00516DA1: `MOV ECX, dword ptr [0x00AD3DB4]` then
// `MOV EAX, dword ptr [ECX + EAX*4]` — two derefs total before the
// record is in hand). Returns "" for out-of-range ids and unpopulated
// slots so the caller can push unconditionally.
const char *LookupItemClassName(uint32_t classID) {
    const int min = *reinterpret_cast<const int *>(Offsets::VAR_ITEMCLASS_MIN);
    const int max = *reinterpret_cast<const int *>(Offsets::VAR_ITEMCLASS_MAX);
    const int id = static_cast<int>(classID);
    if (id < min || id > max)
        return "";
    auto *records = *reinterpret_cast<const uint8_t *const *const *>(
        Offsets::VAR_ITEMCLASS_RECORDS_BASE);
    if (records == nullptr)
        return "";
    const uint8_t *record = records[id - min];
    if (record == nullptr)
        return "";
    const char *name = *reinterpret_cast<const char *const *>(
        record + Offsets::OFF_ITEMCLASS_NAME);
    return EmptyIfNull(name);
}

// Walks ItemSubClass.dbc linearly (no direct index — table is keyed on
// `(classID, subClassID)` pairs). Mirrors `Script_GetItemInfo`'s
// fallback chain: prefer the verbose name, fall back to the short name
// when verbose is empty. `VAR_ITEMSUBCLASS_RECORDS` is a *pointer* to
// the records block (verified at 00516DCE:
// `MOV ECX, dword ptr [0x00AD3F60]` — one deref to get the records
// base, then indexed by `i * stride`).
const char *LookupItemSubClassName(uint32_t classID, uint32_t subClassID) {
    const int count = *reinterpret_cast<const int *>(Offsets::VAR_ITEMSUBCLASS_COUNT);
    if (count <= 0)
        return "";
    auto *records = *reinterpret_cast<const uint8_t *const *>(
        Offsets::VAR_ITEMSUBCLASS_RECORDS);
    if (records == nullptr)
        return "";
    for (int i = 0; i < count; ++i) {
        const uint8_t *record = records + i * Offsets::OFF_ITEMSUBCLASS_STRIDE;
        if (*reinterpret_cast<const uint32_t *>(record + Offsets::OFF_ITEMSUBCLASS_CLASS_ID)
                != classID)
            continue;
        if (*reinterpret_cast<const uint32_t *>(record + Offsets::OFF_ITEMSUBCLASS_SUBCLASS_ID)
                != subClassID)
            continue;
        const char *verbose = *reinterpret_cast<const char *const *>(
            record + Offsets::OFF_ITEMSUBCLASS_VERBOSE_NAME);
        if (verbose != nullptr && verbose[0] != 0)
            return verbose;
        const char *shortName = *reinterpret_cast<const char *const *>(
            record + Offsets::OFF_ITEMSUBCLASS_NAME);
        return EmptyIfNull(shortName);
    }
    return "";
}

const char *LookupInvType(uint32_t invType) {
    if (invType > Offsets::INVTYPE_TABLE_MAX_INDEX)
        return "";
    auto **table = reinterpret_cast<const char **>(Offsets::VAR_INVTYPE_STRING_TABLE);
    return EmptyIfNull(table[invType]);
}

// Builds the full icon path "Interface\Icons\<basename>" by calling
// the engine's display-info-to-icon resolver and prepending the standard
// directory. Engine's resolver returns "INV_Misc_QuestionMark" as the
// fallback for missing rows.
void BuildIconPath(uint32_t displayInfoID, char *out, size_t outSize) {
    if (out == nullptr || outSize == 0)
        return;
    auto fn = reinterpret_cast<IconBasenameByDisplayID_t>(
        Offsets::FUN_ICON_BASENAME_BY_DISPLAY_ID);
    const char *basename = fn(displayInfoID);
    if (basename == nullptr) {
        out[0] = 0;
        return;
    }
    std::snprintf(out, outSize, "Interface\\Icons\\%s", basename);
}

int __cdecl Script_C_Item_GetItemInfoInstant(void *L) {
    const int itemID = Item::Arg::ResolveItemID(L, 1);
    if (itemID <= 0)
        return 0;

    const uint8_t *record = FetchItemRecord(static_cast<uint32_t>(itemID));
    if (record == nullptr) {
        // Cache miss — kick off the network query so a follow-up call
        // after GET_ITEM_INFO_RECEIVED succeeds. Matches the implicit-
        // warmup behavior of our GetItemInfo hook.
        Item::Data::WarmCache(static_cast<uint32_t>(itemID));
        return 0;
    }

    const uint32_t classID =
        *reinterpret_cast<const uint32_t *>(record + Offsets::OFF_ITEMSTATS_CLASS);
    const uint32_t subClassID =
        *reinterpret_cast<const uint32_t *>(record + Offsets::OFF_ITEMSTATS_SUBCLASS);
    const uint32_t displayInfoID =
        *reinterpret_cast<const uint32_t *>(record + Offsets::OFF_ITEMSTATS_DISPLAY_INFO_ID);
    const uint32_t invType =
        *reinterpret_cast<const uint32_t *>(record + Offsets::OFF_ITEMSTATS_INVENTORY_TYPE);

    char iconPath[260] = {0};
    BuildIconPath(displayInfoID, iconPath, sizeof(iconPath));

    Game::Lua::PushNumber(L, static_cast<double>(itemID));
    Game::Lua::PushString(L, LookupItemClassName(classID));
    Game::Lua::PushString(L, LookupItemSubClassName(classID, subClassID));
    Game::Lua::PushString(L, LookupInvType(invType));
    Game::Lua::PushString(L, iconPath);
    Game::Lua::PushNumber(L, static_cast<double>(classID));
    Game::Lua::PushNumber(L, static_cast<double>(subClassID));
    return 7;
}

// -----------------------------------------------------------------------------
// Single-field accessor helpers shared by the C_Item.Get*ByID /
// C_Item.Get*(itemLocation) pairs below. All of these follow the same
// shape:
//   * ByID variant accepts numeric itemID or `"item:NN..."` string,
//     fires WarmCache on cache miss (so the follow-up call after
//     GET_ITEM_INFO_RECEIVED returns the data), and returns 0 (= nil
//     in Lua) on any failure.
//   * Location variant accepts an itemLocation table, resolves to
//     CGItem → itemID → cache record, and returns 0 on empty slot /
//     invalid arg. No WarmCache because the engine already caches
//     items the moment they land in inventory.

const uint8_t *FetchByLuaArg(void *L) {
    const int itemID = Item::Arg::ResolveItemID(L, 1);
    if (itemID <= 0)
        return nullptr;
    const uint8_t *record = FetchItemRecord(static_cast<uint32_t>(itemID));
    if (record == nullptr) {
        Item::Data::WarmCache(static_cast<uint32_t>(itemID));
        return nullptr;
    }
    return record;
}

const uint8_t *FetchByLuaLocation(void *L) {
    if (!Item::Location::IsLocationArg(L, 1))
        return nullptr;
    const int itemID = Item::ID::FromCGItem(Item::Location::Resolve(L, 1));
    if (itemID <= 0)
        return nullptr;
    return FetchItemRecord(static_cast<uint32_t>(itemID));
}

int PushUint32FromRecord(void *L, const uint8_t *record, int offset) {
    const uint32_t value = *reinterpret_cast<const uint32_t *>(record + offset);
    Game::Lua::PushNumber(L, static_cast<double>(value));
    return 1;
}

int PushIconFromRecord(void *L, const uint8_t *record) {
    const uint32_t displayInfoID = *reinterpret_cast<const uint32_t *>(
        record + Offsets::OFF_ITEMSTATS_DISPLAY_INFO_ID);
    char iconPath[260] = {0};
    BuildIconPath(displayInfoID, iconPath, sizeof(iconPath));
    Game::Lua::PushString(L, iconPath);
    return 1;
}

int PushNameFromRecord(void *L, const uint8_t *record) {
    const char *name = *reinterpret_cast<const char *const *>(
        record + Offsets::OFF_ITEMSTATS_NAME);
    if (name == nullptr)
        return 0;
    Game::Lua::PushString(L, name);
    return 1;
}

// -----------------------------------------------------------------------------
// Quality

int __cdecl Script_C_Item_GetItemQualityByID(void *L) {
    const uint8_t *record = FetchByLuaArg(L);
    if (record == nullptr)
        return 0;
    return PushUint32FromRecord(L, record, Offsets::OFF_ITEMSTATS_QUALITY);
}

int __cdecl Script_C_Item_GetItemQuality(void *L) {
    const uint8_t *record = FetchByLuaLocation(L);
    if (record == nullptr)
        return 0;
    return PushUint32FromRecord(L, record, Offsets::OFF_ITEMSTATS_QUALITY);
}

// -----------------------------------------------------------------------------
// Max stack size

int __cdecl Script_C_Item_GetItemMaxStackSizeByID(void *L) {
    const uint8_t *record = FetchByLuaArg(L);
    if (record == nullptr)
        return 0;
    return PushUint32FromRecord(L, record, Offsets::OFF_ITEMSTATS_STACK_COUNT);
}

int __cdecl Script_C_Item_GetItemMaxStackSize(void *L) {
    const uint8_t *record = FetchByLuaLocation(L);
    if (record == nullptr)
        return 0;
    return PushUint32FromRecord(L, record, Offsets::OFF_ITEMSTATS_STACK_COUNT);
}

// -----------------------------------------------------------------------------
// Item level
//
// Modern WoW's `GetDetailedItemLevelInfo` returns three values
// (effectiveLevel, isPreview, baseLevel) — none of those distinctions
// exist in 3.3.5 (no scaling, no upgrades), so we just return the
// single baseline integer. ItemUtil.lua already wraps the call in
// parentheses to take only the first return, so single-value matches.

int __cdecl Script_C_Item_GetDetailedItemLevelInfo(void *L) {
    const uint8_t *record = FetchByLuaArg(L);
    if (record == nullptr)
        return 0;
    return PushUint32FromRecord(L, record, Offsets::OFF_ITEMSTATS_ITEM_LEVEL);
}

int __cdecl Script_C_Item_GetCurrentItemLevel(void *L) {
    const uint8_t *record = FetchByLuaLocation(L);
    if (record == nullptr)
        return 0;
    return PushUint32FromRecord(L, record, Offsets::OFF_ITEMSTATS_ITEM_LEVEL);
}

// -----------------------------------------------------------------------------
// Inventory type (the integer enum, not the InvType string)

int __cdecl Script_C_Item_GetItemInventoryTypeByID(void *L) {
    const uint8_t *record = FetchByLuaArg(L);
    if (record == nullptr)
        return 0;
    return PushUint32FromRecord(L, record, Offsets::OFF_ITEMSTATS_INVENTORY_TYPE);
}

int __cdecl Script_C_Item_GetItemInventoryType(void *L) {
    const uint8_t *record = FetchByLuaLocation(L);
    if (record == nullptr)
        return 0;
    return PushUint32FromRecord(L, record, Offsets::OFF_ITEMSTATS_INVENTORY_TYPE);
}

// -----------------------------------------------------------------------------
// Icon path

int __cdecl Script_C_Item_GetItemIconByID(void *L) {
    const uint8_t *record = FetchByLuaArg(L);
    if (record == nullptr)
        return 0;
    return PushIconFromRecord(L, record);
}

int __cdecl Script_C_Item_GetItemIcon(void *L) {
    const uint8_t *record = FetchByLuaLocation(L);
    if (record == nullptr)
        return 0;
    return PushIconFromRecord(L, record);
}

// -----------------------------------------------------------------------------
// Plain name (no color codes / suffixes)

int __cdecl Script_C_Item_GetItemNameByID(void *L) {
    const uint8_t *record = FetchByLuaArg(L);
    if (record == nullptr)
        return 0;
    return PushNameFromRecord(L, record);
}

int __cdecl Script_C_Item_GetItemName(void *L) {
    const uint8_t *record = FetchByLuaLocation(L);
    if (record == nullptr)
        return 0;
    return PushNameFromRecord(L, record);
}

// -----------------------------------------------------------------------------
// Item link (full "|cff…|Hitem:…|h[Name]|h|r" string)
//
// Modern WoW's `C_Item.GetItemLink` takes an itemLocation. We delegate
// to the engine's link formatter at `FUN_ITEM_LINK_FORMATTER`, which
// already builds the colored hyperlink string with the local player's
// level baked into the link's level field — exactly what stock
// `GetItemInfo` produces as its second return value.

int __cdecl Script_C_Item_GetItemLink(void *L) {
    if (!Item::Location::IsLocationArg(L, 1))
        return 0;
    const int itemID = Item::ID::FromCGItem(Item::Location::Resolve(L, 1));
    if (itemID <= 0)
        return 0;
    auto fn = reinterpret_cast<ItemLinkFormatter_t>(Offsets::FUN_ITEM_LINK_FORMATTER);
    const char *link = fn(static_cast<uint32_t>(itemID));
    if (link == nullptr)
        return 0;
    Game::Lua::PushString(L, link);
    return 1;
}

// -----------------------------------------------------------------------------
// Lock state stub
//
// Modern WoW's `C_Item.IsLocked(itemLocation)` reads the transient
// "item is locked" flag set during trade / mail / loot interactions
// (ITEM_FIELD_FLAGS bit in the CGItem update fields). Mapping that
// flag's exact bit on this build is deferred; for now we always return
// false so the ItemUtil.lua `ItemMixin:IsItemLocked` path returns the
// correct answer for the common case (non-locked items). The Lua
// companion separately stubs `C_Item.LockItem` / `UnlockItem` as
// no-ops via `or function() end`.

int __cdecl Script_C_Item_IsLocked(void *L) {
    Game::Lua::PushBool(L, false);
    return 1;
}

void RegisterLuaFunctions() {
    Game::Lua::RegisterTableFunction("C_Item", "GetItemInfoInstant",
                                     &Script_C_Item_GetItemInfoInstant);

    Game::Lua::RegisterTableFunction("C_Item", "GetItemQualityByID",
                                     &Script_C_Item_GetItemQualityByID);
    Game::Lua::RegisterTableFunction("C_Item", "GetItemQuality",
                                     &Script_C_Item_GetItemQuality);

    Game::Lua::RegisterTableFunction("C_Item", "GetItemMaxStackSizeByID",
                                     &Script_C_Item_GetItemMaxStackSizeByID);
    Game::Lua::RegisterTableFunction("C_Item", "GetItemMaxStackSize",
                                     &Script_C_Item_GetItemMaxStackSize);

    Game::Lua::RegisterTableFunction("C_Item", "GetDetailedItemLevelInfo",
                                     &Script_C_Item_GetDetailedItemLevelInfo);
    Game::Lua::RegisterTableFunction("C_Item", "GetCurrentItemLevel",
                                     &Script_C_Item_GetCurrentItemLevel);

    Game::Lua::RegisterTableFunction("C_Item", "GetItemInventoryTypeByID",
                                     &Script_C_Item_GetItemInventoryTypeByID);
    Game::Lua::RegisterTableFunction("C_Item", "GetItemInventoryType",
                                     &Script_C_Item_GetItemInventoryType);

    Game::Lua::RegisterTableFunction("C_Item", "GetItemIconByID",
                                     &Script_C_Item_GetItemIconByID);
    Game::Lua::RegisterTableFunction("C_Item", "GetItemIcon",
                                     &Script_C_Item_GetItemIcon);

    Game::Lua::RegisterTableFunction("C_Item", "GetItemNameByID",
                                     &Script_C_Item_GetItemNameByID);
    Game::Lua::RegisterTableFunction("C_Item", "GetItemName",
                                     &Script_C_Item_GetItemName);

    Game::Lua::RegisterTableFunction("C_Item", "GetItemLink",
                                     &Script_C_Item_GetItemLink);

    Game::Lua::RegisterTableFunction("C_Item", "IsLocked",
                                     &Script_C_Item_IsLocked);
}

const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace
} // namespace Item::Info
