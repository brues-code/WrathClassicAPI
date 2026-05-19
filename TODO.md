# WrathClassicAPI — TODO

Open items, with enough context to pick up next session. Items are
listed in the order I'd tackle them — the GUID-string variant is the
biggest known gap in the currently-shipped surface; the rest is
"nice to have."

## 1. `itemLocation` GUID-string variant

**The gap.** `C_Item.IsItemDataCached` and `C_Item.RequestLoadItemData`
currently accept the two table shapes —
`{equipmentSlotIndex=N}` and `{bagID=B, slotIndex=S}` — but **not** the
GUID-string form modern callers can pass:

```lua
C_Item.IsItemDataCached("0x4000000007EEB6FC")   -- doesn't work yet
```

This form is what `C_Item.GetItemGUID(itemLocation)` returns, and a
chunk of modern addons round-trip through it ("store a stable
reference to this item, query its cache state later"). We accept
table shapes only — string args fall through to the `IsLocationArg`
type check and the engine raises the typed error.

**The "complexity" was overstated.** Originally deferred as "needs
the engine's CGItemMgr GUID resolver re-derived." Then I noticed
awesome_wotlk's [GameClient.h][aw-gc] already exposes all three
pieces we'd need:

  * `ObjectMgr::HexString2Guid(str)` at `0x0074D120` — parses
    `"0xHHHHHHHHLLLLLLLL"` to `uint64_t` (cdecl, returns 64-bit).
  * `ObjectMgr::Get(guid, ObjectFlags)` at `0x004D4DB0` — looks up an
    object by GUID. We already call this from inside the engine's
    `PackBagSlot` chain (it's `FUN_004D4DB0`); with
    `flags = 4` (`ObjectFlags_Item`) it returns a CGItem* or nullptr.
  * No new CGItem offsets needed — `OFF_ITEM_INSTANCE_BLOCK = 0x08`
    and `OFF_INSTANCE_BLOCK_ITEM_ID = 0x0C` we already have apply
    once we have the CGItem.

[aw-gc]: https://github.com/FrostAtom/awesome_wotlk/blob/master/src/AwesomeWotlkLib/GameClient.h

**Sketch.** Add offsets, then teach `Item::Location::Resolve` to
accept strings:

```cpp
// new offsets
FUN_OBJECT_RESOLVE_BY_GUID = 0x004D4DB0,   // (guid_lo, guid_hi, flags) → Object*
FUN_HEXSTRING_TO_GUID      = 0x0074D120,   // (const char *) → uint64_t
OBJ_FLAGS_ITEM             = 4,

// in Item::Location::Resolve
if (Game::Lua::Type(L, locIdx) == Game::Lua::TYPE_STRING) {
    const char *s = Game::Lua::ToString(L, locIdx);
    if (s == nullptr) return nullptr;
    using HexToGuid_t = uint64_t (__cdecl *)(const char *);
    using ResolveByGuid_t = void *(__cdecl *)(uint32_t lo, uint32_t hi, int flags);
    const uint64_t guid = reinterpret_cast<HexToGuid_t>(
        Offsets::FUN_HEXSTRING_TO_GUID)(s);
    if (guid == 0) return nullptr;
    return static_cast<const uint8_t *>(reinterpret_cast<ResolveByGuid_t>(
        Offsets::FUN_OBJECT_RESOLVE_BY_GUID)(
            static_cast<uint32_t>(guid),
            static_cast<uint32_t>(guid >> 32),
            Offsets::OBJ_FLAGS_ITEM));
}
```

Then drop the "string GUID forms not supported in this build" comment
in `IsLocationArg` and accept `TYPE_STRING` too.

**Open questions to verify in Ghidra before shipping**, since I
haven't actually disassembled these:

  * `HexString2Guid`'s return convention — is it really 64-bit-in-EDX:EAX,
    or does it write to an out-parameter? Awesome_wotlk's declaration
    is `guid_t HexString2Guid(const char*)` (returns `uint64_t`), so
    the EDX:EAX return is the likely shape, but confirm by reading the
    function's `RET` and the asm at one of its call sites.
  * `ObjectMgr::Get`'s calling convention — awesome_wotlk declares it
    `__cdecl(guid_t, ObjectFlags)`. Verify at the `PackBagSlot` call
    site (`FUN_005D7380`) which already invokes it — that's how I'd
    pin the exact ABI before trusting it.

If both shapes match awesome_wotlk's declarations, this is ~30 lines.

## 2. `/reload` re-registration path

`Game::RunModuleRegistrations` fires only from the
**first-boot** GameUI hook (`FUN_0052A980`). 3.3.5 has a separate
`/reload` path through `FUN_00528F00 → FUN_00512280` that we
don't hook. After a reload, every `C_*` namespace we registered
becomes nil; the user has to log out and back in to get them back.

In practice this is minor — addon authors `/reload` constantly during
development, so the disappearing API is annoying — but we have the
piece in place to make it work: a second `MinHook` detour on
`FUN_00512280` that runs `Game::RunModuleRegistrations` post-call. The
caveat is that `FUN_00512280` clears all engine globals before
re-registering them (it's the reload pathway), so the timing of OUR
re-registration relative to the engine's clear+rebuild matters — see
[`memory/feedback_two_load_script_paths.md`][reload-mem] for the
specifics.

The first-attempted version of this (hooking `FUN_00512280`) crashed
during the engine's mid-rebuild state — that was actually our wrong
offset for `LUA_NEW_TABLE` masquerading as a `/reload` bug. With the
offsets right now, it should just work. Worth a careful re-attempt.

[reload-mem]: ~/.claude/projects/c--Git-WrathClassicAPI/memory/feedback_two_load_script_paths.md

## 3. Negative-bagID slots (keyring, bank)

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

## 4. The big one — more APIs from ClassicAPI

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

### 4a. Reference — `!!!ClassicAPI` (Frostmourne) C_\* surface

[`C:\WoW\FrostmourneClient\Interface\AddOns\!!!ClassicAPI`](C:/WoW/FrostmourneClient/Interface/AddOns/!!!ClassicAPI/Load.xml)
is a pure-Lua compat addon for Frostmourne (Wrath 3.3.5) that
backports / forwards the C_\* namespaces below. It's the cleanest
inventory of "what 3.3.5 addons actually expect from a modern API
surface," so it's the natural shortlist for section 4 — port what
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
    `IsItemDataCached`, `RequestLoadItemData` (table forms; see
    [TODO §1](TODO.md) for the GUID-string gap). Addon also
    exposes: `DoesItemExistByID`, `GetItemNameByID`,
    `GetItemInfoInstant`, `GetItemSubClassInfo`,
    `GetItemInventorySlotInfo`, `GetItemInventoryTypeByID`,
    `GetItemQualityByID`, `GetItemInfo`, `GetItemIconByID`,
    `GetItemCount`, and the `itemLocation` family
    (`GetItemName`, `IsLocked`, `GetItemID`, `GetItemIcon`,
    `GetItemLink`, `GetItemQuality`, `GetItemInventoryType`,
    `GetCurrentItemLevel`, `IsBound`, `DoesItemExist`). 🆕
    `IsBound` does a tooltip scan in Lua — the bound bit is a CGItem
    flag we can read directly. `GetItemGUID` and the
    `Lock/UnlockItemByGUID` family are noop stubs in the addon —
    natural pair to the [§1 GUID-string variant](TODO.md) work.
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
  * **C_Timer** — `After`, `NewTimer`, `NewTicker`. 3.3.5 already
    ships `C_Timer` natively; this file is a fallback for very
    old 3.3.5 builds (`if not C_Timer then` guard). No native work.
  * **C_UI** — `Reload = ReloadUI`. One-line forward.
  * **C_VoiceChat** — `GetTtsVoices = function() return {} end`.
    Stub only.

**Selection criterion for native ports.** The 🆕-flagged items have
something a pure-Lua wrapper can't do well: avoid tooltip scans,
read flags directly from CGItem / CGSpell, or replace a stateful
event-driven cache with a one-shot engine query. Everything else
is fine as Lua and porting it to native is largely cosmetic.

[!!!ClassicAPI on disk]: C:/WoW/FrostmourneClient/Interface/AddOns/!!!ClassicAPI/Util/
