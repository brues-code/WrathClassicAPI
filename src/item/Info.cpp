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

// `C_Item.GetItemInfoInstant(item)` — modern WoW backport.
//
// Returns 7 values from client-side data alone, no item-stats network query:
//   itemID, itemType, itemSubType, itemEquipLoc, icon, classID, subClassID
//
// The four numeric inputs (class, subclass, inventory type, display-info) are
// read from the client `Item.dbc` store, resident for every item shipped with
// the 3.3.5 client — so for those items this is genuinely instant and never
// returns nil, matching modern. The type / subtype / equip-loc strings and the
// icon path then resolve from `ItemClass.dbc` / `ItemSubClass.dbc` / the invtype
// table / `ItemDisplayInfo.dbc`, all client-side.
//
// The one gap is a server-custom item beyond the client's `Item.dbc`: it isn't
// in that store, so we fall back to the server-populated item-stats cache (the
// same record `GetItemInfo` reads). If that's also a miss we kick off the
// network query via `WarmCache` and return nil this call, so a follow-up after
// `GET_ITEM_INFO_RECEIVED` then succeeds — the old behavior, now only for
// custom items.

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
using BuildNameFromID_t = const char *(__cdecl *)(char *out, int outSize,
                                                  uint32_t itemID, int suffixID);

const char *EmptyIfNull(const char *s) {
    return (s != nullptr) ? s : "";
}

const uint8_t *FetchItemRecord(uint32_t itemID) {
    auto fn = reinterpret_cast<GetItemRecord_t>(Offsets::FUN_DBCACHE_ITEMSTATS_GET_RECORD);
    auto *cache = reinterpret_cast<void *>(Offsets::VAR_ITEMDB_CACHE);
    const uint64_t zeroGuid = 0;
    return fn(cache, itemID, &zeroGuid, nullptr, nullptr, 0);
}

// The four "instant" fields GetItemInfoInstant needs: class / subclass /
// inventory type / display-info (icon). All derivable from client-side data
// alone, no item-stats network query.
struct InstantFields {
    uint32_t classID;
    uint32_t subClassID;
    uint32_t invType;
    uint32_t displayInfoID;
};

uint32_t StoreField(uintptr_t base, unsigned off) {
    return *reinterpret_cast<const uint32_t *>(base + off);
}

