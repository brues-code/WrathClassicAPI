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
- [Expansion](#expansion)
  - [`GetClassicExpansionLevel()`](#getclassicexpansionlevel)
  - [`ClassicExpansionAtLeast(level)`](#classicexpansionatleastlevel)
  - [`ClassicExpansionAtMost(level)`](#classicexpansionatmostlevel)
- [Item](#item)
  - [`C_Item.GetItemID(itemLocation)` / `GetItemGUID`](#c_itemgetitemiditemlocation)
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
- [Spell](#spell)
  - [`IsPlayerSpell(spellID)`](#isplayerspellspellid)
- [Tooltip](#tooltip)
  - [`GameTooltip:HasSpell()`](#gametooltiphasspell)
  - [`GameTooltip:HasItem()`](#gametooltiphasitem)
  - [`GameTooltip:HasUnit()`](#gametooltiphasunit)
- [UI Color](#ui-color)
  - [`C_UIColor.GetColors()`](#c_uicolorgetcolors)
- [Unit](#unit)
  - [`UnitClassID(unit)`](#unitclassidunit)
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
