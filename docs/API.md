# WrathClassicAPI — Lua Reference

Per-function reference for the Lua surface WrathClassicAPI adds to the
3.3.5a Lua environment. See the [project README](../README.md) for
build / install instructions and a high-level summary; this file
documents shape, semantics, and edge cases per call.

Conventions:

- "`ByID` variant" means the call accepts a numeric `itemID` (or a
  `"item:N..."` string / full hyperlink). The non-`ByID` variant
  accepts an `itemLocation` — table form `{bagID, slotIndex}` or
  `{equipmentSlotIndex}`, or a GUID string `"0xHHHHHHHHLLLLLLLL"`.
- "Returns nil on cache miss" means the call fires `WarmCache` so a
  follow-up call after `GET_ITEM_INFO_RECEIVED` lands the data —
  same behavior as the modern `GetItemInfo` (5.4+) on cache misses.
- All calls that read DBC data are read-only — they don't trigger
  network traffic except where explicitly noted.

## Contents

- [Events](#events)
  - [`C_EventUtils.IsEventValid(eventName)`](#c_eventutilsiseventvalideventname)
  - [`GET_ITEM_INFO_RECEIVED` event](#get_item_info_received-event)
  - [`ITEM_DATA_LOAD_RESULT` event](#item_data_load_result-event)
  - [`QUEST_DATA_LOAD_RESULT` event](#quest_data_load_result-event)
- [Expansion](#expansion)
  - [`GetClassicExpansionLevel()`](#getclassicexpansionlevel)
  - [`ClassicExpansionAtLeast(level)`](#classicexpansionatleastlevel)
  - [`ClassicExpansionAtMost(level)`](#classicexpansionatmostlevel)
- [Gossip](#gossip)
  - [`C_GossipInfo.GetText()`](#c_gossipinfogettext)
  - [`C_GossipInfo.GetOptions()`](#c_gossipinfogetoptions)
  - [`C_GossipInfo.GetAvailableQuests()` / `GetActiveQuests()`](#c_gossipinfogetavailablequests--getactivequests)
  - [`C_GossipInfo.GetNumOptions()` / `GetNumAvailableQuests()` / `GetNumActiveQuests()`](#c_gossipinfogetnumoptions--getnumavailablequests--getnumactivequests)
  - [`C_GossipInfo.SelectOption(gossipOptionID[, text[, copperCost]])`](#c_gossipinfoselectoptiongossipoptionid-text-coppercost)
  - [`C_GossipInfo.SelectOptionByIndex(orderIndex)`](#c_gossipinfoselectoptionbyindexorderindex)
  - [`C_GossipInfo.SelectAvailableQuest(questID)` / `SelectActiveQuest(questID)`](#c_gossipinfoselectavailablequestquestid--selectactivequestquestid)
  - [`C_GossipInfo.CloseGossip()`](#c_gossipinfoclosegossip)
- [Item](#item)
  - [`C_Item.GetItemID(itemLocation)` / `GetItemGUID`](#c_itemgetitemiditemlocation)
  - [`C_Item.GetItemLocation(itemGUID)`](#c_itemgetitemlocationitemguid)
  - [`C_Item.GetItemInfoInstant(item)`](#c_itemgetiteminfoinstantitem)
  - [`C_Item.DoesItemExist[ByID]`](#c_itemdoesitemexistitemlocation--doesitemexistbyiditem)
  - [`C_Item.GetItemQuality[ByID]`](#c_itemgetitemqualityitemlocation--getitemqualitybyiditem)
  - [`C_Item.GetItemMaxStackSize[ByID]`](#c_itemgetitemmaxstacksizeitemlocation--getitemmaxstacksizebyiditem)
  - [`C_Item.GetCurrentItemLevel` / `GetDetailedItemLevelInfo`](#c_itemgetcurrentitemlevelitemlocation--getdetaileditemlevelinfoitem)
  - [`C_Item.GetItemInventoryType[ByID]`](#c_itemgetiteminventorytypeitemlocation--getiteminventorytypebyiditem)
  - [`C_Item.GetItemIcon[ByID]`](#c_itemgetitemiconitemlocation--getitemiconbyiditem)
  - [`C_Item.GetItemName[ByID]`](#c_itemgetitemnameitemlocation--getitemnamebyiditem)
  - [`C_Item.GetItemLink(itemLocation)`](#c_itemgetitemlinkitemlocation)
  - [`C_Item.IsItemDataCached[ByID]` / `RequestLoadItemData[ByID]`](#c_itemisitemdatacacheditemlocation--isitemdatacachedbyiditem)
  - [`C_Item.IsLocked(itemLocation)`](#c_itemislockeditemlocation)
  - [`C_Item.IsBound(itemLocation)`](#c_itemisbounditemlocation)
- [Quest Log](#quest-log)
  - [`C_QuestLog.GetTitleForQuestID(questID)`](#c_questloggettitleforquestidquestid)
  - [`C_QuestLog.RequestLoadQuestByID(questID)`](#c_questlogrequestloadquestbyidquestid)
- [Spell](#spell)
  - [`IsPlayerSpell(spellID)`](#isplayerspellspellid)
- [Talent](#talent)
  - [`GetTalentSpellID(tabIndex, talentIndex[, isInspect, isPet, groupIndex, rank])`](#gettalentspellidtabindex-talentindex-isinspect-ispet-groupindex-rank)
  - [`GetTalentIDByIndex(tabIndex, talentIndex[, isInspect, isPet, groupIndex])`](#gettalentidbyindextabindex-talentindex-isinspect-ispet-groupindex)
- [Time](#time)
  - [`GetServerTime()`](#getservertime)
  - [`C_DateAndTime.GetCurrentCalendarTime()`](#c_dateandtimegetcurrentcalendartime)
  - [`C_DateAndTime.GetCalendarTimeFromEpoch(epoch)`](#c_dateandtimegetcalendartimefromepochepoch)
  - [`C_DateAndTime.AdjustTimeByDays(t, days)` / `AdjustTimeByMinutes(t, minutes)`](#c_dateandtimeadjusttimebydayst-days--adjusttimebyminutest-minutes)
  - [`C_DateAndTime.CompareCalendarTime(lhs, rhs)`](#c_dateandtimecomparecalendartimelhs-rhs)
  - [`C_DateAndTime.GetSecondsUntilDailyReset()`](#c_dateandtimegetsecondsuntildailyreset)
  - [`C_DateAndTime.GetServerTimeLocal()`](#c_dateandtimegetservertimelocal)
- [Timer](#timer)
  - [`C_Timer.After(seconds, callback)`](#c_timerafterseconds-callback)
  - [`C_Timer.NewTimer(seconds, callback)`](#c_timernewtimerseconds-callback)
  - [`C_Timer.NewTicker(seconds, callback[, iterations])`](#c_timernewtickerseconds-callback-iterations)
- [Tooltip](#tooltip)
  - [`GameTooltip:HasSpell()`](#gametooltiphasspell)
  - [`GameTooltip:HasItem()`](#gametooltiphasitem)
  - [`GameTooltip:HasUnit()`](#gametooltiphasunit)
- [UI Color](#ui-color)
  - [`C_UIColor.GetColors()`](#c_uicolorgetcolors)
- [Unit](#unit)
  - [`UnitClassID(unit)`](#unitclassidunit)
- [Unit Auras](#unit-auras)
  - [`C_UnitAuras.GetAuraDataByIndex(unit, index[, filter])`](#c_unitaurasgetauradatabyindexunit-index-filter)
  - [`C_UnitAuras.GetBuffDataByIndex(unit, index)` / `GetDebuffDataByIndex(unit, index)`](#c_unitaurasgetbuffdatabyindexunit-index--getdebuffdatabyindexunit-index)
  - [`C_UnitAuras.GetUnitAuraBySpellID(unit, spellID[, filter])`](#c_unitaurasgetunitaurabyspellidunit-spellid-filter)
  - [`C_UnitAuras.GetPlayerAuraBySpellID(spellID)`](#c_unitaurasgetplayeraurabyspellidspellid)
  - [`C_UnitAuras.GetUnitAuras(unit[, filter])`](#c_unitaurasgetunitaurasunit-filter)
  - [`C_UnitAuras.GetAuraDispelTypeColor(type)`](#c_unitaurasgetauradispeltypecolortype)
  - [`AuraData` table shape](#auradata-table-shape)
- [Globals](#globals)
  - [`LE_EXPANSION_*`](#le_expansion_)
- [Behavioral extensions](#behavioral-extensions)
  - [`GetItemInfo` — auto cache warmup](#getiteminfo--auto-cache-warmup)
  - [`GameTooltip:SetSpellByID` — works for unknown spells](#gametooltipsetspellbyid--works-for-unknown-spells)
- [Argument shapes](#argument-shapes)
  - [`itemLocation`](#itemlocation)

---

## Events

### `C_EventUtils.IsEventValid(eventName)`

Returns `true` if `eventName` is a string the engine recognizes as a
registerable event (i.e. `frame:RegisterEvent(eventName)` would
succeed). Returns `false` for unknown / empty / non-string input.

```lua
C_EventUtils.IsEventValid("PLAYER_LOGIN")           -- true
C_EventUtils.IsEventValid("GET_ITEM_INFO_RECEIVED") -- true (our custom event)
C_EventUtils.IsEventValid("NOT_A_REAL_EVENT")       -- false
```

Calls into the engine's own event-name hash table, so it covers
every stock event plus any custom event WrathClassicAPI has appended.

### `GET_ITEM_INFO_RECEIVED` event

Payload: `itemID, success`

Fires when the engine has just filled the item-stats cache from an
`SMSG_ITEM_QUERY_SINGLE_RESPONSE` triggered by an **implicit** path —
i.e. one of these:

- `GetItemInfo(uncachedID)` (handled by our cache-warmup hook)
- Hovering a hyperlink with `:SetHyperlink("item:...")`
- The chat link-resolution path
- Any other engine path that pulls item data without an explicit
  `RequestLoadItemData(ByID)` call

```lua
local f = CreateFrame("Frame")
f:RegisterEvent("GET_ITEM_INFO_RECEIVED")
f:SetScript("OnEvent", function(self, event, itemID, success)
    print("cache filled for", itemID, "success:", success)
end)
```

### `ITEM_DATA_LOAD_RESULT` event

Payload: `itemID, success`

Fires when the engine has just filled the cache for an **explicit**
`C_Item.RequestLoadItemData(ByID)` call. A given cache fill fires
exactly one of `GET_ITEM_INFO_RECEIVED` / `ITEM_DATA_LOAD_RESULT` —
never both — depending on what initiated the request. Same split as
modern WoW.

### `QUEST_DATA_LOAD_RESULT` event

Payload: `questID, success`

Fires when the engine has filled the quest static-info cache for an
**explicit** `C_QuestLog.RequestLoadQuestByID` call. `success` is
`1` on a cache hit or successful `SMSG_QUEST_QUERY_RESPONSE`, `0` if
the server rejected the query. Modern WoW (8.0+) addons listen for
this to know when `C_QuestLog.GetTitleForQuestID(questID)` will
return non-nil for a previously uncached quest.

Like its modern counterpart, this fires once per explicit request —
including for quests that were already cached when the request was
made (we synthesize the event so addons get a uniform notification
regardless of cache state).

---

## Expansion

### `GetClassicExpansionLevel()`

Returns the integer identifying the expansion this client targets.
Fixed at compile time for WrathClassicAPI:

```lua
GetClassicExpansionLevel()  -- always 2 (LE_EXPANSION_WRATH_OF_THE_LICH_KING)
```

### `ClassicExpansionAtLeast(level)`

Returns `true` iff `level <= GetClassicExpansionLevel()`. Useful for
addons that want to guard "this code path is for WotLK or later":

```lua
if ClassicExpansionAtLeast(LE_EXPANSION_WRATH_OF_THE_LICH_KING) then
    -- WotLK-or-newer code path
end
```

### `ClassicExpansionAtMost(level)`

Returns `true` iff `level >= GetClassicExpansionLevel()`. Mirror of
`ClassicExpansionAtLeast` for upper-bound checks.

---

## Gossip

Modern table-shaped wrappers around 3.3.5's flat
`GetGossipText` / `GetGossipOptions` / `GetGossip*Quests` /
`SelectGossip*` surface. All getters read directly from the
engine's two gossip-state arrays (populated by
`SMSG_GOSSIP_MESSAGE` and cleared each open); selectors
translate the modern arg shape back to the engine's slot index
and call the engine helpers directly so we share the
CMSG-send path and money / password gating.

Fields the 3.3.5 wire protocol doesn't transmit are omitted
(modern `rewards` / `spellID` / per-option `status`, modern UX
hints like `overrideIconID` / `selectOptionWhenOnlyOption`).

### `C_GossipInfo.GetText()`

Returns the greeting string the engine resolved for the
gossip-giver's `NPC_TEXT.dbc` entry, or empty string when no
gossip frame is open.

### `C_GossipInfo.GetOptions()`

Returns an array (1-indexed) of `GossipOptionUIInfo` tables in
display order:

| Field | Type | Notes |
|-------|------|-------|
| `gossipOptionID` | number | Stable engine option ID — same value `SelectOption` matches against. |
| `name` | string | Option text. |
| `icon` | number | Engine gossip-type byte (0..N: gossip / vendor / taxi / trainer / healer / binder / banker / petition / tabard / battlemaster / auctioneer). NOT a retail-style fileID. |
| `flags` | number | Bit 0 = `boxCoded` (option requires a password). |
| `moneyCost` | number | Copper required to take the option (added in 3.3.5; `0` for free options). |
| `orderIndex` | number | 1-based display position. Matches `SelectOptionByIndex`'s arg. |

### `C_GossipInfo.GetAvailableQuests()` / `GetActiveQuests()`

Return arrays of `GossipQuestUIInfo` tables. `GetAvailableQuests`
covers quests the giver offers but the player hasn't taken;
`GetActiveQuests` covers quests in the player's log that the
giver tracks. Per-entry fields:

| Field | Type | When |
|-------|------|------|
| `questID` | number | Always. |
| `title` | string | Always. Inline buffer from the gossip packet. |
| `questLevel` | number | Always. |
| `repeatable` | boolean | Always. Flag bit `0x1000`. |
| `isComplete` | boolean | Active-only. `true` when ready to turn in. |

### `C_GossipInfo.GetNumOptions()` / `GetNumAvailableQuests()` / `GetNumActiveQuests()`

Count-only variants. Avoid the table allocations when all you
need is "are there any?".

### `C_GossipInfo.SelectOption(gossipOptionID[, text[, copperCost]])`

Selects the option with matching `gossipOptionID`. `text` is the
password for `boxCoded` options; `copperCost` is required for
money-charging options (3.3.5 added option-level money — pass `0`
for free options, which is the engine's default).

No-op if the gossip frame is closed or the option ID isn't
currently in the array.

### `C_GossipInfo.SelectOptionByIndex(orderIndex)`

Selects by 1-based display position rather than ID. Matches the
`orderIndex` field returned by `GetOptions()`. Doesn't accept a
password — use `SelectOption` for `boxCoded` options.

### `C_GossipInfo.SelectAvailableQuest(questID)` / `SelectActiveQuest(questID)`

Accepts the available quest or hands in the active quest with the
matching `questID`. No-op if `questID` isn't in the respective
filtered list.

### `C_GossipInfo.CloseGossip()`

Closes the gossip frame and clears the engine's gossip state.
Same effect as clicking the X / pressing Escape — delegates to
the engine's `Script_CloseGossip` so the CMSG path is verbatim.

---

## Item

Every "ByID" call accepts a number, a `"item:N..."` string, or a full
hyperlink. Every location-based call accepts an `itemLocation` table
or a GUID string — see [Argument shapes](#argument-shapes) below.

### `C_Item.GetItemID(itemLocation)`

Returns the integer `itemID` at the given inventory location, or
`nil` for an empty slot / invalid arg.

```lua
C_Item.GetItemID({equipmentSlotIndex = 16})           -- main hand item ID
C_Item.GetItemID({bagID = 0, slotIndex = 1})          -- first backpack slot
C_Item.GetItemID("0x4000000083ECA16C")                -- by GUID
```

### `C_Item.GetItemGUID(itemLocation)`

Returns the engine GUID string for the item at the given location,
in the form `"0xHHHHHHHHHHHHHHHH"` (uppercase, 18 chars including
the `0x` prefix). Returns `nil` for an empty slot / invalid arg.
The returned string is stable across inventory moves and can be fed
back to any `C_Item.*` accessor that takes an `itemLocation`.

```lua
local guid = C_Item.GetItemGUID({equipmentSlotIndex = 16})
-- e.g. "0x4000000083ECA16C"
C_Item.GetItemQuality(guid)  -- works
```

### `C_Item.GetItemLocation(itemGUID)`

Inverse of `GetItemGUID`. Takes a GUID string
(`"0xHHHHHHHHLLLLLLLL"`, with or without the `0x` prefix) and
returns the `itemLocation` table for where that item currently
lives in the player's inventory, or `nil` if it isn't held by the
player.

```lua
local guid = C_Item.GetItemGUID({bagID = 0, slotIndex = 1})
local loc = C_Item.GetItemLocation(guid)
-- loc = { bagID = 0, slotIndex = 1 }
C_Item.GetItemName(loc)  -- works on the returned table directly
```

Returns:

- `{ equipmentSlotIndex = N }` for items in character-pane slots 1..19
- `{ bagID = B, slotIndex = S }` for items in backpack (`B=0`) or
  equipped bags (`B=1..4`)
- `nil` for unknown / malformed GUIDs, items the player doesn't own
  (trade items, auction listings, etc.), or non-item GUIDs (units,
  players)

Implementation walks the player's equipment + backpack + bags
comparing CGItem pointers — modern WoW returns an `ItemLocation`
mixin object backed by the GUID itself, but our addon-side
[`ItemLocationMixin`](https://warcraft.wiki.gg/wiki/ItemLocationMixin)
only supports table-shape locations, so we resolve the GUID to a
concrete `(bagID, slotIndex)` or `equipmentSlotIndex` at call
time. Keyring / bank / mail / void-storage slots aren't covered
(those use different inventory managers in 3.3.5).

### `C_Item.GetItemInfoInstant(item)`

Returns 7 values without requiring a fully-cached item-stats record
beyond the basic DBC lookups the engine already has resident:

```lua
local itemID, itemType, itemSubType, equipLoc, icon, classID, subClassID
    = C_Item.GetItemInfoInstant(itemInfo)
```

Returns `nil` (= no values) for cache miss; fires `WarmCache` so a
follow-up call after `GET_ITEM_INFO_RECEIVED` lands the data.

```lua
C_Item.GetItemInfoInstant(6948)
-- 6948, "Miscellaneous", "Junk", "", "Interface\\Icons\\INV_Misc_Rune_01", 15, 0

C_Item.GetItemInfoInstant(7005)
-- 7005, "Weapon", "Miscellaneous", "INVTYPE_WEAPON", "Interface\\Icons\\INV_Weapon_ShortBlade_01", 2, 14
```

Unlike modern WoW, 3.3.5 has no separate "instant" cache — uncached
items return nil here, same as `GetItemInfo` does. The auto-warmup
mitigates this for the second call onward.

### `C_Item.DoesItemExist(itemLocation)` / `DoesItemExistByID(item)`

`DoesItemExist` returns `true` iff the location resolves to a
populated inventory slot on the active player. Empty slots and
invalid tables return `false` without raising.

`DoesItemExistByID` returns `true` iff the cache currently has data
for the itemID. Cache-miss returns `false` but kicks off the network
query so a follow-up call lands the value.

### `C_Item.GetItemQuality(itemLocation)` / `GetItemQualityByID(item)`

Returns the item quality (0 = poor / 1 = common / 2 = uncommon /
3 = rare / 4 = epic / 5 = legendary / 6 = artifact / 7 = heirloom).
Returns `nil` (= no values) for cache miss / invalid arg.

### `C_Item.GetItemMaxStackSize(itemLocation)` / `GetItemMaxStackSizeByID(item)`

Returns the item's maximum stack count. Returns `nil` for cache miss
/ invalid arg.

### `C_Item.GetCurrentItemLevel(itemLocation)` / `GetDetailedItemLevelInfo(item)`

Both return the item's base level. Modern WoW's `GetDetailedItemLevelInfo`
returns three values (effective, isPreview, base); 3.3.5 has no
scaling / upgrades, so we just return the single integer. Callers
that wrap the call in parens — `local lvl = (C_Item.GetDetailedItemLevelInfo(x))` —
work identically against modern and us.

### `C_Item.GetItemInventoryType(itemLocation)` / `GetItemInventoryTypeByID(item)`

Returns the integer `INVTYPE` enum value (0 = non-equip, 1 = head,
2 = neck, …, 17 = ranged, etc.). For the string form (e.g.
`"INVTYPE_WEAPON"`), use `GetItemInfoInstant`'s 4th return.

### `C_Item.GetItemIcon(itemLocation)` / `GetItemIconByID(item)`

Returns the full icon texture path: `"Interface\Icons\<basename>"`.
Returns `nil` for cache miss / invalid arg.

```lua
C_Item.GetItemIconByID(6948)
-- "Interface\\Icons\\INV_Misc_Rune_01"
```

### `C_Item.GetItemName(itemLocation)` / `GetItemNameByID(item)`

Returns the item's base name (no color codes, no suffixes). Returns
`nil` for cache miss / invalid arg.

### `C_Item.GetItemLink(itemLocation)`

Returns the full colored item link `"|cffXXXXXX|Hitem:…|h[Name]|h|r"`,
with the local player's level baked into the link's level field —
exactly what stock `GetItemInfo`'s second return produces for the
same itemID. Returns `nil` for an empty slot / invalid arg.

### `C_Item.IsItemDataCached(itemLocation)` / `IsItemDataCachedByID(item)`

Returns `true` iff the item-stats cache currently has data for the
item. Does NOT trigger a network query — for that, use
`RequestLoadItemData(ByID)`.

### `C_Item.RequestLoadItemData(itemLocation)` / `RequestLoadItemDataByID(item)`

Triggers an explicit cache fill if the item isn't cached. When the
response arrives, `ITEM_DATA_LOAD_RESULT` fires (not
`GET_ITEM_INFO_RECEIVED` — that's reserved for the implicit-warmup
paths).

### `C_Item.IsLocked(itemLocation)`

Returns whether the item is currently transient-locked (during
trade / mail / loot interactions). **Currently a stub** that always
returns `false` — the ITEM_FIELD_FLAGS bit hasn't been mapped on
this build. Safe to use; just won't return `true` when the lock is
actually set.

### `C_Item.IsBound(itemLocation)`

Returns `true` if the item is currently bound to the player.
Matches modern semantics: covers both soulbound items (regular
BoP after pickup, BoE after equip, quest items, etc.) **and**
account-bound heirlooms.

```lua
C_Item.IsBound({equipmentSlotIndex = 16})  -- main hand: true once equipped
C_Item.IsBound({bagID = 0, slotIndex = 1}) -- backpack slot 1
```

Implementation: delegates to the engine's `CGItem::IsSoulbound`
helper (the same predicate the tooltip builder uses to gate the
bind-label line) which handles the per-instance soulbound bit
plus the uncommon "enchantment bound the item" path. If that
returns `false`, we additionally check the item-stats record for
the `ITEM_FLAG_ACCOUNT_BOUND` proto flag (bit 27) so heirlooms
register as bound too — modern `C_Item.IsBound` returns `true`
for them since they can't leave the account.

Returns `false` for empty slots, malformed `itemLocation`, and
items whose stats record isn't cached yet (pair with
`C_Item.RequestLoadItemData(itemLocation)` if you're querying a
recently-seen item that might not be loaded).

---

## Quest Log

A pair of modern (8.0+) static-info accessors layered over 3.3.5's
existing `questcache.wdb` infrastructure. Pair them: call
`RequestLoadQuestByID` when you want a quest, listen for
`QUEST_DATA_LOAD_RESULT`, then read with `GetTitleForQuestID` once
the event fires.

### `C_QuestLog.GetTitleForQuestID(questID)`

Returns the locale-applied quest title from the engine's quest
static-info cache, or `nil` if the cache hasn't loaded the record yet.

```lua
C_QuestLog.GetTitleForQuestID(70)    -- "Hare Today, Gone Tomorrow" (if cached)
C_QuestLog.GetTitleForQuestID(99999) -- nil for unknown / uncached
```

Title-only getter — doesn't auto-warm the cache. For quests that
might not be cached yet (because the player has never visited the
giver and the title hasn't come up via tooltip / chatlink), pair
this with `C_QuestLog.RequestLoadQuestByID(questID)` and listen for
[`QUEST_DATA_LOAD_RESULT`](#quest_data_load_result-event).

Cache state is independent of the player's active quest log —
quests the player has never seen can still resolve once their
`SMSG_QUEST_QUERY_RESPONSE` has been processed (e.g. after a
hyperlink hover, a chat-link click, or our explicit request path).

### `C_QuestLog.RequestLoadQuestByID(questID)`

Kicks off a `CMSG_QUEST_QUERY` for `questID` if its data isn't
already cached, then fires
[`QUEST_DATA_LOAD_RESULT(questID, success)`](#quest_data_load_result-event)
when the response arrives (or immediately, if it was already
cached).

```lua
local function ReadyTitle(questID, callback)
    if C_QuestLog.GetTitleForQuestID(questID) then
        callback(C_QuestLog.GetTitleForQuestID(questID))
        return
    end
    local f = CreateFrame("Frame")
    f:RegisterEvent("QUEST_DATA_LOAD_RESULT")
    f:SetScript("OnEvent", function(_, _, id, success)
        if id == questID then
            f:UnregisterAllEvents()
            callback(success == 1 and C_QuestLog.GetTitleForQuestID(id) or nil)
        end
    end)
    C_QuestLog.RequestLoadQuestByID(questID)
end
```

Returns nothing — same as modern WoW. The completion event is the
contract.

---

## Spell

### `IsPlayerSpell(spellID)`

Returns `true` iff the player has learned `spellID` from any source:
trained class abilities, racials, talent passives, profession recipes
(including those learned from vendors or discovered via trade-skill
crit), or any other path that triggers `SMSG_LEARNED_SPELL` on the
server.

```lua
IsPlayerSpell(133)                  -- Fireball — true if mage
IsPlayerSpell(2657)                 -- Smelt Copper — true if miner
IsPlayerSpell(20580)                -- Forsaken racial — true if undead
IsPlayerSpell(99999)                -- unknown ID — false
```

Reads the engine's player-spell-knowledge bitmap directly (same data
structure modern WoW's `IsPlayerSpell` uses). **Broader than the
engine's native `IsSpellKnown`** — that one walks the displayable
spellbook arrays, which famously don't include profession recipes
in 3.3.5 (per Wowhead: "as of 3.0.8, does not work for profession
spells"). `IsPlayerSpell` closes that gap.

---

## Talent

3.3.5's `GetTalentInfo(tab, idx)` returns `(name, icon, tier, column,
currentRank, maxRank, ...)` — useful for the talent UI, but doesn't
expose the talent's primary key or its spellID, both of which addons
routinely need (for stable identifiers in saved builds, or to chain
into the spell APIs). These two calls fill that gap.

### `GetTalentSpellID(tabIndex, talentIndex[, isInspect, isPet, groupIndex, rank])`

Returns the spellID for the given talent at the requested rank, or
`nil` if anything is out of range / the talent isn't allocated.

The first 5 args mirror the engine's `GetTalentInfo` positional order
exactly. `rank` (position 6) is the WrathClassicAPI extension.

```lua
GetTalentSpellID(1, 5)                          -- player, current rank
GetTalentSpellID(1, 5, false, false, nil, 3)    -- rank 3 specifically
GetTalentSpellID(1, 5, true)                    -- inspect target (after NotifyInspect)
GetTalentSpellID(1, 5, false, true)             -- pet
GetTalentSpellID(1, 5, false, false, 2)         -- player, secondary spec group
GetTalentSpellID(1, 5, false, false, nil, 99)   -- nil (rank > 9)
```

| Arg | Default | Effect |
|---|---|---|
| `isInspect` | `false` | Query the current inspect target's tabs instead of the player's. |
| `isPet`     | `false` | Query the player pet's tabs (mutually exclusive with `isInspect`). |
| `groupIndex`| (active group) | Which dual-spec group to read `currentRank` from. Ignored when `rank` is given explicitly. |
| `rank`      | `currentRank` | Explicit rank slot. If `currentRank` is 0 (talent unallocated), the implicit path falls back to rank 1 so a tooltip preview still has a real spellID. |

Returns `nil` when the explicit `rank` exceeds the talent's maxRank
(the SpellRank slot is zero past that point).

### `GetTalentIDByIndex(tabIndex, talentIndex[, isInspect, isPet, groupIndex])`

Returns the Talent.dbc primary key for the talent at (tab, idx) in
the source selected by `isInspect` / `isPet`, or `nil` for
out-of-range input.

```lua
GetTalentIDByIndex(1, 5)         -- e.g. 1612 (the Talent.dbc row ID, player)
GetTalentIDByIndex(1, 5, true)   -- inspect target's row ID at (1, 5)
GetTalentIDByIndex(1, 5, false, true)  -- pet
```

`groupIndex` is accepted for API symmetry with `GetTalentInfo`'s
shape but doesn't affect the result — talent IDs are class-determined
and identical across dual-spec groups.

Useful as a stable identifier for talent builds in `SavedVariables`
or for build-sharing protocols — survives talent-tree reshuffles
across patches, unlike `(class, tab, tier, column)` encoding.

---

## Time

3.3.5's stock `GetTime()` returns frame-relative seconds-since-login
— useless for anything that needs wall-clock alignment (cooldown
sync, log timestamps, daily-reset countdowns). The Time suite
backports the modern Unix-epoch shape plus the modern
`C_DateAndTime.*` date-math API.

### `GetServerTime()`

Returns the current server clock as a Unix epoch (seconds since
1970-01-01 UTC). Returns `nil` before login.

```lua
GetServerTime()                       -- e.g. 1716123456
date("%H:%M:%S", GetServerTime())     -- "14:37:36"
```

Reads year/month/day/hour/minute from the engine's game-time struct
(populated by `SMSG_LOGIN_VERIFY_WORLD` / `SMSG_LOGIN_SETTIMESPEED`)
and converts via `_mkgmtime`. The wire protocol carries minute
granularity only — we interpolate seconds via `GetTickCount` deltas
between minute boundaries. **Cold-start caveat**: the first reported
minute lands at :00 (off by up to 59s); subsequent calls are accurate
within a few hundred ms once we've observed a minute rollover.

### `C_DateAndTime.GetCurrentCalendarTime()`

Returns a fresh `CalendarTime` table for the current server time:

```lua
C_DateAndTime.GetCurrentCalendarTime()
-- { year = 2026, month = 5, monthDay = 23, weekday = 6, hour = 14, minute = 37 }
```

CalendarTime field conventions (matching Blizzard's modern
`TimeDocumentation.lua`):

| Field      | Range  | Notes                              |
|------------|--------|------------------------------------|
| `year`     | full   | e.g. 2026                          |
| `month`    | 1..12  | Lua-indexed (Jan = 1)              |
| `monthDay` | 1..31  | Lua-indexed                        |
| `weekday`  | 1..7   | Lua-indexed (Sunday = 1)           |
| `hour`     | 0..23  |                                    |
| `minute`   | 0..59  |                                    |

Returns `nil` before login.

### `C_DateAndTime.GetCalendarTimeFromEpoch(epoch)`

Inverse — decomposes a Unix epoch into a CalendarTime table.

```lua
C_DateAndTime.GetCalendarTimeFromEpoch(1716123456)
-- { year=2024, month=5, monthDay=19, weekday=1, hour=14, minute=17 }
```

### `C_DateAndTime.AdjustTimeByDays(t, days)` / `AdjustTimeByMinutes(t, minutes)`

Returns a new CalendarTime table that's `days` (or `minutes`)
offset from `t`. Negative deltas walk backwards. The math goes
through epoch conversion, so month/year rollover is handled
correctly.

```lua
local tomorrow = C_DateAndTime.AdjustTimeByDays(now, 1)
local fiveMinAgo = C_DateAndTime.AdjustTimeByMinutes(now, -5)
```

### `C_DateAndTime.CompareCalendarTime(lhs, rhs)`

Returns `-1` / `0` / `1` for `lhs < rhs` / `==` / `>`. Compares via
epoch so normalization is consistent — `{month=13, monthDay=1}`
compares correctly as "January next year".

### `C_DateAndTime.GetSecondsUntilDailyReset()`

Returns seconds until the next daily reset, using the engine's own
reset clock (the same value `GetQuestResetTime` returns). That clock
is populated by a server-broadcast calendar packet, so it respects
the actual reset schedule the server uses — 3am server-local on
retail Wrath, arbitrary on private servers.

```lua
local s = C_DateAndTime.GetSecondsUntilDailyReset()
print(string.format("Daily reset in %dh %dm", s/3600, (s%3600)/60))
```

Returns `nil` if the server hasn't broadcast a reset epoch yet
(pre-login or very early in the session).

`C_DateAndTime.GetSecondsUntilWeeklyReset` is **not** implemented —
3.3.5 has no analogous server-broadcast weekly clock. Compute your
own from the daily reset if you need it.

### `C_DateAndTime.GetServerTimeLocal()`

Returns the server's wall clock packed as a Unix epoch in the
client's local-timezone interpretation. Useful for
`date(format, GetServerTimeLocal())` to print server-clock strings
without timezone offsets sneaking in.

```lua
date("%H:%M:%S", GetServerTimeLocal())
-- prints server-side hour/minute regardless of client TZ
```

The trick: take the server's UTC-style components from
`GetServerTime()`, re-interpret them via `mktime` (which treats
input as local time). The resulting epoch, when fed to `date()` (a
local-time formatter), reproduces the server's wall clock.

---

## Timer

Modern callback scheduling. Internally a min-heap keyed by fire
time, drained from a hook on `FrameScript_FireOnUpdate` — same
tick cadence as Lua-side `OnUpdate` handlers, so a 1.0s timer
fires on the first frame at-or-after `GetTime() + 1.0`.

3.3.5 ships nothing equivalent natively (no `C_Timer`, no
`NewTicker`, no `NewTimer` strings anywhere in the binary), so the
whole namespace is new.

### `C_Timer.After(seconds, callback)`

Fires `callback` once after `seconds` have elapsed. Returns
nothing. Use this when you don't need to cancel.

```lua
C_Timer.After(2.5, function() print("2.5s later") end)
C_Timer.After(0,   function() print("next frame") end)
```

### `C_Timer.NewTimer(seconds, callback)`

One-shot like `After`, but returns a timer object you can cancel
before it fires:

```lua
local t = C_Timer.NewTimer(5, function() print("don't see me") end)
C_Timer.After(1, function() t:Cancel() end)
-- nothing prints
```

Returned table:

| Method | Returns | Notes |
|--------|---------|-------|
| `:Cancel()` | nothing | Marks the timer cancelled. Idempotent. |
| `:IsCancelled()` | boolean | `true` after `:Cancel()`, `false` otherwise. Stays `false` after the timer fires normally (cancellation is the explicit user action, not "did it complete"). |

### `C_Timer.NewTicker(seconds, callback[, iterations])`

Repeating timer. Fires every `seconds`, optionally limited to
`iterations` total fires. Omitted / non-positive `iterations`
means infinite — runs until `:Cancel()` is called.

```lua
local n = 0
local ticker = C_Timer.NewTicker(1, function()
    n = n + 1
    print("tick", n)
end, 5)
-- prints "tick 1" through "tick 5", one per second, then stops.

local heartbeat = C_Timer.NewTicker(60, function() Heartbeat() end)
-- runs every minute forever; call heartbeat:Cancel() to stop.
```

Same `:Cancel()` / `:IsCancelled()` methods as `NewTimer`.

Per Blizzard's design note on the original implementation:
> The one case where you're better off not using the new C_Timer
> system is when you have a ticker with a very short period —
> something that's going to fire every couple frames \[...\]
> you're going to be best served by using an OnUpdate function.

The heap-per-tick check is cheap (one comparison against the
top), so sub-frame tickers still work — but if you're scheduling
literally every frame, a direct `OnUpdate` script is fewer
indirections.

---

## Tooltip

These are registered as native frame methods on the `GameTooltip`
method table — same registration path the engine uses for `:SetSpellByID`,
`:GetSpell`, etc. They are real methods (`:call`), not globals.

### `GameTooltip:HasSpell()`

Returns `true` iff the tooltip is currently displaying a spell.
Equivalent to `(self:GetSpell()) ~= nil` but cheaper — reads the
engine's content-state slot directly.

```lua
GameTooltip:SetSpellByID(133)
GameTooltip:HasSpell()  -- true
GameTooltip:Hide()
GameTooltip:HasSpell()  -- false
```

### `GameTooltip:HasItem()`

Returns `true` iff the tooltip is currently displaying an item.
Same shape as `:HasSpell()`; reads the item-state slot.

### `GameTooltip:HasUnit()`

Returns `true` iff the tooltip is currently displaying a unit.
Reads the engine's unit-GUID slot on the tooltip frame and returns
true if either GUID half is non-zero.

---

## UI Color

### `C_UIColor.GetColors()`

Returns a Lua array of color rows from a Blizzard `GlobalColor.dbc`
snapshot:

```lua
{
  [1] = { baseTag = "NORMAL_FONT_COLOR", color = {r=1.0, g=0.82, b=0.0, a=1.0} },
  [2] = { baseTag = "WHITE_FONT_COLOR",  color = {r=1.0, g=1.0, b=1.0, a=1.0} },
  ...
}
```

~190 named colors. Modern Blizzard's `Blizzard_SharedXMLBase/Color.lua`
loops the same shape to build `_G[baseTag]` globals (e.g.
`_G.NORMAL_FONT_COLOR`). The companion addon
[`!!!WrathClassicAPI`](../AddOns/!!!WrathClassicAPI/Util/Color.lua)
does this loop for you so `NORMAL_FONT_COLOR` etc. are populated as
real `ColorMixin` instances.

The inner `color` field is a plain `{r, g, b, a}` table — *not* a
`ColorMixin`. Consumers re-wrap via `CreateColor(r, g, b, a)` in the
Lua-side loop.

---

## Unit

### `UnitClassID(unit)`

Returns the integer class ID (1=Warrior, 2=Paladin, 3=Hunter,
4=Rogue, 5=Priest, 6=Death Knight, 7=Shaman, 8=Mage, 9=Warlock,
11=Druid) for the unit, or `nil` if unresolvable.

```lua
UnitClassID("player")    -- e.g. 2 for paladin
UnitClassID("target")    -- depends on selected target
UnitClassID("partyN")    -- N=1..4
```

Why this exists: 3.3.5's `UnitClass(unit)` and `UnitClassBase(unit)`
both return `(localizedName, englishToken)` — neither returns the
class ID. Modern Blizzard's `UnitClass` adds it as a third return,
and `UnitClassBase` returns `(englishToken, classID)`. This call is
the additive backport so addons can dispatch on the integer ID
without a token→ID lookup table.

Accepts any standard unit token (`"player"`, `"target"`, `"partyN"`,
`"raidN"`, `"mouseover"`, etc.). For `"player"` specifically, reads
a login-session global rather than the unit descriptor, so it works
even at the first-login window before the player descriptor is
populated.

---

## Unit Auras

The `C_UnitAuras.*` namespace returns rich aura tables — modern's
`AuraData` shape with most fields populated for real (not just
defaulted). 3.3.5's wire protocol carries per-aura `duration`,
`expirationTime`, `stacks`, and `casterGUID` for every unit (not
just the local player like 1.12), so `sourceUnit`,
`isFromPlayerOrPlayerPet`, and the timing fields are real data
for target / party / raid / mouseover too.

Filter parsing mirrors modern: `"HELPFUL"` (the default if omitted)
vs `"HARMFUL"`. The other modern filter tokens
(`PLAYER` / `RAID` / `CANCELABLE` / `INCLUDE_NAME_PLATE_ONLY`) are
accepted but no-ops — they'd need source-side caster classification
we don't surface or modern-only systems (nameplate-only auras)
that don't exist in 3.3.5.

### `C_UnitAuras.GetAuraDataByIndex(unit, index[, filter])`

Returns the `index`-th aura on `unit` matching `filter` as an
[`AuraData`](#auradata-table-shape) table, or `nil` if no such aura.

```lua
-- 1st helpful aura on the player
local d = C_UnitAuras.GetAuraDataByIndex("player", 1)
print(d.name, d.spellId, d.duration, d.expirationTime - GetTime())

-- 1st harmful aura on the target
local d = C_UnitAuras.GetAuraDataByIndex("target", 1, "HARMFUL")
```

Walks the unit's aura array in the engine's stored order (NOT
alphabetical / priority-sorted), same way the engine's stock
`UnitAura(unit, n, "HELPFUL"|"HARMFUL")` does — so the (n)th aura
returned here matches the (n)th of the corresponding 3.3.5
`UnitBuff` / `UnitDebuff` call.

Returns `nil` for unresolvable unit tokens, indices `< 1`, or
indices past the populated-aura count.

### `C_UnitAuras.GetBuffDataByIndex(unit, index)` / `GetDebuffDataByIndex(unit, index)`

Filter-locked variants. Equivalent to
`GetAuraDataByIndex(unit, index, "HELPFUL")` and
`GetAuraDataByIndex(unit, index, "HARMFUL")` respectively. Saves
the third arg when you know which polarity you want.

### `C_UnitAuras.GetUnitAuraBySpellID(unit, spellID[, filter])`

Returns the first aura on `unit` with the given `spellID` as an
[`AuraData`](#auradata-table-shape) table, or `nil` if absent.

```lua
-- Is Renew up on the player?
local d = C_UnitAuras.GetUnitAuraBySpellID("player", 139)
if d then
    print("Renew remaining:", d.expirationTime - GetTime())
end
```

`filter` restricts the search to one polarity. Omit `filter`
(default behavior) to match either helpful or harmful — useful for
spells whose polarity isn't fixed (e.g. polymorph appears as
either depending on caster vs. target perspective).

Spell-ID lookup is exact, not by rank: `139` matches Renew rank 1
only, not the other ranks. Use the spell's max-rank ID (or a rank
table) for "any rank of Renew".

### `C_UnitAuras.GetPlayerAuraBySpellID(spellID)`

Equivalent to `GetUnitAuraBySpellID("player", spellID)`. Saves the
unit-token arg in the very common "is this buff up on me" case.

### `C_UnitAuras.GetUnitAuras(unit[, filter])`

Bulk fetch. Returns an array (1-indexed) of every populated
[`AuraData`](#auradata-table-shape) on `unit`. With `filter`,
restricts to one polarity (`"HELPFUL"` or `"HARMFUL"`); without,
returns helpful + harmful interleaved in the engine's storage
order.

```lua
for _, aura in ipairs(C_UnitAuras.GetUnitAuras("player")) do
    print(aura.name, aura.isHelpful and "BUFF" or "DEBUFF")
end
```

Always returns a table — never `nil`. The table is empty when the
unit doesn't exist or has no matching auras.

### `C_UnitAuras.GetAuraDispelTypeColor(type)`

Returns the dispel-type ColorMixin for an aura's `dispelName`
(`"Magic"`, `"Curse"`, `"Disease"`, `"Poison"`, `"Bleed"`,
`"Enrage"`), or the `NONE` color for unknown / nil types.

```lua
local d = C_UnitAuras.GetUnitAuraBySpellID("target", 27218)
if d and d.dispelName then
    local c = C_UnitAuras.GetAuraDispelTypeColor(d.dispelName)
    print(string.format("%.2f %.2f %.2f", c.r, c.g, c.b))
end
```

Lookup logic mirrors modern: returns `_G["DEBUFF_TYPE_<TYPE>_COLOR"]`
if some addon has already wrapped the entry as a ColorMixin global,
otherwise falls back to a plain `{r, g, b, a}` table decoded from
the embedded `GlobalColor.dbc` snapshot
([`UI::ColorData`](../src/ui/ColorData.h)). The `Enrage` row is a
ClassicAPI extension carried in the same data file — Blizzard
dropped it from `GlobalColor.dbc` in BC Classic, so we re-add it
so consumers don't get the `NONE` fallback for enrage debuffs.

### `AuraData` table shape

Fields populated with real data:

| Field | Type | Notes |
|-------|------|-------|
| `name` | string | Locale-resolved spell name from `Spell.dbc`. |
| `icon` | string | Full texture path (e.g. `Interface\Icons\Spell_Holy_Renew`). |
| `applications` | number | Stack count. `1` = single-stack aura (not `0`). |
| `spellId` | number | Spell ID. |
| `dispelName` | string\|nil | `"Magic"`, `"Curse"`, `"Disease"`, `"Poison"`, `"Bleed"`, `"Enrage"`, or `nil` for none. |
| `isHelpful` | boolean | True for buffs. |
| `isHarmful` | boolean | True for debuffs (`= not isHelpful`). |
| `duration` | number | Total duration in seconds, `0` for infinite. |
| `expirationTime` | number | Absolute `GetTime()` epoch when the aura ends, `0` for infinite. |
| `sourceUnit` | string\|nil | Unit token of the caster (`"player"`, `"target"`, `"partyN"`, `"pet"`, etc.), or `nil` if no caster GUID. |
| `isFromPlayerOrPlayerPet` | boolean | True iff `sourceUnit == "player"` or `"pet"`. |
| `timeMod` | number | Always `1` (3.3.5 doesn't expose per-aura time-mod). |

Vanilla-truthful defaults (modern provides these fields; 3.3.5
lacks the underlying systems):

| Field | Value |
|-------|-------|
| `charges`, `maxCharges` | `0` (3.3.5 has no spell-charge system) |
| `isStealable` | `false` |
| `isBossAura` | `false` |
| `isNameplateOnly` | `false` |
| `nameplateShowAll` | `false` |
| `nameplateShowPersonal` | `false` |
| `canApplyAura` | `false` |
| `shouldConsolidate` | `false` |
| `isRaid` | `false` |

Modern's `auraInstanceID` and `points` are omitted entirely
(missing-key reads yield `nil`, matching modern's behavior when
those fields don't apply).

---

## Globals

### `LE_EXPANSION_*`

Numeric constants for the modern Blizzard expansion enum, matching
`Enum.ExpansionLevel`:

| Constant | Value |
|----------|-------|
| `LE_EXPANSION_LEVEL_CURRENT` | 2 (fixed for this WotLK build) |
| `LE_EXPANSION_CLASSIC` | 0 |
| `LE_EXPANSION_BURNING_CRUSADE` | 1 |
| `LE_EXPANSION_WRATH_OF_THE_LICH_KING` | 2 |
| `LE_EXPANSION_CATACLYSM` | 3 |
| `LE_EXPANSION_MISTS_OF_PANDARIA` | 4 |
| `LE_EXPANSION_WARLORDS_OF_DRAENOR` | 5 |
| `LE_EXPANSION_LEGION` | 6 |
| `LE_EXPANSION_BATTLE_FOR_AZEROTH` | 7 |
| `LE_EXPANSION_SHADOWLANDS` | 8 |
| `LE_EXPANSION_DRAGONFLIGHT` | 9 |
| `LE_EXPANSION_WAR_WITHIN` | 10 |
| `LE_EXPANSION_MIDNIGHT` | 11 |

Pair with `GetClassicExpansionLevel` / `ClassicExpansionAt*` for
expansion-gated code paths.

---

## Behavioral extensions

WrathClassicAPI changes the behavior of two existing engine functions
without changing their signatures. Existing callers see the new
behavior automatically.

### `GetItemInfo` — auto cache warmup

3.3.5's stock `GetItemInfo` returns nil on cache misses and does NOT
trigger a network query — addons that want fresh item data had to
roll their own warmup. We hook `Script_GetItemInfo` so a cache miss
now kicks off `SMSG_ITEM_QUERY_SINGLE` transparently; the original
still returns nil this call, but subsequent calls return data and
`GET_ITEM_INFO_RECEIVED` fires when the response arrives. Same shape
as modern WoW (5.4+).

### `GameTooltip:SetSpellByID` — works for unknown spells

3.3.5's stock `SetSpellByID` gates tooltip building on a spellbook+
petbar walk and silently no-ops for any spell not in those
displayable structures (profession recipes, item-granted spells,
anything else the engine tracks only in the player-spell bitmap).
We hook the gate function to allow any non-zero spellID — matches
modern WoW (5.4+) where Blizzard removed the gate. The downstream
tooltip builder handles unknown spells gracefully: it produces a
static tooltip from `Spell.dbc` with no player-specific state
(cooldown remaining, charges) filled in.

```lua
-- Works for any valid spellID, even if the player hasn't learned it:
GameTooltip:SetOwner(UIParent, "ANCHOR_PRELOAD")
GameTooltip:SetSpellByID(2657)  -- Smelt Copper — populates even on a non-miner
GameTooltip:Show()
```

---

## Argument shapes

### `itemLocation`

Three accepted forms for any `C_Item.*` call without the `ByID`
suffix:

```lua
{ equipmentSlotIndex = N }       -- character-pane slot, 1..19
{ bagID = 0, slotIndex = N }     -- backpack slot, 1..16
{ bagID = 1..4, slotIndex = N }  -- equipped bag at INVSLOT_BAG1+bagID-1, slot 1..bag size
"0xHHHHHHHHLLLLLLLL"             -- engine GUID string (with or without "0x" prefix)
```

Non-supported in this build:

- Negative `bagID` (keyring, bank) — these correspond to slots
  outside the standard equipment+bag range and use different invMgr
  paths in the engine. Add when an addon actually needs it; deferred
  per [TODO §3](../TODO.md).
- Non-item GUIDs — `C_Item.*` calls type-check the GUID against
  `TYPEMASK_ITEM | TYPEMASK_CONTAINER`, so a unit/player GUID
  passed here returns `nil` (the wrong thing for the call type).
