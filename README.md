# WrathClassicAPI

A small DLL for World of Warcraft 3.3.5a (Wrath of the Lich King /
ChromieCraft) that adds a handful of modern Lua API calls Blizzard
introduced after WotLK shipped — primarily to backport addons written
against later API versions (4.x / 5.x / 8.x+) where these calls exist.

The DLL hooks the FrameScript engine after WoW boots and registers its
extensions through the same in-engine mechanisms the engine itself
uses for Lua functions and events. No companion addon required.

This is the WotLK 3.3.5a sibling of [ClassicAPI][classicapi] (1.12).
Mirrors its module pattern and conventions; only the engine offsets
and the Lua-state ABI differ (3.3.5 moved Lua to 5.1 and the C
function ABI to `__cdecl`).

[classicapi]: https://github.com/brues-code/ClassicAPI

## Status

Verified in-game on ChromieCraft 3.3.5a. Loads cleanly alongside
[awesome_wotlk][aw]; coexistence is non-conflicting (no shared
detours on hot dispatch sites). The work-in-progress scope is "the
modern item-cache API surface plus the supporting event-injection
plumbing"; more features will follow.

[aw]: https://github.com/FrostAtom/awesome_wotlk

## What's added

Per-function reference with shape, semantics, and edge cases is in
[`docs/API.md`](docs/API.md). This section is a flat catalogue.

### Lua calls

| Namespace | Calls |
|-----------|-------|
| Events    | `C_EventUtils.IsEventValid` |
| Expansion | `GetClassicExpansionLevel`, `ClassicExpansionAtLeast`, `ClassicExpansionAtMost` |
| Item      | `C_Item.DoesItemExist`, `C_Item.DoesItemExistByID`, `C_Item.GetCurrentItemLevel`, `C_Item.GetDetailedItemLevelInfo`, `C_Item.GetItemIcon`, `C_Item.GetItemIconByID`, `C_Item.GetItemGUID`, `C_Item.GetItemID`, `C_Item.GetItemInfoInstant`, `C_Item.GetItemInventoryType`, `C_Item.GetItemInventoryTypeByID`, `C_Item.GetItemLink`, `C_Item.GetItemMaxStackSize`, `C_Item.GetItemMaxStackSizeByID`, `C_Item.GetItemName`, `C_Item.GetItemNameByID`, `C_Item.GetItemQuality`, `C_Item.GetItemQualityByID`, `C_Item.IsItemDataCached`, `C_Item.IsItemDataCachedByID`, `C_Item.IsLocked`, `C_Item.RequestLoadItemData`, `C_Item.RequestLoadItemDataByID` |
| Spell     | `IsPlayerSpell` |
| Talent    | `GetTalentSpellID`, `GetTalentIDByIndex` |
| Time      | `GetServerTime`, `C_DateAndTime.AdjustTimeByDays`, `C_DateAndTime.AdjustTimeByMinutes`, `C_DateAndTime.CompareCalendarTime`, `C_DateAndTime.GetCalendarTimeFromEpoch`, `C_DateAndTime.GetCurrentCalendarTime`, `C_DateAndTime.GetSecondsUntilDailyReset`, `C_DateAndTime.GetServerTimeLocal` |
| Tooltip   | `GameTooltip:HasSpell`, `GameTooltip:HasItem`, `GameTooltip:HasUnit`
| UI Color  | `C_UIColor.GetColors` |
| Unit      | `UnitClassID` |

### Globals

| Group | Constants |
|-------|-----------|
| Expansion | `LE_EXPANSION_LEVEL_CURRENT`, `LE_EXPANSION_CLASSIC` … `LE_EXPANSION_MIDNIGHT` |

### Events

| Event | Payload | When it fires |
|-------|---------|---------------|
| `GET_ITEM_INFO_RECEIVED` | `itemID, success` | The engine has just filled the item-stats cache from an `SMSG_ITEM_QUERY_SINGLE_RESPONSE` triggered by an **implicit** path (`GetItemInfo(uncachedID)`, hyperlink hover, chat-link resolution, etc.) |
| `ITEM_DATA_LOAD_RESULT`  | `itemID, success` | The engine has just filled the cache for an **explicit** `C_Item.RequestLoadItemData(ByID)` call |

A given cache fill fires exactly one of these — never both — depending
on what initiated the request. Same split as modern WoW.

### Behavioral extensions

| Function | Change |
|----------|--------|
| `GetItemInfo(itemID|"item:N..."|"name")` | The 3.3.5 implementation returns nil on cache misses with no follow-up query. We hook it so a miss now kicks off `SMSG_ITEM_QUERY_SINGLE` transparently; the original still returns nil this call, but subsequent calls return data and `GET_ITEM_INFO_RECEIVED` fires when the response arrives. Same shape as modern WoW (5.4+). |
| `GameTooltip:SetSpellByID(spellID)` | The 3.3.5 implementation gates on a spellbook+petbar walk and silently no-ops for any spell not in those displayable structures (profession recipes, item-granted spells, anything else the engine tracks only in the player-spell bitmap). We hook the gate to allow any non-zero spellID. Same shape as modern WoW (5.4+) where Blizzard removed the gate. |

### itemLocation argument shape

The `_ByID`-suffixed `C_Item` calls take an integer item ID. The
unsuffixed variants take a Lua table — same as the modern
[ItemLocation mixin][item-location] — or a GUID string:

```lua
{ equipmentSlotIndex = N }      -- character-pane slot, 1..19
{ bagID = 0, slotIndex = N }    -- backpack, 1..16
{ bagID = 1..4, slotIndex = N } -- equipped bag
"0xHHHHHHHHLLLLLLLL"            -- engine GUID string (with or without "0x")
```

The string form is parsed via the engine's `HexString2Guid` + resolved
via `ObjectMgr::Get` with the ITEM type-mask. Negative `bagID` values
(keyring, bank) and non-item GUIDs both return nil-equivalent.

[item-location]: https://warcraft.wiki.gg/wiki/ItemLocationMixin

## Building

```powershell
git submodule update --init --recursive    # vendored MinHook
cmake -B build -A Win32
cmake --build build --config Release
```

Output: `build/Release/WrathClassicAPI.dll`. Must be **Win32 (x86)** —
the target `Wow.exe` is 32-bit and won't load an x64 DLL.

## Deploying

WrathClassicAPI is loaded via [LichLoader][lichloader]. Copy
`WrathClassicAPI.dll` into the same directory as your `Wow.exe` (or a
`dll\` subdirectory next to it) and add its path to `dlls.txt`:

```
dll\WrathClassicAPI.dll
```

Launch via `LichLoader.exe`. Hooks install before the game's main
thread starts.

[lichloader]: https://github.com/brues-code/LichLoader
