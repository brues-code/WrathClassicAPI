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
detours on hot dispatch sites). Currently adding various functions and events as I think of them.

[aw]: https://github.com/FrostAtom/awesome_wotlk

## What's added

Per-function reference with shape, semantics, and edge cases is in
[`docs/API.md`](docs/API.md). This section is a flat catalogue.

### Lua calls

| Namespace | Calls |
|-----------|-------|
| AddOns    | `C_AddOns.GetAddOnLocalTable` |
| Events    | `C_EventUtils.IsEventValid` |
| Expansion | `GetClassicExpansionLevel`, `ClassicExpansionAtLeast`, `ClassicExpansionAtMost` |
| Gossip    | `C_GossipInfo.GetText`, `C_GossipInfo.GetOptions`, `C_GossipInfo.GetAvailableQuests`, `C_GossipInfo.GetActiveQuests`, `C_GossipInfo.GetNumOptions`, `C_GossipInfo.GetNumAvailableQuests`, `C_GossipInfo.GetNumActiveQuests`, `C_GossipInfo.SelectOption`, `C_GossipInfo.SelectOptionByIndex`, `C_GossipInfo.SelectAvailableQuest`, `C_GossipInfo.SelectActiveQuest`, `C_GossipInfo.CloseGossip` |
| Item      | `C_Item.DoesItemExist`, `C_Item.DoesItemExistByID`, `C_Item.GetCurrentItemLevel`, `C_Item.GetDetailedItemLevelInfo`, `C_Item.GetItemIcon`, `C_Item.GetItemIconByID`, `C_Item.GetItemGUID`, `C_Item.GetItemID`, `C_Item.GetItemInfoInstant`, `C_Item.GetItemInventoryType`, `C_Item.GetItemInventoryTypeByID`, `C_Item.GetItemLink`, `C_Item.GetItemLocation`, `C_Item.GetItemMaxStackSize`, `C_Item.GetItemMaxStackSizeByID`, `C_Item.GetItemName`, `C_Item.GetItemNameByID`, `C_Item.GetItemQuality`, `C_Item.GetItemQualityByID`, `C_Item.GetItemSpell`, `C_Item.IsBound`, `C_Item.IsItemDataCached`, `C_Item.IsItemDataCachedByID`, `C_Item.IsLocked`, `C_Item.RequestLoadItemData`, `C_Item.RequestLoadItemDataByID` |
| Quest Log | `C_QuestLog.GetQuestIDForLogIndex`, `C_QuestLog.ReadyForTurnIn`, `C_QuestLog.GetTitleForQuestID`, `C_QuestLog.RequestLoadQuestByID` |
| Spell     | `IsPlayerSpell` |
| Talent    | `GetTalentSpellID`, `GetTalentIDByIndex` |
| Time      | `GetServerTime`, `C_DateAndTime.AdjustTimeByDays`, `C_DateAndTime.AdjustTimeByMinutes`, `C_DateAndTime.CompareCalendarTime`, `C_DateAndTime.GetCalendarTimeFromEpoch`, `C_DateAndTime.GetCurrentCalendarTime`, `C_DateAndTime.GetSecondsUntilDailyReset`, `C_DateAndTime.GetServerTimeLocal` |
| Timer     | `C_Timer.After`, `C_Timer.NewTimer`, `C_Timer.NewTicker` |
| Tooltip   | `GameTooltip:HasSpell`, `GameTooltip:HasItem`, `GameTooltip:HasUnit`
| UI Color  | `C_UIColor.GetColors` |
| Unit      | `UnitClassID` |
| Unit Auras | `C_UnitAuras.GetAuraDataByIndex`, `C_UnitAuras.GetBuffDataByIndex`, `C_UnitAuras.GetDebuffDataByIndex`, `C_UnitAuras.GetUnitAuraBySpellID`, `C_UnitAuras.GetPlayerAuraBySpellID`, `C_UnitAuras.GetUnitAuras`, `C_UnitAuras.GetAuraDispelTypeColor` |

### Globals

| Group | Constants |
|-------|-----------|
| Version | `WRATH_CLASSIC_API_VERSION` |
| Expansion | `LE_EXPANSION_LEVEL_CURRENT`, `LE_EXPANSION_CLASSIC` … `LE_EXPANSION_MIDNIGHT` |