// Reads the instant fields straight from client Item.dbc (no server query) via
// the inline WowClientDB GetRow: bounds-check the itemID against [minID, maxID],
// then index the id->record table. Returns false when the item isn't in the
// client's Item.dbc (e.g. a server-custom item) so the caller can fall back.
bool ReadItemDbcInstant(uint32_t itemID, InstantFields *out) {
    const int id = static_cast<int>(itemID);
    const int minID = static_cast<int>(StoreField(Offsets::VAR_ITEMDBC_STORE,
                                                   Offsets::OFF_ITEMDBC_STORE_MIN_ID));
    const int maxID = static_cast<int>(StoreField(Offsets::VAR_ITEMDBC_STORE,
                                                   Offsets::OFF_ITEMDBC_STORE_MAX_ID));
    if (id < minID || id > maxID)
        return false;
    auto *index = *reinterpret_cast<const uint8_t *const *const *>(
        Offsets::VAR_ITEMDBC_STORE + Offsets::OFF_ITEMDBC_STORE_INDEX);
    if (index == nullptr)
        return false;
    const uint8_t *record = index[id - minID];
    if (record == nullptr)
        return false; // gap in the id range
    out->classID = StoreField(reinterpret_cast<uintptr_t>(record), Offsets::OFF_ITEMDBC_CLASS);
    out->subClassID = StoreField(reinterpret_cast<uintptr_t>(record), Offsets::OFF_ITEMDBC_SUBCLASS);
    out->invType =
        StoreField(reinterpret_cast<uintptr_t>(record), Offsets::OFF_ITEMDBC_INVENTORY_TYPE);
    out->displayInfoID =
        StoreField(reinterpret_cast<uintptr_t>(record), Offsets::OFF_ITEMDBC_DISPLAY_INFO_ID);
    return true;
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

    // Client Item.dbc first — resident for every stock item with no server
    // query, so this is genuinely instant and never nils for them (matching
    // modern). Only server-custom items miss here.
    InstantFields f;
    if (!ReadItemDbcInstant(static_cast<uint32_t>(itemID), &f)) {
        // Not in the client's Item.dbc (a server-custom item). Fall back to the
        // item-stats cache; a miss there kicks off the network query so a
        // follow-up call after GET_ITEM_INFO_RECEIVED succeeds.
        const uint8_t *record = FetchItemRecord(static_cast<uint32_t>(itemID));
        if (record == nullptr) {
            Item::Data::WarmCache(static_cast<uint32_t>(itemID));
            return 0;
        }
        f.classID = *reinterpret_cast<const uint32_t *>(record + Offsets::OFF_ITEMSTATS_CLASS);
        f.subClassID = *reinterpret_cast<const uint32_t *>(record + Offsets::OFF_ITEMSTATS_SUBCLASS);
        f.displayInfoID =
            *reinterpret_cast<const uint32_t *>(record + Offsets::OFF_ITEMSTATS_DISPLAY_INFO_ID);
        f.invType =
            *reinterpret_cast<const uint32_t *>(record + Offsets::OFF_ITEMSTATS_INVENTORY_TYPE);
    }

    char iconPath[260] = {0};
    BuildIconPath(f.displayInfoID, iconPath, sizeof(iconPath));

    Game::Lua::PushNumber(L, static_cast<double>(itemID));
    Game::Lua::PushString(L, LookupItemClassName(f.classID));
    Game::Lua::PushString(L, LookupItemSubClassName(f.classID, f.subClassID));
    Game::Lua::PushString(L, LookupInvType(f.invType));
    Game::Lua::PushString(L, iconPath);
    Game::Lua::PushNumber(L, static_cast<double>(f.classID));
    Game::Lua::PushNumber(L, static_cast<double>(f.subClassID));
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

// Standard ITEM_QUALITY_COLORS hex, indexed by quality 0..7. Used to build item
// links without the engine's link formatter (FUN_0061E290 reads extra hidden
// stack args and isn't safe to call as a plain `(itemID)` function).
const char *QualityColorHex(uint32_t quality) {
    static const char *const kColors[] = {
        "9d9d9d", // 0 Poor
        "ffffff", // 1 Common
        "1eff00", // 2 Uncommon
        "0070dd", // 3 Rare
        "a335ee", // 4 Epic
        "ff8000", // 5 Legendary
        "e6cc80", // 6 Artifact
        "e6cc80", // 7 Heirloom (WotLK shares the artifact gold tone)
    };
    if (quality > 7)
        quality = 1;
    return kColors[quality];
}

// Builds a quality-colored item hyperlink
// "|cff<color>|Hitem:<id>:0:0:0:0:0:<suffix>:<seed>:0|h[<name>]|h|r". The suffix
// and seed sit in the same fields the client encodes them in, so a random-enchant
// item round-trips (0/0 collapses to a plain link). `name` may be null (rendered
// as an empty label).
void BuildItemLink(uint32_t itemID, uint32_t quality, const char *name,
                   int suffix, int seed, char *out, size_t outSize) {
    std::snprintf(out, outSize, "|cff%s|Hitem:%u:0:0:0:0:0:%d:%d:0|h[%s]|h|r",
                  QualityColorHex(quality), itemID, suffix, seed, EmptyIfNull(name));
}

// Engine item-name builder (FUN_00706D70): writes the display name for
// (itemID, suffixID) into `out` — plain for suffixID 0, "… of the Bear" for a
// random suffix. Returns false if the engine wrote nothing.
bool NameWithSuffix(uint32_t itemID, int suffixID, char *out, size_t outSize) {
    if (out == nullptr || outSize == 0)
        return false;
    out[0] = '\0';
    auto fn = reinterpret_cast<BuildNameFromID_t>(Offsets::FUN_ITEM_BUILD_NAME_FROM_ID);
    fn(out, static_cast<int>(outSize), itemID, suffixID);
    return out[0] != '\0';
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
// Modern WoW's `C_Item.GetItemLink` takes an itemLocation. We resolve it to the
// held item's cache record and build the quality-colored hyperlink ourselves
// (see BuildItemLink) rather than call the engine's formatter, which reads
// hidden stack args and isn't safe to invoke as a plain function.

int __cdecl Script_C_Item_GetItemLink(void *L) {
    if (!Item::Location::IsLocationArg(L, 1))
        return 0;
    const int itemID = Item::ID::FromCGItem(Item::Location::Resolve(L, 1));
    if (itemID <= 0)
        return 0;
    const uint8_t *record = FetchItemRecord(static_cast<uint32_t>(itemID));
    if (record == nullptr)
        return 0;
    const uint32_t quality =
        *reinterpret_cast<const uint32_t *>(record + Offsets::OFF_ITEMSTATS_QUALITY);
    const char *name = *reinterpret_cast<const char *const *>(
        record + Offsets::OFF_ITEMSTATS_NAME);
    char link[320];
    BuildItemLink(static_cast<uint32_t>(itemID), quality, name, 0, 0, link, sizeof(link));
    Game::Lua::PushString(L, link);
    return 1;
}

// -----------------------------------------------------------------------------
// Full item info tuple
//
// `C_Item.GetItemInfo(itemInfo)` — the modern 18-value info tuple, sourced from
// the server-populated item-stats cache record (+ the class/subclass/invtype
// DBC name lookups this file already does for GetItemInfoInstant). Accepts the
// same item-arg forms as the rest of C_Item (itemID, "item:N…" / full link,
// item GUID, or item name).
//
// Returns:
//   itemName, itemLink, itemQuality, itemLevel, itemMinLevel, itemType,
//   itemSubType, itemStackCount, itemEquipLoc, itemTexture, sellPrice,
//   classID, subclassID, bindType, expansionID, setID, isCraftingReagent,
//   itemDescription
//
// `itemTexture` is the icon PATH string (3.3.5 has no fileID system — same as
// the other C_Item icon accessors, feed straight to texture:SetTexture).
// `expansionID` is 254 (the Classic sentinel), `isCraftingReagent` is always
// false (no such flag in 3.3.5's item data), and `setID` is nil for an item in
// no set. On a cache miss it warms the cache and returns nil this call — the
// value lands on a retry after GET_ITEM_INFO_RECEIVED, matching the modern
// async contract.
int __cdecl Script_C_Item_GetItemInfo(void *L) {
    const Item::Arg::Resolved arg = Item::Arg::Resolve(L, 1);
    const int itemID = arg.itemID;
    if (itemID <= 0)
        return 0;
    const uint8_t *record = FetchItemRecord(static_cast<uint32_t>(itemID));
    if (record == nullptr) {
        Item::Data::WarmCache(static_cast<uint32_t>(itemID));
        return 0; // nil this call; GET_ITEM_INFO_RECEIVED fires when ready
    }

    auto u32 = [record](int off) {
        return *reinterpret_cast<const uint32_t *>(record + off);
    };
    const uint32_t quality = u32(Offsets::OFF_ITEMSTATS_QUALITY);
    const uint32_t itemLevel = u32(Offsets::OFF_ITEMSTATS_ITEM_LEVEL);
    const uint32_t minLevel = u32(Offsets::OFF_ITEMSTATS_REQUIRED_LEVEL);
    const uint32_t classID = u32(Offsets::OFF_ITEMSTATS_CLASS);
    const uint32_t subClassID = u32(Offsets::OFF_ITEMSTATS_SUBCLASS);
    const uint32_t stackCount = u32(Offsets::OFF_ITEMSTATS_STACK_COUNT);
    const uint32_t invType = u32(Offsets::OFF_ITEMSTATS_INVENTORY_TYPE);
    const uint32_t displayInfoID = u32(Offsets::OFF_ITEMSTATS_DISPLAY_INFO_ID);
    const uint32_t sellPrice = u32(Offsets::OFF_ITEMSTATS_SELL_PRICE);
    const uint32_t bindType = u32(Offsets::OFF_ITEMSTATS_BONDING);
    const uint32_t setID = u32(Offsets::OFF_ITEMSTATS_ITEM_SET);
    const char *baseName = *reinterpret_cast<const char *const *>(
        record + Offsets::OFF_ITEMSTATS_NAME);
    const char *description = *reinterpret_cast<const char *const *>(
        record + Offsets::OFF_ITEMSTATS_DESCRIPTION);

    // A random-enchant link carries a suffix — apply it to both the display name
    // ("… of the Bear") and the reconstructed link. With no suffix these reduce
    // to the base name and a plain link.
    char suffixedName[128];
    const char *name = baseName;
    if (arg.suffix != 0 &&
        NameWithSuffix(static_cast<uint32_t>(itemID), arg.suffix, suffixedName,
                       sizeof(suffixedName)))
        name = suffixedName;

    char iconPath[260] = {0};
    BuildIconPath(displayInfoID, iconPath, sizeof(iconPath));

    char link[320];
    BuildItemLink(static_cast<uint32_t>(itemID), quality, name, arg.suffix, arg.seed,
                  link, sizeof(link));

    Game::Lua::PushString(L, EmptyIfNull(name));                         // 1  itemName
    Game::Lua::PushString(L, link);                                      // 2  itemLink
    Game::Lua::PushNumber(L, static_cast<double>(quality));              // 3  itemQuality
    Game::Lua::PushNumber(L, static_cast<double>(itemLevel));            // 4  itemLevel
    Game::Lua::PushNumber(L, static_cast<double>(minLevel));             // 5  itemMinLevel
    Game::Lua::PushString(L, LookupItemClassName(classID));              // 6  itemType
    Game::Lua::PushString(L, LookupItemSubClassName(classID, subClassID)); // 7 itemSubType
    Game::Lua::PushNumber(L, static_cast<double>(stackCount));           // 8  itemStackCount
    Game::Lua::PushString(L, LookupInvType(invType));                    // 9  itemEquipLoc
    Game::Lua::PushString(L, iconPath);                                  // 10 itemTexture (path)
    Game::Lua::PushNumber(L, static_cast<double>(sellPrice));            // 11 sellPrice
    Game::Lua::PushNumber(L, static_cast<double>(classID));              // 12 classID
    Game::Lua::PushNumber(L, static_cast<double>(subClassID));           // 13 subclassID
    Game::Lua::PushNumber(L, static_cast<double>(bindType));             // 14 bindType
    Game::Lua::PushNumber(L, 254.0);                                     // 15 expansionID (Classic)
    if (setID != 0)
        Game::Lua::PushNumber(L, static_cast<double>(setID));           // 16 setID
    else
        Game::Lua::PushNil(L);
    Game::Lua::PushBool(L, false);                                       // 17 isCraftingReagent
    Game::Lua::PushString(L, EmptyIfNull(description));                  // 18 itemDescription
    return 18;
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
    Game::Lua::RegisterTableFunction("C_Item", "GetItemInfo",
                                     &Script_C_Item_GetItemInfo);
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
