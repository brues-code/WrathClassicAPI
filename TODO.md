# WrathClassicAPI — TODO

Open items, with enough context to pick up next session.

## 1. Negative-bagID slots (keyring, bank)

`Item::Location::ResolveBagSlot` handles `bagID 0` (backpack) and
`bagID 1..4` (equipped bags). The engine's `PackBagSlot` also supports
negative bag IDs for the keyring, bank, etc., with different invMgr
paths. Decompile of `FUN_005D7380` has a `switch ((int)bagID)` arm
that handles each — `case -1`, `case -2`, `case -4`. The math is
each case adds a different linear-slot offset and uses the player's
invMgr directly.

Translating those arms to C++ is a ~15-line addition. The reason
to defer it: the modern `itemLocation` table shape rarely uses
negative bagIDs — those slots are usually addressed by their
character-pane equipment-slot index (bank is its own bag UI). Add
when an addon actually asks for it.

## 2. The big one — more APIs from ClassicAPI

ClassicAPI exposes ~150 Lua functions across [a dozen
namespaces][classicapi-readme]. WrathClassicAPI exposes 5. Most of
ClassicAPI's surface ports to 3.3.5 mechanically (same module
pattern, just different offsets); ~10% are 1.12-specific because
3.3.5 already has them natively (e.g., `hooksecurefunc`,
`C_Timer.After`, `table.wipe`).

[classicapi-readme]: https://github.com/brues-code/ClassicAPI#whats-added