### Events

| Event | Payload | When it fires |
|-------|---------|---------------|
| `GET_ITEM_INFO_RECEIVED` | `itemID, success` | The engine has just filled the item-stats cache from an `SMSG_ITEM_QUERY_SINGLE_RESPONSE` triggered by an **implicit** path (`GetItemInfo(uncachedID)`, hyperlink hover, chat-link resolution, etc.) |
| `ITEM_DATA_LOAD_RESULT`  | `itemID, success` | The engine has just filled the cache for an **explicit** `C_Item.RequestLoadItemData(ByID)` call |
| `QUEST_DATA_LOAD_RESULT` | `questID, success` | The engine has just filled the quest static-info cache for an **explicit** `C_QuestLog.RequestLoadQuestByID` call |

A given cache fill fires exactly one of these — never both — depending
on what initiated the request. Same split as modern WoW.

### Client extensions

| Feature | Change |
|---------|--------|
| `GetItemInfo(itemID\|"item:N..."\|"name")` | The 3.3.5 implementation returns nil on cache misses with no follow-up query. We hook it so a miss now kicks off `SMSG_ITEM_QUERY_SINGLE` transparently; the original still returns nil this call, but subsequent calls return data and `GET_ITEM_INFO_RECEIVED` fires when the response arrives. Same shape as modern WoW (5.4+). |
| `GameTooltip:SetSpellByID(spellID)` | The 3.3.5 implementation gates on a spellbook+petbar walk and silently no-ops for any spell not in those displayable structures (profession recipes, item-granted spells, anything else the engine tracks only in the player-spell bitmap). We hook the gate to allow any non-zero spellID. Same shape as modern WoW (5.4+) where Blizzard removed the gate. |
| Embedded `!!!WrathClassicAPI` addon | The DLL embeds a Lua utility addon (`Mixin`, `EventRegistry` + `CallbackRegistryMixin`, `ColorMixin`/`ColorUtil`, `ItemUtil`/`ItemLocation`, `EnumUtil`, `TableUtil`, `MathUtil`, `Pools`, `EventUtil`, `FunctionUtil`, `LinkUtil`, `PlayerUtil`, `EquipmentManager`, timed callbacks, frame watching) and registers it at login — nothing to install on disk. It loads before all other addons, is hidden from the AddOns list and force-enabled, and loads as Blizzard-secure code (its closures don't taint protected paths). A newer on-disk copy at `Interface\AddOns\!!!WrathClassicAPI` overrides the embedded one (release DLLs stamp the embedded `## Version:` from the git tag; local `DEV` builds always defer to disk; a `.wrathclassicapi-dev` marker forces disk). |
| Retail-like `/reload` (hot reload) | A stock 3.3.5 client fixes its view of the game folder at launch, so new files need a restart. With the DLL, `/reload` picks up everything: new addon folders load as normal addons, new files in existing addons load, a first-time SavedVariables file survives (stock clients lose a fresh addon's first save), `##` metadata edits take effect (`## SavedVariables:`, `## Dependencies:`, ...), and deleted folders drop from the addon list. |

## Building

```powershell
git submodule update --init --recursive    # vendored MinHook
cmake -B build -A Win32
cmake --build build --config Release
```

Output: `build/Release/WrathClassicAPI.dll`. Must be **Win32 (x86)** —
the target `Wow.exe` is 32-bit and won't load an x64 DLL.

To stamp a version into `WRATH_CLASSIC_API_VERSION`, pass
`-DWRATHCLASSICAPI_TAG=vX.Y.Z` at configure time; the value exposed to
Lua will be `X*10000 + Y*100 + Z`.

## Deploying

WrathClassicAPI is loaded via [LichLoader][lichloader]. Copy
`WrathClassicAPI.dll` into the same directory as your `Wow.exe` (or a
`dll\` subdirectory next to it) and add its path to `lichloader.txt`:

```
dll\WrathClassicAPI.dll
```

Launch via `LichLoader.exe`. `LichCore.dll` calls the DLL's exported
`Load()` on the game's main thread after boot — that's when the hooks
install (off the Windows loader lock, after every injected DLL's
`DllMain` has completed). Without LichCore present, the DLL falls back
to installing from a worker thread.

[lichloader]: https://github.com/brues-code/LichLoader