Don't port speculatively — port what's actually needed for the next
addon being backported. Each port is the same loop: find the
3.3.5 offset (Ghidra MCP, cross-check awesome_wotlk's table),
re-derive the calling convention from a call-site disassembly (see
`feedback_engine_calling_conventions` memory), drop a new
`src/<domain>/<File>.cpp` with the three-piece module pattern.

The high-value targets (subjective): `C_Spell.GetSpellInfo`,
`C_Item.GetItemInfo`, `C_AddOns.*`, `C_CVar.*`. Each is ~50 lines.

### 2a. Reference — `!!!ClassicAPI` (Frostmourne) C_\* surface

[`C:\WoW\FrostmourneClient\Interface\AddOns\!!!ClassicAPI`](C:/WoW/FrostmourneClient/Interface/AddOns/!!!ClassicAPI/Load.xml)
is a pure-Lua compat addon for Frostmourne (Wrath 3.3.5) that
backports / forwards the C_\* namespaces below. It's the cleanest
inventory of "what 3.3.5 addons actually expect from a modern API
surface," so it's the natural shortlist for section 2 — port what
WrathClassicAPI can do *better* than a pure-Lua wrapper (i.e.
anything needing engine state, GUIDs, or fields not exposed to
addon Lua), and let the addon keep handling the rest.

Each bullet is `Util/<file>.lua` in the addon. ✅ marks namespaces
WrathClassicAPI already implements (full or partial); 🆕 marks
namespaces with at least one entry-point worth doing natively (rest
of the surface is fine as Lua wrappers).

  * **C_AddOns** — `GetAddOnMetadata`, `IsAddOnLoaded`,
    `GetAddOnInfo`, `GetNumAddOns`, `IsAddOnLoadOnDemand`.
    Pure forwards to globals; no native-side value unless we want
    to backport `LoadAddOn` semantics.
  * **C_ChatInfo** — `CanPlayerSpeakLanguage`, `SendAddonMessage`
    (with channel-type guard), `GetColorForChatType`,
    `GetChatTypeName`, `GetChannelRosterInfo`,
    `GetNumActiveChannels`, prefix register/check stubs. Also
    monkey-patches `ChatThrottleLib.SendAddonMessage`.
  * **C_Container** — `GetContainerItemInfo` (returns table form
    with `iconFileID`/`stackCount`/`isBound`/…), `PlayerHasHearthstone`,
    `GetMaxArenaCurrency`, plus ~14 forwards to existing globals
    (`GetBagName`, `GetContainerItemID`, `PickupContainerItem`, …).
    🆕 `GetContainerItemInfo`'s table shape is a candidate for
    native — current Lua wrapper does a tooltip scan for `isBound`.
  * **C_CreatureInfo** — `GetClassInfo` (returns
    `{className, classFile, classID}`), `GetRaceInfo` (returns
    `{raceName, clientFileString, RaceID}`). Both hardcoded
    tables; race localization is enUS-only.
  * **C_DateAndTime** — `GetCurrentCalendarTime`,
    `AdjustTimeByMinutes`. Trivial `os.date` wrappers.
  * **C_EventUtils** ✅ — `IsEventValid`. Already shipped natively
    via [src/event/Util.cpp](src/event/Util.cpp).
  * **C_FriendList** — full surface: `GetNumOnlineFriends`,
    `GetNumFriends`, `GetFriendInfo` (by name),
    `GetFriendInfoByIndex`, `AddFriend`/`RemoveFriend`/
    `RemoveFriendByIndex`, ignore list (`AddIgnore`, `DelIgnore`,
    `DelIgnoreByIndex`, `GetIgnoreName`, `GetNumIgnores`,
    `IsIgnored`/`IsOnIgnoredList`, `AddOrDelIgnore`), who-results
    (`SendWho`, `GetWhoInfo`, `GetNumWhoResults`, `SortWho` stub,
    `SetWhoToUi` stub), `SetFriendNotes`/`SetFriendNotesByIndex`,
    selection getters/setters, `ShowFriends`, `AddOrRemoveFriend`.
    All forwards/wrappers — no obvious native upside.
  * **C_GossipInfo** — `GetText` (= `GetGossipText`),
    `GetNumActiveQuests`, `GetNumAvailableQuests`. Forwards only.
  * **C_Item** ✅ (partial) — already native:
    `IsItemDataCachedByID`, `RequestLoadItemDataByID`,
    `IsItemDataCached`, `RequestLoadItemData`, `GetItemGUID`,
    `GetItemLocation` (round-trips with `GetItemGUID`). Addon
    also exposes: `DoesItemExistByID`, `GetItemNameByID`,
    `GetItemInfoInstant`, `GetItemSubClassInfo`,
    `GetItemInventorySlotInfo`, `GetItemInventoryTypeByID`,
    `GetItemQualityByID`, `GetItemInfo`, `GetItemIconByID`,
    `GetItemCount`, and the `itemLocation` family
    (`GetItemName`, `IsLocked`, `GetItemID`, `GetItemIcon`,
    `GetItemLink`, `GetItemQuality`, `GetItemInventoryType`,
    `GetCurrentItemLevel`, `IsBound`, `DoesItemExist`). 🆕
    `IsBound` does a tooltip scan in Lua — the bound bit is a CGItem
    flag we can read directly. The `Lock/UnlockItemByGUID` family
    are noop stubs in the addon and would pair naturally with the
    rest of the GUID-string surface.
  * **C_Map** — `IsWorldMap`, `GetBestMapForUnit` (stub). 3.3.5
    has no UI-map concept; this namespace is mostly a polyfill.
  * **C_NamePlate** — file is empty (defers to
    [awesome_wotlk](https://github.com/FrostAtom/awesome_wotlk)).
    If a user runs without that addon, this is a gap — but the
    proper fix is to recommend awesome_wotlk, not duplicate it.
  * **C_NewItems** — `ClearAll`, `IsNewItem`, `RemoveNewItem`.
    Complex Lua state machine that watches `BAG_UPDATE`/`BAG_CLOSED`
    and `PickupContainerItem` to track "new" status. 🆕 candidate
    for native — there's likely an engine-side new-item flag we
    could read instead of this whole state machine.
  * **C_PvP** — `IsPvPMap`, `IsRatedBattleground` (false),
    `IsWarModeDesired` (false). Stubs / trivial.
  * **C_QuestLog** — `IsQuestFlaggedCompleted`,
    `GetQuestIDForLogIndex`, `GetLogIndexForQuestID`,
    `GetMaxNumQuests`, `GetMaxNumQuestsCanAccept`, `GetInfo`
    (table form), `RequestLoadQuestByID`, `GetTitleForQuestID`
    (with caching), `GetQuestInfo`, `QuestReadyForTurnIn`,
    `SetAbandonQuest`. Note: addon maintains a name cache via a
    `QUEST_LOG_UPDATE` watcher because there's no synchronous
    "get title by id" in 3.3.5. 🆕 the cache + `RequestLoadQuestByID`
    pair is the only piece a native impl would do meaningfully
    better.
  * **C_Spell** — `RequestLoadSpellData` (fires
    `SPELL_DATA_LOAD_RESULT` immediately), `IsSpellDataCached`,
    `GetSpellDescription` (tooltip scan),
    `GetSpellTexture`, `GetSpellInfo` (returns ID as 7th value —
    modern shape), `GetSpellSubtext`, `GetSchoolString` (returns
    `UNKNOWN`, TODO in addon), `DoesSpellExist`,
    `GetSpellIDForSpellIdentifier`, `SpellHasRange`,
    `GetSpellName`, plus forwards (`PickupSpell`, `GetSpellLink`,
    `IsSpellInRange`). 🆕 `GetSpellDescription` (tooltip-scan in
    Lua) and `GetSchoolString` (unimplemented) both want a native
    impl reading the spell DBC.
  * **C_SpellBook** — `HasPetSpells`, `GetSpellLinkFromSpellID`,
    `PickupSpellBookItem`, `GetSpellBookItemName`. Forwards only.
  * **C_Texture** — `GetAtlasInfo`. Depends on the addon's bundled
    `ATLAS_INFO_STORAGE` table; not native-portable.
  * **C_Timer** ✅ — already native: `After`, `NewTimer`,
    `NewTicker`. 3.3.5 doesn't ship `C_Timer` natively (none of
    those strings appear in the binary; TODO previously claimed it
    did, that was wrong). Implementation drives a min-heap from a
    `FrameScript_FireOnUpdate` hook.
  * **C_UI** — `Reload = ReloadUI`. One-line forward.
  * **C_VoiceChat** — `GetTtsVoices = function() return {} end`.
    Stub only.

**Selection criterion for native ports.** The 🆕-flagged items have
something a pure-Lua wrapper can't do well: avoid tooltip scans,
read flags directly from CGItem / CGSpell, or replace a stateful
event-driven cache with a one-shot engine query. Everything else
is fine as Lua and porting it to native is largely cosmetic.

[!!!ClassicAPI on disk]: C:/WoW/FrostmourneClient/Interface/AddOns/!!!ClassicAPI/Util/
