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

#pragma once

// All addresses derived against `C:\WoW\WoW 3.3.5\Wow.exe` via the Ghidra
// MCP server. Offsets happen to align with the ChromieCraft 3.3.5a
// extraction documented in `docs/BlizzardScriptAPI_3.3.5.md` because both
// derive from the same Blizzard build — but re-derived here as a check.
enum Offsets {
    // Engine's "valid Lua-C function pointer" range.
    // `FUN_0086B5A0` (the check itself) compares each closure's
    // function pointer against `[DAT_00D415B8, DAT_00D415BC)` — if it
    // falls outside, the engine errors with "Invalid function pointer: %p"
    // and crashes via Fatal Condition (ERROR #134). DLL-resident
    // closures are well outside `Wow.exe`'s `.text` range, so the
    // check must be neutered before any of our Script_* functions
    // are dispatched.
    //
    // Two ways to disable it. We use the data-write approach
    // (originally from awesome_wotlk) because it doesn't install a
    // hook — that lets us coexist with other DLLs (including
    // awesome_wotlk itself) that also disable the check, without
    // racing on hook installation at the same function entry.
    VAR_VALID_FUNCPTR_LO = 0x00D415B8,
    VAR_VALID_FUNCPTR_HI = 0x00D415BC,

    // FrameScript event-table populator. Cdecl `(const char **list,
    // int count)`. Resizes the engine's output Event* array
    // `DAT_00D3F7D8` to `count` and fills it from `list`, allocating
    // hash-table entries for any names not already registered. Calling
    // this AGAIN with a list that includes the existing entries plus
    // our customs is non-destructive: existing IDs are preserved
    // because the engine looks each name up in its hash table first
    // (`FUN_004BC410`) and reuses the existing Event* entry.
    //
    // Stores name pointers by reference — does NOT copy. Names must
    // outlive the engine (string literals are fine).
    //
    // Used (called, not hooked) by `Event::Custom::RegisterReservedEvents`
    // to append our custom event names to the engine's table after
    // the engine has populated it.
    //
    // NOT used as the bootstrap hook target: the engine calls
    // FillEvents from BOTH `CGlueMgr` (login-screen Lua state, ~41
    // events) and in-game `GameUIInit` (in-game Lua state, ~722
    // events). Registering on the glue call puts our `Script_*`
    // closures into a state that's destroyed at world entry. See
    // `FUN_UIBINDINGS_INIT` below for the in-game-only hook target.
    FUN_FRAMESCRIPT_FILL_EVENTS = 0x0081B5F0,

    // UIBindings::Initialize. Called once, unconditionally, from
    // in-game `GameUIInit` (FUN_0052A980) at offset 0x0052AB32 —
    // and from nowhere else. The full GameUIInit sequence is
    // `FUN_00819BB0` (Lua state init) → `FUN_005120E0` (register all
    // engine Lua C functions) → `FUN_0081B5F0` (FrameScript_FillEvents
    // for the in-game state) → `FUN_0051D9B0` (CVars_Initialize) →
    // THIS → ... → `FUN_00814340` (load FrameXML.toc which loads
    // addons).
    //
    // Hooked POST as the `ModuleAutoRegister` bootstrap target.
    // It's the cleanest single-call signal that the in-game init
    // is mid-flight: both module prerequisites are satisfied
    // (engine globals registered AND in-game event table populated)
    // and FrameXML.toc / addons haven't loaded yet — so our globals
    // are visible to addon main chunks. Also avoids the
    // glue-vs-in-game state ambiguity of hooking FillEvents (which
    // fires for both) and avoids competing with awesome_wotlk's
    // detours on hot Lua-init paths.
    //
    // Only fires on **first boot**. /reload uses a different path
    // (FUN_00528F00 → FUN_00512280) — see
    // [[feedback-two-load-script-paths]] in memory. Hooking that
    // path is a follow-up.
    FUN_UIBINDINGS_INIT = 0x005620F0,

    // The event-name → Event* output array maintained by
    // FrameScript_FillEvents. `vFireEvent` indexes by integer event
    // ID into this array. Reading [base + id*4] gives the Event*
    // entry; the name is at +0x14 inside the entry.
    VAR_EVENT_LIST_PTR = 0x00D3F7D8,
    VAR_EVENT_LIST_COUNT = 0x00D3F7D4,
    OFF_EVENT_ENTRY_NAME_IN_ENTRY = 0x14,

    // `void __cdecl vFireEvent(int eventID, const char *fmt, va_list args)`
    // The varargs version of FrameScript::FireEvent. Format string is
    // `%d`/`%u`/`%f`/`%s` tokens, one per payload value, no separators
    // or literals. Calling with `eventID < 0` is a no-op (the engine
    // bounds-checks against the event list count).
    FUN_VFIRE_EVENT = 0x0081AC90,

    // Item-data cache. The engine's `DBCache_ItemStats_C::GetRecord`
    // — `__thiscall(this, itemID, *guid, callback, userData, unused)`.
    // With `callback == nullptr`, returns the cached record or NULL
    // (hash-table lookup only). With a non-null callback, also kicks
    // off `SMSG_ITEM_QUERY_SINGLE` for uncached items — the engine
    // fills the cache asynchronously and invokes the supplied
    // callback with `userData` smuggled through.
    //
    // Identical signature to 1.12's same function (ClassicAPI uses
    // the same shape).
    FUN_DBCACHE_ITEMSTATS_GET_RECORD = 0x0067CA30,
    VAR_ITEMDB_CACHE = 0x00C5D828,

    // `Script_GetItemInfo` — Lua `GetItemInfo(itemID|"name"|"itemlink")`.
    // Hooked for transparent cache warmup: pre-resolve arg to itemID,
    // call DBCache::GetRecord with the implicit callback, then run
    // the original. The original still returns nil for cache misses
    // (vanilla behavior) but the query is now in flight, so subsequent
    // calls return valid data and GET_ITEM_INFO_RECEIVED fires when
    // the response arrives.
    FUN_SCRIPT_GET_ITEM_INFO = 0x00516C60,

    // Engine's item-arg string -> itemID resolver — the path GetItemInfo(name),
    // GetItemFamily, etc. take for a string arg. `__cdecl(const char *s) -> u32`:
    // if `s` contains "item:" it parses the link's itemID; otherwise it looks
    // `s` up as an item NAME in the item cache's name index (DAT_00CA1168) and
    // returns that record's itemID, or 0 when the name isn't cached. Does NOT
    // handle a bare numeric string or an item GUID (callers cover those).
    FUN_ITEM_STRING_TO_ID = 0x00709DE0,

    // Item-stats cache record field offsets. Verified inside
    // `Script_GetItemInfo` (FUN_00516C60): the record pointer
    // `puVar4` is the result of `FUN_0067CA30` (DBCache::GetRecord),
    // and the function reads:
    //   puVar4[1]   = +0x04   class
    //   puVar4[2]   = +0x08   subclass
    //   puVar4[4]   = +0x10   ItemDisplayInfo ID (icon)
    //   puVar4[5]   = +0x14   quality
    //   puVar4[9]   = +0x24   vendor sell price
    //   puVar4[10]  = +0x28   inventoryType (InvType enum, index
    //                         into VAR_INVTYPE_STRING_TABLE)
    //   puVar4[13]  = +0x34   itemLevel
    //   puVar4[14]  = +0x38   minLevel
    //   puVar4[23]  = +0x5C   stackCount
    //   puVar4[125] = +0x1F4  base name (single `const char *`,
    //                         verified at FUN_00706D70's
    //                         `puVar2[0x7d]` read).
    OFF_ITEMSTATS_CLASS = 0x04,
    OFF_ITEMSTATS_SUBCLASS = 0x08,
    OFF_ITEMSTATS_DISPLAY_INFO_ID = 0x10,
    OFF_ITEMSTATS_QUALITY = 0x14,
    // `ItemProto.Flags` — the static per-item-type flag word (NOT
    // per-instance). Verified in the tooltip builder at FUN_006277F0
    // around 0x00627EAB: `TEST [stats_record + 0x18], 0x8000000`
    // gates the "Binds to Account" tooltip line. The full word
    // mirrors TrinityCore's `ITEM_FLAG_*` enum — we only need the
    // ACCOUNT_BOUND bit so far.
    OFF_ITEMSTATS_FLAGS = 0x18,
    ITEM_PROTO_FLAG_ACCOUNT_BOUND = 0x08000000,
    // `GetItemOnUseSpell(itemID) -> spellID` — engine helper that
    // walks the item-stats cache record's 5 spell slots and returns
    // the first `spellID` whose trigger is `0` (on-use). Same
    // primary path the engine's stock `Script_GetItemSpell` takes
    // for items not currently held as instances. We use it to
    // implement `C_Item.GetItemSpell` (which returns the modern
    // `(name, spellID)` shape rather than 3.3.5's
    // `(name, rank)`) and to detect hearthstone-equivalents by
    // matching the returned spellID against `HEARTHSTONE_SPELL_ID`.
    //
    // Verified at FUN_00706B90: pulls the stats record from
    // `VAR_ITEMDB_CACHE`, scans `record[0x40..0x44]` (spellIDs)
    // against `record[0x45..0x49]` (trigger types), returns the
    // first non-zero spellID whose trigger is 0. Returns 0 for
    // uncached items, non-existent items, or items with no
    // on-use spell.
    FUN_ITEM_GET_ONUSE_SPELL = 0x00706B90,

    OFF_ITEMSTATS_INVENTORY_TYPE = 0x28,
    OFF_ITEMSTATS_ITEM_LEVEL = 0x34,
    OFF_ITEMSTATS_STACK_COUNT = 0x5C,
    OFF_ITEMSTATS_NAME = 0x1F4,

    // Client-side Item.dbc store (distinct from the server-populated item-stats
    // cache above). A WowClientDB<CItemRec> static object loaded from
    // `DBFilesClient\Item.dbc` by FUN_00644190; every item shipped with the 3.3.5
    // client is resident here with NO server query, so it's the truly-instant
    // source for GetItemInfoInstant. Store fields (verified in the row-index
    // builder FUN_0065C760): maxID @+0x0C, minID @+0x10, and the id->record index
    // array @+0x20 (indexed by `id - minID`, entries null for gaps). GetRow(id) =
    // `id in [minID, maxID] ? index[id - minID] : null` — the inline WowClientDB
    // pattern. Server-custom items beyond the client's Item.dbc are absent here
    // (fall back to the item-stats cache for those).
    VAR_ITEMDBC_STORE = 0x00AD3D4C,
    OFF_ITEMDBC_STORE_MAX_ID = 0x0C,
    OFF_ITEMDBC_STORE_MIN_ID = 0x10,
    OFF_ITEMDBC_STORE_INDEX = 0x20,
    // Item.dbc record columns (8 uint32, 0x20-byte row): ID @+0x00, ClassID
    // @+0x04, SubclassID @+0x08, DisplayInfoID @+0x14, InventoryType @+0x18.
    OFF_ITEMDBC_CLASS = 0x04,
    OFF_ITEMDBC_SUBCLASS = 0x08,
    OFF_ITEMDBC_DISPLAY_INFO_ID = 0x14,
    OFF_ITEMDBC_INVENTORY_TYPE = 0x18,

    // `CGItem::IsSoulbound` — `__thiscall(CGItem) -> bool`. Returns
    // true iff the per-instance soulbound flag is set OR any
    // attached enchantment carries the "bind-on-apply" flag.
    // Verified at the tooltip-builder's bind-label gate
    // (FUN_006277F0, 0x00627E80): the engine calls this then
    // skips the bind label entirely on a false return.
    //
    // Important: this returns false for heirlooms — they're not
    // *soulbound*, they're *account-bound*. Modern `IsBound`
    // returns true for both, so we OR this with a separate
    // `ITEM_FLAG_ACCOUNT_BOUND` proto-flag probe.
    FUN_ITEM_IS_SOULBOUND = 0x00708520,

    // ItemClass.dbc — sparse `[min, max]`-bounded array. The engine
    // stores records via a min-offset translation:
    //   if (id < min || id > max) bail
    //   else record = records[id - min]
    //   name string at record + 0x0C
    // Verified inside `Script_GetItemInfo` at the
    // `pbVar5 = *(byte **)(iVar1 + 0xc)` slot. The name slot is a
    // single `const char *` (engine pre-resolves the locale at DBC
    // load time on this build).
    VAR_ITEMCLASS_MAX = 0x00AD3DA0,
    VAR_ITEMCLASS_MIN = 0x00AD3DA4,
    VAR_ITEMCLASS_RECORDS_BASE = 0x00AD3DB4,
    OFF_ITEMCLASS_NAME = 0x0C,

    // ItemSubClass.dbc — flat records array, scanned linearly because
    // the table is keyed on `(classID, subClassID)` (no direct index).
    // From `Script_GetItemInfo`'s second loop body:
    //   stride = 0x34 (13 dwords; loop increments `+= 0xd`)
    //   record[+0x00] = classID
    //   record[+0x04] = subClassID
    //   record[+0x28] = verboseName (preferred if non-empty)
    //   record[+0x2C] = shortName (fallback when verbose is empty)
    // Both name fields are single `const char *` like ItemClass —
    // engine pre-resolves locale.
    VAR_ITEMSUBCLASS_COUNT = 0x00AD3F4C,
    VAR_ITEMSUBCLASS_RECORDS = 0x00AD3F60,
    OFF_ITEMSUBCLASS_STRIDE = 0x34,
    OFF_ITEMSUBCLASS_CLASS_ID = 0x00,
    OFF_ITEMSUBCLASS_SUBCLASS_ID = 0x04,
    OFF_ITEMSUBCLASS_NAME = 0x28,
    OFF_ITEMSUBCLASS_VERBOSE_NAME = 0x2C,

    // Inventory-type string table — array of `const char *` indexed
    // directly by the `OFF_ITEMSTATS_INVENTORY_TYPE` enum value.
    // Entries are strings like "INVTYPE_HEAD", "INVTYPE_WEAPONMAINHAND",
    // "INVTYPE_2HWEAPON". Verified at `Script_GetItemInfo`'s
    // `(&PTR_DAT_00ac7fd8)[puVar4[10]]` — bare table indexing, no
    // bounds check by the engine. Max valid index is 28 (INVTYPE_RANGEDRIGHT);
    // we guard against junk values defensively.
    VAR_INVTYPE_STRING_TABLE = 0x00AC7FD8,
    INVTYPE_TABLE_MAX_INDEX = 28,

    // `FUN_0070A910(displayInfoID) -> const char *` — engine helper
    // that resolves an `ItemDisplayInfo.dbc` row to its icon basename
    // (e.g. "INV_Sword_04"). Returns "INV_Misc_QuestionMark" as the
    // fallback for missing rows / empty icon slots. The caller is
    // responsible for prepending the standard "Interface\\Icons\\"
    // path prefix (the engine's `Script_GetItemInfo` does this via
    // a snprintf at the call site).
    FUN_ICON_BASENAME_BY_DISPLAY_ID = 0x0070A910,

    // `FUN_0061E290(itemID) -> const char *` — engine helper that
    // builds a full colored item link
    //   "|cff…|Hitem:ID:0:0:0:0:0:0:0:level|h[Name]|h|r"
    // and returns a pointer into a static 1 KiB global buffer at
    // 0x00C5CF50. Buffer is overwritten on every call; we strdup-
    // equivalent by letting `lua_pushstring` make its own copy
    // before the next call.
    //
    // Internally reads the local player's level from the descriptor
    // (player+0xD0 → +0xC0) so the link's level field matches
    // whatever stock GetItemInfo would have produced. No work needed
    // on our side beyond pushing the result.
    FUN_ITEM_LINK_FORMATTER = 0x0061E290,

    // `Game::ResolveUnitToken("player"|"target"|"partyN"|...)` →
    // CGUnit_C *. Plain `__cdecl(const char *token)` — verified at
    // `Script_GetInventoryItemID` (FUN_005EA3E0) which pushes the
    // token on the stack: `PUSH EDX; CALL 0x0060c1f0`. Returns the
    // canonical unit pointer the inventory routines expect
    // (different from the global at 0x00B41414, which is something
    // else despite holding the local-player GUID).
    FUN_RESOLVE_UNIT_TOKEN = 0x0060C1F0,

    // CGObject world-position virtual, shared by every unit (and the
    // player). `float * __thiscall GetPosition(float out[3])` — writes the
    // world position into `out` and returns a float* to it (or to an internal
    // cached field; the caller copies from the returned pointer). vtable slot
    // at byte offset 0x2C. Derived from Script_CheckInteractDistance
    // (FUN_0051B240): it resolves player + unit, calls
    // `obj->vtable[0x2C](&buf)` on each, and sums the three squared component
    // deltas. (The 1.12 sibling uses slot 0x14; the WotLK vtable is wider.)
    OFF_CGOBJECT_VTBL_GET_POSITION = 0x2C,

    // Currently-loaded Map.dbc id (continent / instance) — what modern
    // `UnitPosition` reports as its `mapID`/`instanceID` return. Every unit the
    // client can see shares the player's instance, so this one global serves any
    // resolvable unit. Derived from `Script_GetInstanceInfo` (FUN_0051A8C0):
    // `DAT_00bd088c` is the id it bounds-checks and indexes Map.dbc with
    // (`*(Map.dbc.indexTable + (DAT_00bd088c - min)*4)`).
    VAR_CURRENT_MAP_ID = 0x00BD088C,

    // --- Reputation / factions ---------------------------------------------
    // Derived from Script_GetFactionInfo (FUN_005D1150 → worker FUN_005D0DA0),
    // Script_SetWatchedFactionIndex (FUN_005D1420), and Script_GetWatchedFactionInfo
    // (FUN_005D1240). 3.3.5 exposes clean __cdecl(factionID) helpers for the
    // per-faction reads, so the module calls those rather than re-deriving the
    // rep-slot flag logic.
    //
    // Displayed-list resolver (index → factionID), inline in the engine:
    //   entry = (*(void***)VAR_FACTION_DISPLAY_LIST)[idx0];
    //   factionID = *(int*)(entry + OFF_FACTION_DISPLAY_ENTRY_ID);
    // idx0 is the 0-based display index, valid when 0 <= idx0 <= *VAR_FACTION_VISIBLE_MAX_INDEX.
    VAR_FACTION_DISPLAY_LIST = 0x00C23488,      // ptr to array of display-entry ptrs
    VAR_FACTION_DISPLAY_COUNT = 0x00C23478,     // live count of display entries
    VAR_FACTION_VISIBLE_MAX_INDEX = 0x00C23474, // max valid 0-based display index
    OFF_FACTION_DISPLAY_ENTRY_ID = 0x04,        // factionID field within a display entry

    // Faction.dbc client store: record = (*(void***)INDEX_TABLE)[factionID - *MIN_INDEX],
    // valid when *MIN_INDEX <= factionID <= *MAX_INDEX. Names/descriptions are
    // single (already-localized) char* pointers, not locale arrays.
    VAR_FACTION_DBC_MIN_INDEX = 0x00AD3870,
    VAR_FACTION_DBC_MAX_INDEX = 0x00AD386C,
    VAR_FACTION_DBC_INDEX_TABLE = 0x00AD3880,   // ptr to array of record ptrs
    OFF_FACTION_REP_LIST_INDEX = 0x04,          // record: rep-slot index (int)
    OFF_FACTION_NAME = 0x5C,                    // record: name (char*)
    OFF_FACTION_DESCRIPTION = 0x60,             // record: description (char*)

    // Displayed-list category headers. HEADER_LIST is the array itself (not a
    // ptr); COLLAPSED_BITMASK bit i SET = header i expanded, CLEAR = collapsed.
    VAR_FACTION_HEADER_LIST = 0x00C23370,       // array of header factionIDs
    VAR_FACTION_HEADER_COUNT = 0x00C23470,
    VAR_FACTION_COLLAPSED_BITMASK = 0x00AD0AE4,
    MAX_FACTION_HEADERS = 64,

    // Player reputation slots, indexed by the record's rep-list index (0..127).
    VAR_PLAYER_REP_SLOTS = 0x00C22B70,
    REP_SLOT_STRIDE = 0x10,
    OFF_REP_SLOT_FACTION_ID = 0x00,
    OFF_REP_SLOT_FLAGS = 0x04,
    OFF_REP_SLOT_BASE_STANDING = 0x08,
    OFF_REP_SLOT_DELTA_STANDING = 0x0C,
    MAX_REP_SLOTS = 128,

    // Reaction thresholds indexed by band 0..7 (returned by GET_REACTION_BAND).
    VAR_REACTION_MIN_TABLE = 0x00A2D2FC,
    VAR_REACTION_MAX_TABLE = 0x00A2D300,

    // CGPlayer reputation info sub-struct. `*(void**)(player + OFF_CGPLAYER_INFO)`
    // is the info struct; the watched rep-list index lives at +WATCHED_REP_LIST_ID.
    OFF_CGPLAYER_INFO = 0x1008,
    OFF_CGPLAYER_INFO_WATCHED_REP_LIST_ID = 0x10E8,

    // Engine helpers — all __cdecl(int factionID) except HEADER_HAS_REP, which
    // takes the Faction.dbc record pointer. Bool results are in the low byte.
    FUN_REPUTATION_GET_REACTION_BAND = 0x005D0600, // -> band 0..7
    FUN_REPUTATION_GET_STANDING = 0x005D05B0,       // -> total standing (base + delta)
    FUN_REPUTATION_GET_AT_WAR = 0x005D04B0,         // -> bool (rep-slot flag bit 1)
    FUN_REPUTATION_GET_PEACE_FORCED = 0x005D0500,   // -> bool (rep-slot flag bit 4)
    FUN_REPUTATION_IS_WATCHED = 0x005D0C70,         // -> bool
    FUN_REPUTATION_IS_CHILD = 0x005D06E0,           // -> bool
    FUN_REPUTATION_HEADER_HAS_REP = 0x005D0CE0,     // (record ptr) -> bool
    FUN_PLAYER_SET_WATCHED_FACTION = 0x005D0BA0,    // set watched by id; 0 clears

    // Per-rep-change notify — the "+X reputation" dispatcher, called from the
    // SMSG_SET_FACTION_STANDING handler (FUN_005D20A0) only when a faction's
    // standing actually changed and is visible; NOT from the bulk login rebuild.
    // The hook chokepoint for FACTION_STANDING_CHANGED.
    // void __cdecl(int factionID, int delta, int isGeneric, float rate)
    FUN_REPUTATION_FIRE_NOTIFY = 0x0050B8C0,

    // `ObjectMgr::HexString2Guid(const char *s) -> uint64_t` —
    // parses `"0xHHHHHHHHLLLLLLLL"` (or bare 16-hex-char form) to a
    // 64-bit GUID. Returns via EDX:EAX (standard cdecl 64-bit
    // return). Returns 0 for empty / malformed input — the loop
    // breaks on the first non-hex char.
    //
    // Decompile verified: skips the optional `0x` prefix, loops up
    // to 16 hex chars accumulating into a 64-bit value, returns
    // CONCAT44(high, low). Same shape as awesome_wotlk's
    // `guid_t HexString2Guid(const char *)` declaration.
    FUN_HEXSTRING_TO_GUID = 0x0074D120,

    // `ObjectMgr::Get(uint32_t guid_lo, uint32_t guid_hi, int flags) -> Object *` —
    // looks up an object by GUID in the local ObjectMgr (read from
    // the thread-local storage slot, so always usable from the
    // main thread). The `flags` arg is a TYPEMASK bitfield; the
    // returned object is returned only if `obj.entry.type & flags`
    // is non-zero, else nullptr.
    //
    // Used internally by the engine for unit-token resolution
    // (`Script_UnitClass` passes flags = 8 = TYPEMASK_UNIT). The
    // TYPEMASK encoding is the standard `1 << TYPEID` from
    // 3.3.5-era trinitycore — see comment on `OBJ_FLAGS_ITEM`.
    FUN_OBJECT_RESOLVE_BY_GUID = 0x004D4DB0,

    // TYPEMASK bits for `FUN_OBJECT_RESOLVE_BY_GUID`'s flags arg.
    // Standard 3.3.5 encoding (`1 << TYPEID`):
    //   TYPEMASK_OBJECT        = 0x01
    //   TYPEMASK_ITEM          = 0x02
    //   TYPEMASK_CONTAINER     = 0x04
    //   TYPEMASK_UNIT          = 0x08
    //   TYPEMASK_PLAYER        = 0x10
    //   TYPEMASK_GAMEOBJECT    = 0x20
    //   ...
    // `OBJ_FLAGS_ITEM` is `TYPEMASK_ITEM | TYPEMASK_CONTAINER` so
    // both ordinary items and bags resolve through the same path —
    // bags are TYPEID_CONTAINER but every `C_Item.*` accessor that
    // takes an `itemLocation` should still work on them.
    OBJ_FLAGS_ITEM = 0x02 | 0x04,
    // TYPEMASK_UNIT — resolves a GUID to its in-world CGUnit (player or creature)
    // only, or null. Used by UnitNameFromGUID.
    OBJ_FLAGS_UNIT = 0x08,

    // UnitNameFromGUID (src/unit/NameFromGUID.cpp). Two name sources, mirroring
    // the engine's own Script_UnitName (FUN_0060E740):
    //   * in-world CGUnit -> FUN_004FD0E0(unit, &realmOut, 1) -> name (any player
    //     or creature you can see). `__thiscall`; realmOut receives the realm
    //     string pointer (null for creatures).
    //   * player name cache -> FUN_0067D770, a `__thiscall` DBCache::GetRecord on
    //     the cache object at VAR_PLAYER_NAME_CACHE. Exact call from
    //     Script_GetPlayerInfoByGUID: `ECX = cache; (guidLo, guidHi, &scratch[2],
    //     0, 0, 0)`, non-null return = the record; the trailing 0s keep it a pure
    //     lookup (no name query / side effects). Same store/record that backs
    //     GetPlayerInfoByGUID, so it covers cached players (group, chat, combat)
    //     whether or not they're in view. Record holds the name inline at +0x00
    //     and the realm inline at +0x34.
    FUN_UNIT_NAME_FROM_OBJECT = 0x004FD0E0,
    VAR_PLAYER_NAME_CACHE = 0x00C5D938,
    FUN_PLAYER_NAME_CACHE_GET = 0x0067D770,
    OFF_PLAYER_NAME_REC_NAME = 0x00,
    OFF_PLAYER_NAME_REC_REALM = 0x34,

    // CGUnit descriptor (updatefields buffer) pointer offset.
    // Verified inside the engine's own `Script_UnitClassBase`
    // (FUN_00610040): reads class byte as
    // `*(byte *)(*(int *)(unit + 0xD0) + 0x45)` — two derefs.
    OFF_UNIT_DESCRIPTOR = 0xD0,

    // Class byte position inside the descriptor block. This is the
    // class field of `UNIT_FIELD_BYTES_0` (byte 1 of that uint32):
    // descriptor index 0x11 (17), so 17 * 4 + 1 = 0x45. Layout:
    //   byte 0 = race, byte 1 = class, byte 2 = gender, byte 3 = power.
    OFF_UNIT_DESCRIPTOR_CLASS_BYTE = 0x45,

    // Race byte — byte 0 of the SAME UNIT_FIELD_BYTES_0 dword
    // (descriptor index 0x11, 17 * 4 + 0 = 0x44). Verified in
    // `Script_UnitRace` (FUN_0060FD40, "Usage: UnitRace(\"unit\")"):
    // reads `*(byte *)(*(int *)(unit + 0xD0) + 0x44)` for non-player
    // units — the exact sibling of the class read at +0x45.
    OFF_UNIT_DESCRIPTOR_RACE_BYTE = 0x44,

    // UNIT_FIELD_HEALTH / UNIT_FIELD_MAXHEALTH within the unit descriptor
    // (`unit + OFF_UNIT_DESCRIPTOR`). Both uint32, stored at face value (no
    // display divisor). Verified in Script_UnitHealthMax (FUN_0060EC60 reads
    // maxhealth at `*(*(unit+0xD0) + 0x68)`) and the health getter FUN_0071C2C0
    // that Script_UnitHealth calls (`*(*(unit+0xD0) + 0x48)` in the normal case).
    OFF_UNIT_FIELD_HEALTH = 0x48,
    OFF_UNIT_FIELD_MAXHEALTH = 0x68,

    // UNIT_FIELD_POWER1..7 / MAXPOWER1..7 in the same descriptor: POWER[t] at
    // +0x4C + t*4, MAXPOWER[t] at +0x6C + t*4 (t = 0..6 = mana, rage, focus,
    // energy, happiness, runes, runic power). The unit's ACTIVE power type is
    // byte 3 of UNIT_FIELD_BYTES_0 (+0x47). Verified in Script_UnitPower /
    // Script_UnitPowerMax (FUN_0060ED40 / FUN_0060EF40) and the power getter
    // FUN_0071C2E0 (`*(*(unit+0xD0) + 0x4C + t*4)`).
    OFF_UNIT_DESCRIPTOR_POWER_TYPE = 0x47,
    OFF_UNIT_FIELD_POWER_BASE = 0x4C,    // POWER[t]    = base + t*4
    OFF_UNIT_FIELD_MAXPOWER_BASE = 0x6C, // MAXPOWER[t] = base + t*4

    // Power display-divisor table (uint32[], indexed by power type). Rage and
    // runic power are stored x10, so UnitPower divides the raw field by this
    // before returning. `FUN_007FDE00(t)` is just `t < 0 ? 1 : table[t]`.
    VAR_POWER_DISPLAY_DIVISOR_TABLE = 0x00AF5220,

    // Group-roster fallback for a party/raid member outside the client's
    // object-sync range (no live object to read a descriptor from). The
    // token -> GUID -> party -> raid resolution is wrapped by Unit::ResolveMember
    // (unit/Resolve.h) — the same fallthrough Script_UnitHealth / UnitHealthMax
    // take when their object lookup misses. All __cdecl internal primitives, no
    // Lua surface:
    //   token -> GUID:  FUN_0060ABF0(const char *token, uint32 out[2], char flag)
    //                   (flag 0; writes {lo, hi}, returns bool success)
    //   party by GUID:  FUN_00513C30(const uint32 guid[2]) -> record* | null
    //   raid  by GUID:  FUN_00513CB0(const uint32 guid[2]) -> record* | null
    // Record fields are per-stat; the health fields (uint32) are party health
    // @+0x0C, max @+0x10; raid health @+0x5C, max @+0x60. The power fields (both
    // u16, and the roster caches only the member's ACTIVE power type) are party
    // type @+0x0A / current @+0x14 / max @+0x16; raid type @+0x58 / current
    // @+0x64 / max @+0x66. All from Script_UnitPower / Script_UnitPowerMax.
    FUN_TOKEN_TO_GUID = 0x0060ABF0,
    FUN_PARTY_ROSTER_BY_GUID = 0x00513C30,
    FUN_RAID_ROSTER_BY_GUID = 0x00513CB0,
    OFF_PARTY_MEMBER_HEALTH = 0x0C,
    OFF_PARTY_MEMBER_MAXHEALTH = 0x10,
    OFF_RAID_MEMBER_HEALTH = 0x5C,
    OFF_RAID_MEMBER_MAXHEALTH = 0x60,
    OFF_PARTY_MEMBER_POWER_TYPE = 0x0A,
    OFF_PARTY_MEMBER_POWER = 0x14,
    OFF_PARTY_MEMBER_MAXPOWER = 0x16,
    OFF_RAID_MEMBER_POWER_TYPE = 0x58,
    OFF_RAID_MEMBER_POWER = 0x64,
    OFF_RAID_MEMBER_MAXPOWER = 0x66,

    // Local player class byte global. Populated by the engine during
    // login session setup — well before the unit descriptor at
    // `unit + OFF_UNIT_DESCRIPTOR` is ready. Both `Script_UnitClass`
    // (FUN_0060FEC0) and `Script_UnitClassBase` (FUN_00610040) take
    // a fast path for the `"player"` token that reads this byte
    // directly via `FUN_006B1080` (one-line accessor: returns
    // `*(byte *)VAR_LOCAL_PLAYER_CLASS_BYTE`). Use this path for
    // `"player"` to avoid the at-login race where the descriptor
    // hasn't been populated yet.
    VAR_LOCAL_PLAYER_CLASS_BYTE = 0x00C79E89,

    // Local player race byte global — byte 0 of the login-session
    // UNIT_FIELD_BYTES_0 (race @ 0x00C79E88, class @ +1 = 0xC79E89,
    // gender @ +2 = 0xC79E8A). `Script_UnitRace`'s "player" fast path
    // reads it via `FUN_006B1070` (returns `*(byte *)0x00C79E88`), the
    // race sibling of the class accessor `FUN_006B1080`. Same at-login-
    // race rationale as the class byte — valid before the unit
    // descriptor is populated.
    VAR_LOCAL_PLAYER_RACE_BYTE = 0x00C79E88,

    // ChrRaces.dbc client store — `[min, max]`-bounded index table, same
    // struct layout as the Faction.dbc store above (max, min = max+4,
    // index-table = min+0x10). record = (*(void***)INDEX_TABLE)[raceID - *MIN],
    // valid when *MIN <= raceID <= *MAX. Verified in `Script_UnitRace`
    // (FUN_0060FD40): `iVar7 = *(int*)(DAT_00ad3448 + (raceID - DAT_00ad3438)*4)`.
    VAR_CHRRACES_DBC_MAX_INDEX = 0x00AD3434,
    VAR_CHRRACES_DBC_MIN_INDEX = 0x00AD3438,
    VAR_CHRRACES_DBC_INDEX_TABLE = 0x00AD3448, // ptr to array of record ptrs
    // Filename (clientFileString) column — a single locale-independent char*
    // ("Human", "NightElf", "Scourge", …), distinct from the localized Name.
    // `Script_UnitRace` returns it as its second value: `*(char**)(record+0x2C)`.
    OFF_CHRRACES_CLIENT_FILE_STRING = 0x2C,

    // Per-player inventory manager. Offset INTO the CGPlayer
    // returned by ResolveUnitToken("player"), pointing to the
    // CInventoryMgr the engine uses for slot lookups. Found by
    // matching the `(void*)((int)player + 0x18F0)` arg the engine
    // passes to `GetItemBySlot` in `Script_GetInventoryItemID`.
    OFF_PLAYER_INVENTORY_MANAGER = 0x18F0,

    // `CInventoryMgr::GetItemBySlot(this, int slot0Based)` →
    // CGItem*. `__thiscall(ECX = invMgr, slot)`. Takes a *linearized
    // 0-based* slot. Engine paths all decrement Lua's 1-based slot
    // arg before calling.
    FUN_ITEMMGR_GET_ITEM_BY_SLOT = 0x00754390,

    // CGContainer (bag) layout. First dword is the slot count —
    // verified at `Script_GetContainerNumSlots` (FUN_005D74A0): after
    // `(**(code **)(*piVar4 + 0x28))()` (= CGItem::GetContainer via
    // vtable slot 10), the function reads `*piVar4` (the first dword
    // of the returned container) and pushes it to Lua. Same layout as
    // 1.12 — this slot count is the value `GetContainerNumSlots`
    // returns.
    OFF_CONTAINER_NUM_SLOTS = 0x00,

    // CGItem instance-block layout. CGItem at +0x08 holds a pointer
    // to an "instance block" (the per-item data — `ObjectEntry` in
    // awesome_wotlk's terminology). Layout:
    //   +0x00  guid (uint64_t)
    //   +0x08  type (uint32_t)
    //   +0x0C  entry (uint32_t) — for items, this is the itemID
    //   +0x10  scaleX (float)
    //   +0x14  padding
    // Same offsets in 1.12 (CGItem layout predates the 1.12-vs-3.3.5
    // cut, so this structure didn't shift).
    OFF_ITEM_INSTANCE_BLOCK = 0x08,
    OFF_INSTANCE_BLOCK_GUID = 0x00,
    OFF_INSTANCE_BLOCK_ITEM_ID = 0x0C,

    // `ObjectMgr::Guid2HexString(uint32_t lo, uint32_t hi, char *buf)` —
    // inverse of `HexString2Guid`. Writes `"0xHHHHHHHHHHHHHHHH"`
    // (uppercase hex, 19 bytes including the null terminator) into
    // the caller-supplied buffer. Same shape as awesome_wotlk's
    // `void Guid2HexString(guid_t guid, char* buf)` declaration.
    FUN_GUID_TO_HEXSTRING = 0x0074D0D0,

    // Equipment-slot id range (Lua 1-based). 19 paperdoll slots.
    EQUIPMENT_SLOT_FIRST = 1,
    EQUIPMENT_SLOT_LAST = 19,

    // Registers a single global Lua function. CDECL (3.3.5 moved from
    // 1.12's __fastcall). Body: lua_pushcclosure(L, func, 0);
    // lua_pushstring(L, name); lua_insert(L, -2);
    // lua_settable(L, LUA_GLOBALSINDEX).
    FUN_FRAMESCRIPT_REGISTER_FUNCTION = 0x00817F90,

    // Global `lua_State *`. Engine populates this in
    // FrameScript_Initialize after constructing the state; all the
    // batch registrars and FrameScript_RegisterFunction read it
    // through this pointer.
    VAR_LUA_STATE = 0x00D3F78C,

    // FrameScript in-game Lua-state teardown — `void __cdecl(void)`.
    // `lua_close`s the state at VAR_LUA_STATE, nulls the pointer, and
    // frees the event table. Runs on `/reload` and `/logout` (from
    // GameUIInit `FUN_0052A980` and the logout/shutdown paths) BEFORE the
    // matching FrameScript_Initialize (`FUN_00819BB0`) builds the new
    // state — the two are separate functions in 3.3.5 (1.12 fused them).
    // Pre-hooked in DllMain to run `Game::RunReloadCleanups()` while the
    // old state is still valid.
    FUN_FRAMESCRIPT_SHUTDOWN = 0x0081A9A0,

    // Lua 5.1 C API — all __cdecl, all live in the 0x0084xxxx cluster.
    // Cross-checked against `C:\Git\awesome_wotlk`, which has a hand-
    // curated table of these same offsets for the same 3.3.5 binary.
    //
    // Heads up — addresses that LOOK adjacent are easy to swap and
    // we hit two of those bugs before noticing:
    //   * `lua_newthread` (0x0084DB90) writes type tag 8 (TTHREAD)
    //     and was originally misidentified as `lua_newtable`. Pushing
    //     a thread onto the stack and assigning it to `_G[name]` left
    //     a malformed Table object in scope and crashed the next
    //     `_G` rehash in `luaH_resize → numusearray`.
    //   * `lua_rawget`/`lua_rawset` at 0x0084E600/0x0084E970 vs
    //     `lua_gettable`/`lua_settable` at 0x0084E590/0x0084E8D0 —
    //     the metamethod-aware pair is at the lower addresses.
    LUA_GET_TOP       = 0x0084DBD0, // int  lua_gettop(L)
    LUA_SET_TOP       = 0x0084DBF0, // void lua_settop(L, idx)
    LUA_CREATE_TABLE  = 0x0084E6E0, // void lua_createtable(L, narr, nrec) — use this for new tables
    LUA_SET_TABLE     = 0x0084E8D0, // void lua_settable(L, idx) — metamethod-aware (see the rawset/settable note above)
    LUA_INSERT        = 0x0084DCC0, // void lua_insert(L, idx)
    LUA_REMOVE        = 0x0084DC50, // void lua_remove(L, idx)
    LUA_PUSH_VALUE    = 0x0084DE50, // void lua_pushvalue(L, idx)
    LUA_TYPE          = 0x0084DEB0, // int  lua_type(L, idx)
    LUA_IS_NUMBER     = 0x0084DF20, // int  lua_isnumber(L, idx)
    LUA_IS_STRING     = 0x0084DF60, // int  lua_isstring(L, idx)
    LUA_TO_NUMBER     = 0x0084E030, // double lua_tonumber(L, idx)
    LUA_TO_LSTRING    = 0x0084E0E0, // const char *lua_tolstring(L, idx, len*)
    LUA_PUSH_NIL      = 0x0084E280, // void lua_pushnil(L)
    LUA_PUSH_NUMBER   = 0x0084E2A0, // void lua_pushnumber(L, double)
    LUA_PUSH_STRING   = 0x0084E350, // void lua_pushstring(L, const char *)
    LUA_PUSH_CCLOSURE = 0x0084E400, // void lua_pushcclosure(L, fn, nupvals)
    LUA_PUSH_BOOLEAN  = 0x0084E4D0, // void lua_pushboolean(L, int)
    LUA_GET_FIELD     = 0x0084E590, // void lua_getfield(L, idx, name) — atomic pushstring+gettable
    LUA_RAW_GET       = 0x0084E600, // void lua_rawget(L, idx)
    LUA_SET_FIELD     = 0x0084E900, // void lua_setfield(L, idx, name) — atomic pushstring+insert+settable
    LUA_RAW_SET       = 0x0084E970, // void lua_rawset(L, idx)
    LUAL_ERROR        = 0x0084F280, // int  luaL_error(L, fmt, ...) (cdecl, varargs)
    LUA_TO_USERDATA   = 0x0084E1C0, // void *lua_touserdata(L, idx) — returns NULL for non-userdata
    LUA_TO_BOOLEAN    = 0x0084E0B0, // int   lua_toboolean(L, idx) — returns 0 for nil/false/missing, 1 otherwise
    LUA_PCALL         = 0x0084EC50, // int   lua_pcall(L, nargs, nresults, errfunc) — protected call
    LUA_RAW_SETI      = 0x0084EA00, // void  lua_rawseti(L, idx, n) — t[n] = top, pops top
    // lua_pushlstring — binary-safe string push (keeps embedded NULs,
    // counts `len` explicitly). Derived from lua_pushstring (0x0084E350),
    // which computes strlen then tail-calls this with (L, s, len).
    LUA_PUSH_LSTRING  = 0x0084E300, // void lua_pushlstring(L, const char *s, size_t len)
    // lua_next — pops a key, pushes the next key+value pair (or nothing
    // and returns 0 at end of table). Cross-checked against awesome_wotlk.
    LUA_NEXT          = 0x0084EF50, // int  lua_next(L, idx)

    // `FrameScript_FireOnUpdate` — engine's per-frame dispatcher that
    // fires every Lua-bound `OnUpdate` handler. Cross-checked against
    // awesome_wotlk (uses this same offset for its own per-frame hook).
    // `__cdecl(int a1, int a2, int a3, int a4)`; body is just a pair
    // of helper calls (dispatch all frame OnUpdates + process delayed
    // events). We hook to drive `C_Timer.*` dispatch — args ignored.
    FUN_FRAMESCRIPT_FIRE_ON_UPDATE = 0x00495810,

    // Player spell-knowledge bitmap pointer — single-deref global
    // holding the base of a dword bitmap with one bit per spellID.
    // Bit `spellID` is set iff the player has learned that spell from
    // ANY source: trained class abilities, racials, talent passives,
    // and profession recipes (the latter critically NOT covered by
    // the spellbook arrays at `DAT_00BE6D88` — that's why the engine's
    // own `IsSpellKnown` returns false for profession recipes).
    //
    // The bit is set unconditionally by `FUN_00542030` (the spell-
    // learner used by SMSG_LEARNED_SPELL / SMSG_INITIAL_SPELLS / etc.)
    // *before* any branch on spell category. The bounds-checked
    // reader at `FUN_0053C5B0` uses the same `bitmap[spellID >> 5]
    // & (1 << (spellID & 31))` pattern ClassicAPI's 1.12 port uses.
    //
    // Same shape modern WoW's `IsPlayerSpell` reads.
    VAR_PLAYER_SPELL_BITMAP = 0x00BE8DC4,

    // Max spellID bound — direct integer value (no deref). Used to
    // gate bitmap reads so we never index past the allocated dword
    // range. Verified at the reader `FUN_0053C5B0`:
    // `CMP EDX, dword ptr [0x00AD49DC]; JBE ok` — direct dword read,
    // so the address holds the value itself.
    VAR_MAX_SPELL_ID = 0x00AD49DC,

    // Gate function for `GameTooltip:SetSpellByID` — `bool __cdecl
    // (uint spellID, int isPet)`. Called from exactly one site:
    // `Script_GameTooltip_SetSpellByID` (FUN_00625B90) before
    // building the tooltip. The original implementation walks the
    // player+pet spellbook arrays via `FUN_0053B4E0` and an action-
    // bar fallback via `FUN_005D3560`; returns false for any spell
    // not in those displayable structures, which includes profession
    // recipes (their bit is set only in `VAR_PLAYER_SPELL_BITMAP`).
    //
    // Hooked unconditionally to `return spellID != 0` so tooltips
    // work for ANY valid spellID — matches modern WoW (5.4+) where
    // Blizzard removed the gate. The blast radius is minimal: the
    // function has a single xref. Tooltip builder (FUN_006238A0)
    // handles unknown spells gracefully — produces a static tooltip
    // from `Spell.dbc` with no player-specific state (cooldown,
    // charges, etc.) filled in.
    FUN_SET_SPELL_BY_ID_GATE = 0x0053B930,

    // GameTooltip frame-method walker — `void __cdecl(lua_State *L)`.
    // Called at Lua-table-init time for the GameTooltip frame type
    // with the method table on top of the stack. Body is just
    //   FUN_0049E540(L);                       // parent-class methods
    //   FUN_008167E0(L, 0x00AD2AE0, 0x45);     // tooltip methods
    // We hook it to append extra (name, func) entries via a second
    // call to `FUN_REGISTER_FRAME_METHODS` after the original
    // returns. Multiple calls accumulate into the same table — the
    // registrar is just a `for (i=0; i<n; i++) { push key/value;
    // lua_settable; }` loop with no init step.
    FUN_TOOLTIP_METHODS_WALKER = 0x0061B4A0,

    // Generic frame-method registrar — `void __cdecl(lua_State *L,
    // const MethodEntry *table, int count)`. Each MethodEntry is an
    // 8-byte (name_ptr, func_ptr) pair. Walks the table and does
    // `T[name] = func` for each, where T is the table currently on
    // the Lua stack (already set up by the caller — typically
    // `FUN_00816790` creates the table, calls a per-type walker
    // like `FUN_TOOLTIP_METHODS_WALKER` which calls this with the
    // type's static method table, then assigns the populated table
    // into the Lua registry).
    FUN_REGISTER_FRAME_METHODS = 0x008167E0,

    // CGTooltip content-state offsets — the engine stores the
    // current spell / item / unit directly on the tooltip frame
    // whenever a `Set*` method is called. The `Get*` script
    // functions read these same offsets:
    //   spellID at +0x364   (verified at FUN_0061F0F0's `piVar1[0xd9]`)
    //   itemID  at +0x360   (verified at FUN_0061EF10's `piVar1[0xd8]`)
    //   unit GUID at +0x328..+0x32C (verified at FUN_0061DAD0's
    //                                  `piVar2[0xca]`/`piVar2[0xcb]`)
    // `Has*` is just "field != 0" for spell/item, and "either GUID
    // half != 0" for unit. Reset to zero when the tooltip clears.
    OFF_TOOLTIP_ITEM_ID = 0x360,
    OFF_TOOLTIP_SPELL_ID = 0x364,
    OFF_TOOLTIP_UNIT_GUID_LO = 0x328,
    OFF_TOOLTIP_UNIT_GUID_HI = 0x32C,

    // Talent system — TabInfo lookup. The engine stores three
    // separate `TabInfo *` arrays for (player / pet / inspect) and
    // picks between them in `FUN_005C5C60` based on isInspect/isPet
    // flags:
    //   (isInspect=0, isPet=0) → player tabs   (count C2101C / arr C21020)
    //   (isInspect=0, isPet=1) → pet tabs      (count C21028 / arr C2102C)
    //   (isInspect=1, isPet=0) → inspect tabs  (count C21130 / arr C21134)
    //   any other combo        → returns 0
    // We call the engine helper directly rather than re-implementing
    // the three-way switch — fewer addresses to maintain.
    //
    // Signature: `TabInfo *__cdecl(uint32_t tabIndex0Based, int isInspect, int isPet)`.
    // Returns the TabInfo pointer or NULL for out-of-range / mismatched
    // flag combo.
    //
    // TabInfo struct layout (this build):
    //   +0x04  numTalents (uint32)
    //   +0x08  talent entries pointer (TalentEntry*, stride 0x5C)
    //
    // TalentEntry struct layout (stride 0x5C = 92 bytes):
    //   +0x00  talentID (TalentDBC primary key)
    //   +0x08  tier (0-based; +1 for the Lua return)
    //   +0x0C  column (0-based; +1 for the Lua return)
    //   +0x10  SpellRank[9]   — 9 spellIDs, one per rank slot.
    //                          Slot 0 = rank 1, slot N = rank N+1.
    //                          Zero past max-rank for the talent.
    //
    // All struct offsets verified at `Script_GetTalentInfo`
    // (FUN_005C7800) — see the `puVar8[N]` index pattern:
    //   puVar8[0]    = talentID
    //   puVar8[2..3] = tier, column
    //   puVar8[4..12]= SpellRank[9]
    FUN_TALENT_TAB_INFO_LOOKUP = 0x005C5C60,
    OFF_TABINFO_NUM_TALENTS = 0x04,
    OFF_TABINFO_TALENT_ARRAY = 0x08,
    OFF_TALENT_DBC_ID = 0x00,
    OFF_TALENT_SPELL_RANK = 0x10,
    TALENT_ENTRY_STRIDE = 0x5C,
    TALENT_MAX_RANKS = 9,

    // Engine's `Script_GetTalentInfo` Lua C function. We call it from
    // `GetTalentSpellID` to derive the player's currentRank without
    // having to maintain our own per-talent rank state — its 5th
    // return is `currentRank`, populated from `puVar2[7]` after the
    // function's internal `FUN_005C77B0` lookup. The engine errors
    // (lua_error) on out-of-range tab/talent indices or pre-login
    // state, so callers must pre-validate before invoking.
    //
    // Signature: `int __cdecl Script_GetTalentInfo(lua_State *L)`.
    // Returns the number of Lua values pushed (10 in 3.3.5 — vs
    // 8 in 1.12). The 5th return is at stack index `-(n-4)`.
    FUN_SCRIPT_GET_TALENT_INFO = 0x005C7800,

    // Engine game-time struct. Populated from `SMSG_LOGIN_VERIFY_WORLD`
    // / `SMSG_LOGIN_SETTIMESPEED` and stepped forward every minute by
    // the engine's time keeper. The wire protocol carries minute
    // granularity only — no seconds field — so any addon-facing
    // "current time" needs `GetTickCount`-based interpolation between
    // minute boundaries.
    //
    // Layout (same shape as 1.12, verified at `Script_GetGameTime`
    // (FUN_00608230) which reads minute/hour, and the calendar getter
    // FUN_005B8160 which reads weekday/day/month/year with the
    // `+1` / `+2000` normalizations baked in):
    //   +0x00  minute       (0..59)
    //   +0x04  hour         (0..23)
    //   +0x08  weekday      (0..6, 0=Sunday)
    //   +0x0C  day-of-month (0-based; engine `+1`s before exposing to Lua)
    //   +0x10  month        (0-based; engine `+1`s)
    //   +0x14  year         (stored as `year - 2000`, e.g. 8 = 2008)
    //
    // Pre-login the struct is BSS-zero; we use `year == 0` as the
    // "uninitialized" sentinel.
    VAR_GAMETIME_STRUCT = 0x00D37F98,
    OFF_GAMETIME_MINUTE = 0x00,
    OFF_GAMETIME_HOUR = 0x04,
    OFF_GAMETIME_DAY = 0x0C,
    OFF_GAMETIME_MONTH = 0x10,
    OFF_GAMETIME_YEAR = 0x14,

    // Next daily-reset epoch (`time_t`-shaped Unix timestamp) — the
    // engine's own daily-reset clock, populated by the server-broadcast
    // calendar packet. Verified at `Script_GetQuestResetTime`
    // (FUN_005E6DE0), whose entire body is:
    //   `time_t now = _time32(NULL);
    //    if (DAT_00C23AF8 && DAT_00C23AF8 <= now) FUN_005E6940(1);
    //    push (DAT_00C23AEC - now);`
    // So `DAT_00C23AEC - time(NULL)` is "seconds until daily reset" —
    // respects whatever schedule the server actually uses (3am
    // Pacific on retail; arbitrary on private servers).
    //
    // Zero before the server broadcasts the reset schedule
    // (pre-login or early in the session); we treat that as "unknown"
    // and return nil.
    VAR_NEXT_DAILY_RESET_EPOCH = 0x00C23AEC,

    // Aura array on CGUnit. Unlike 1.12, 3.3.5 stores per-unit auras
    // directly on the CGUnit object (NOT in the updatefields descriptor),
    // with per-aura duration/expiration/caster broadcast for *all* units
    // — no player-buff-table side channel needed.
    //
    // Layout discovered in `Script_UnitAura`'s call chain
    // (FUN_006147c0 → FUN_0072c9b0 + FUN_00556e10). The array has two
    // storage modes depending on the sentinel at `+0xdd0`:
    //
    //   if unit[+0xdd0] != 0xFFFFFFFF:
    //       count = unit[+0xdd0]
    //       base  = unit + 0xc50           // inline storage
    //   else:
    //       count = unit[+0xc54]
    //       base  = *(unit + 0xc58)        // external buffer pointer
    //
    // Stride 0x18 (24 bytes). Each entry:
    //   +0x00  caster GUID low  (u32)
    //   +0x04  caster GUID high (u32)
    //   +0x08  spellID          (u32)  — 0 = empty slot
    //   +0x0C  flag byte        (u8)   — bit 7 = "is helpful" (set = HELPFUL)
    //   +0x0E  stack count      (u8)   — engine pushes verbatim (1 = 1 stack)
    //   +0x10  duration ms      (u32)  — *0.001 for seconds, 0 = infinite
    //   +0x14  expiration ms    (u32)  — engine ms, *0.001 = GetTime() epoch
    //
    // Stack/duration/expiration field offsets verified at FUN_006147c0
    // `puVar7+0xe`, `puVar7[4]`, `puVar7[5]`. The aura entry pointer
    // (puVar7) is the same one whose head is read by FUN_0060b420 to
    // resolve caster-GUID → unit token, so caster GUID lives at +0x00.
    OFF_CGUNIT_AURA_INLINE_COUNT       = 0xDD0,
    OFF_CGUNIT_AURA_INLINE_BASE        = 0xC50,
    OFF_CGUNIT_AURA_OVERFLOW_COUNT     = 0xC54,
    OFF_CGUNIT_AURA_OVERFLOW_BASE_PTR  = 0xC58,
    AURA_ENTRY_STRIDE                  = 0x18,

    OFF_AURA_CASTER_GUID_LO            = 0x00,
    OFF_AURA_CASTER_GUID_HI            = 0x04,
    OFF_AURA_SPELL_ID                  = 0x08,
    OFF_AURA_FLAGS                     = 0x0C,
    OFF_AURA_STACKS                    = 0x0E,
    OFF_AURA_DURATION_MS               = 0x10,
    OFF_AURA_EXPIRATION_MS             = 0x14,

    // Flag byte at OFF_AURA_FLAGS. Bit 7 is the engine's
    // `AFLAG_NEGATIVE` — set means HARMFUL, clear means HELPFUL.
    // (Matches 3.3.5 TrinityCore's `AuraFlags` enum:
    //  AFLAG_EFF_INDEX_0=0x01, _1=0x02, _2=0x04, _CASTER=0x08,
    //  _POSITIVE=0x10, _DURATION=0x20, _AMOUNT=0x40, _NEGATIVE=0x80.)
    // The engine's UnitAura test is `~(byte >> 7) & 1` — a "not
    // harmful" probe, not a "helpful" probe — so callers reading
    // this directly must invert.
    AURA_FLAG_HARMFUL                  = 0x80,

    // Bits 0..2 of the flag byte indicate which spell-effect indexes
    // this aura applies to (one bit per effect). An entry is considered
    // "live" by the engine's slot-table rebuild only when at least one
    // of these is set — a stale slot waiting for cleanup has bits 0..2
    // all clear. Engine check: `*(byte *)(entry + 0xC) & 7 != 0`.
    AURA_FLAG_EFF_INDEX_MASK           = 0x07,

    // `IsAuraStealable` — `__cdecl(targetUnit, auraEntry, spellRecord)`
    // -> bool. The engine's own predicate for whether a helpful aura
    // on `targetUnit` can be stolen by the local player (Spellsteal &
    // friends). Called inline from `Script_UnitAura` at
    // FUN_006147c0 — same logic, same dispel-mask global at
    // `VAR_PLAYER_STEALABLE_DISPEL_MASK`. Returns false for self
    // auras, non-magic dispel types, hostile/friendly mismatches,
    // and when the local player has no steal ability (mask == 0).
    //
    // 3.3.5 has Spellsteal (mage 30) so this is a real condition,
    // not always-false like in 1.12 (where it was added later).
    FUN_AURA_IS_STEALABLE              = 0x0053D680,

    // GUID → single unit-token resolver. `__cdecl(uint *guidPair)` → "player" |
    // "target" | "partyN" | "raidN" | "pet" | "partypetN" | "raidpetN" |
    // "arenaN" | "vehicle" | "focus" | "mouseover" | "npc" | NULL (no live token
    // maps to the GUID). Writes party/raid/arena formats into a shared static
    // buffer at 0x00C25B7C — caller must consume (e.g. push to Lua, which copies
    // into the Lua heap) before the next call. Wrapped by Unit::GuidToToken.
    // Used for aura casters (matching the engine's own UnitAura) and as
    // UnitTokenFromGUID's "mouseover" fallback. NOTE: this resolver is NOT the
    // one client mods hook to inject custom tokens — see FUN_GUID_TO_UNIT_KEYWORDS.
    FUN_GUID_TO_UNIT_TOKEN             = 0x0060B420,

    // GUID → all current unit-token keywords. `__cdecl(const uint *guidPair,
    // int *outCount) -> char **tokens` (NULL if the local player isn't in
    // world). Fills a shared static buffer (0x00AD2780) with every token naming
    // the GUID right now — "player", "pet", "partyN", "raidN", "arenaN",
    // "bossN", "target", "focus", "npc" (no "mouseover") — and sets *outCount.
    // This is the engine's own chokepoint for naming a unit when it dispatches
    // UNIT_* events (FUN_0060BF10 fires each event once per keyword), so tokens
    // other loaded client mods inject by hooking it — e.g. awesome_wotlk's
    // "nameplateN" — appear here too. Wrapped by Unit::GuidToTokens. Consume the
    // strings before the next engine call.
    FUN_GUID_TO_UNIT_KEYWORDS          = 0x0060BB70,

    // Spell.dbc record-copy helper. `__thiscall(this, id, *out)` →
    // copies the 680-byte (0x2A8) record into *out, returns 1 on hit
    // / 0 on miss. `this` = `VAR_SPELL_DBC_DESC`; descriptor layout
    // (relative to `this`):
    //   +0x0C = max id (alias of existing VAR_MAX_SPELL_ID = 0xAD49DC)
    //   +0x10 = min id
    //   +0x20 = pointer to `int *records[]` array
    // The engine routes through a locale-aware copy path
    // (FUN_004CFBB0) when DAT_00C5DEA0 is set — either branch leaves
    // the OUTPUT BUFFER fields at the same offsets, so callers
    // unconditionally read out at OFF_SPELL_*.
    FUN_DBC_COPY_RECORD                = 0x004CFD20,
    VAR_SPELL_DBC_DESC                 = 0x00AD49D0,
    SPELL_DBC_RECORD_SIZE              = 0x2A8,

    // Pointer-returning DBC lookup. `__thiscall(this, id)` →
    // `records[id - min]` or 0 if out of range. The "anchor" `this`
    // points to the records-array field directly (NOT the descriptor
    // base) — relative offsets are:
    //   *(this - 0xC) = max id
    //   *(this - 0x8) = min id
    //   *(this + 0x8) = records-array base
    FUN_DBC_GET_RECORD_PTR             = 0x0065C290,
    VAR_SPELLDISPEL_DBC_ANCHOR         = 0x00AD4814,
    VAR_SPELLICON_DBC_ANCHOR           = 0x00AD48A4,

    // Spell.dbc record field offsets within the buffer that
    // FUN_DBC_COPY_RECORD populates. Verified at FUN_006147c0 — see
    // the Ghidra-decoded local frame layout in that function:
    //   local_2c0  (record + 0x008) = dispel type ID
    //   local_b4   (record + 0x214) = SpellIcon.dbc ID
    //   local_a8   (record + 0x220) = locale-resolved spell name
    //   local_a4   (record + 0x224) = locale-resolved rank text
    OFF_SPELL_DISPEL_TYPE_ID           = 0x008,
    OFF_SPELL_ICON_DBC_ID              = 0x214,
    OFF_SPELL_NAME                     = 0x220,
    OFF_SPELL_RANK                     = 0x224,

    // SpellDispelType.dbc record fields. `+0x0C` is the engine's
    // "has name" sentinel — when zero, the engine treats the row as
    // having no displayable name.
    OFF_SPELLDISPEL_HAS_NAME           = 0x0C,
    OFF_SPELLDISPEL_NAME               = 0x10,

    // SpellIcon.dbc record field — single string pointer. In 3.3.5
    // this is the FULL texture path (e.g. "Interface\\Icons\\Spell_Holy_Renew"),
    // not just the basename — the engine's UnitAura pushes it
    // verbatim and modern callers consume it as-is.
    OFF_SPELLICON_PATH                 = 0x04,

    // Spellbook name → spellID resolver. `int __cdecl(const char *name,
    // int *outBookType)`. Looks `name` up in the engine's spellbook name index
    // (`DAT_00be8e64`) and returns the matching spellID, writing the book (0 =
    // player, 1 = pet) to `*outBookType`. Honors a trailing "(subtext)" as a
    // rank selector ("Fireball(Rank 4)"); with no subtext, returns the highest
    // known rank. Returns 0 for a name the player's/pet's spellbook doesn't
    // contain — the same spellbook scoping the native GetSpellTexture/GetSpellInfo
    // name path uses. Verified via Script_GetSpellTexture (FUN_00540d70 →
    // resolver FUN_00540670 → FUN_00540200 → FUN_0053f5e0). The '(' split and
    // leading '!' strip are done inside this function, so callers pass the raw
    // identifier string.
    FUN_SPELL_NAME_TO_ID               = 0x00540200,

    // Additional Spell.dbc record fields within the FUN_DBC_COPY_RECORD output
    // buffer (the same buffer OFF_SPELL_NAME / OFF_SPELL_ICON_DBC_ID target),
    // used to assemble C_Spell.GetSpellInfo. Verified against the native
    // Script_GetSpellInfo (FUN_00540a30) and the cost/casttime/range helpers it
    // calls (FUN_008012f0 / FUN_007ff180 / FUN_007ff480):
    //   +0x18  AttributesEx2  (bit 0x800 = SPELL_ATTR_EX2_HEALTH_FUNNEL)
    //   +0x70  CastingTimeIndex → SpellCastTimes.dbc
    //   +0xA4  powerType (0 = mana, 1 = rage, 2 = focus, 3 = energy, -2 = health, …)
    //   +0xB8  RangeIndex → SpellRange.dbc
    OFF_SPELL_ATTRIBUTES_EX2           = 0x18,
    OFF_SPELL_CASTING_TIME_INDEX       = 0x70,
    OFF_SPELL_POWER_TYPE               = 0xA4,
    OFF_SPELL_RANGE_INDEX              = 0xB8,
    SPELL_ATTR_EX2_HEALTH_FUNNEL       = 0x800,

    // SpellCastTimes.dbc — pointer-anchored store (read via FUN_DBC_GET_RECORD_PTR,
    // same shape as the SpellIcon / SpellDispel anchors). Base cast time in ms at
    // record+4. From FUN_007ff180: castRec[1]=base, castRec[2]=per-skill scale,
    // castRec[3]=min clamp — the flat base is castRec[1] (0 for instant spells).
    VAR_SPELLCASTTIMES_DBC_ANCHOR      = 0x00AD4760,
    OFF_SPELLCASTTIMES_BASE_MS         = 0x04,

    // SpellRange.dbc — pointer-anchored store. minRange float at record+4,
    // maxRange float at record+0xC (the default/hostile range set; the
    // friend-range set is the parallel pair at +8 / +0x10). From FUN_007ff480:
    // min = rangeRec[1 + set], max = rangeRec[3 + set], with set = 0 here.
    VAR_SPELLRANGE_DBC_ANCHOR          = 0x00AD49A0,
    OFF_SPELLRANGE_MIN                 = 0x04,
    OFF_SPELLRANGE_MAX                 = 0x0C,

    // Quest static-info cache (`DBCache<QuestCache, int, HASHKEY_INT>`)
    // — same generic shape as the item cache (`FUN_DBCACHE_ITEMSTATS_GET_RECORD`).
    // `__thiscall(this, questID, *outBuf, callback, userData, char unused)`
    // → returns the data block (`entry + 0x18`) on cache hit, NULL on
    // miss (and queues a `CMSG_QUEST_QUERY` if `callback != nullptr`).
    //
    // The single global instance lives at `VAR_QUEST_CACHE`; the
    // string `"questcache.wdb"` (its WDB filename) cross-references
    // there. Verified by following `Script_GetQuestLink` at
    // FUN_005E51D0, which calls `FUN_0067DE90(&DAT_00C5DA48, questID,
    // ...)` then reads `puVar3[2]` (quest level) at offset +0x08 of
    // the returned record.
    FUN_DBCACHE_QUEST_GET_RECORD       = 0x0067DE90,
    VAR_QUEST_CACHE                    = 0x00C5DA48,

    // Inline `char title[N]` inside the data block returned by the
    // quest cache's `_GetRecord`. Derivation: the engine's
    // `FUN_005DEC70(questID)` helper does exactly:
    //   puVar1 = FUN_0067DE90(&DAT_00C5DA48, questID, ..., callback,
    //                         0, 0);
    //   if (puVar1 != nullptr) return puVar1 + 0x2D;
    // With `puVar1` typed as `uint *`, `+0x2D` advances 0x2D * 4 = 0xB4
    // bytes. So the title is a null-terminated C string at +0xB4.
    // (1.12's analog is at +0x9C — the layout drifted between builds.)
    OFF_QUEST_TITLE                    = 0xB4,

    // Gossip-state arrays. Populated by `SMSG_GOSSIP_MESSAGE`'s handler
    // and read by the engine's `Script_GetGossip*` Lua functions. We
    // walk the same arrays directly to produce modern `C_GossipInfo.*`
    // table-shaped returns.
    //
    // **Options** — inline array of 32 entries × 24-byte stride
    // (`GOSSIP_OPTIONS_STRIDE = 0x18`). Verified at
    // `Script_GetGossipOptions` (FUN_0058A9E0): iterates from
    // `&DAT_00C00BF0` incrementing by 6 dwords / entry until the
    // greeting-text buffer at +0x300 (= 32 × 24 = 0x300 bytes later).
    // Sentinel: `entry[+0x08] == -1` marks an empty slot. The
    // `Script_SelectGossipOption` helper (FUN_0058AF10) also caps
    // `param_1 < 0x20`, confirming `GOSSIP_OPTIONS_MAX = 32`.
    //
    // Entry fields (verified across `Script_GetGossipOptions` and
    // `Script_SelectGossipOption`):
    //   +0x00  const char *name    — option text, pushed verbatim
    //   +0x04  uint32 moneyCost    — gold the option charges (3.3.5
    //                                added; 1.12 didn't have option-
    //                                level money). Engine errors with
    //                                "%d%s%d" if non-zero and caller
    //                                passes copperCost=0.
    //   +0x08  int32  gossipOptionID  (sentinel -1 = empty)
    //   +0x0C  uint32 flags        — bit 0 = boxCoded (password required)
    //   +0x10  uint32 requiredMoney
    //   +0x14  uint32 icon         — gossip-type ID (0..N indexing into
    //                                the engine's string table at
    //                                &PTR_s_gossip_00ACEF20 for the
    //                                "gossip"/"vendor"/etc. names)
    VAR_GOSSIP_OPTIONS                = 0x00C00BF0,
    GOSSIP_OPTIONS_STRIDE             = 0x18,
    GOSSIP_OPTIONS_MAX                = 32,
    OFF_GOSSIP_OPTION_NAME            = 0x00,
    OFF_GOSSIP_OPTION_MONEY_COST      = 0x04,
    OFF_GOSSIP_OPTION_INDEX           = 0x08,
    OFF_GOSSIP_OPTION_FLAGS           = 0x0C,
    OFF_GOSSIP_OPTION_REQUIRED_MONEY  = 0x10,
    OFF_GOSSIP_OPTION_ICON            = 0x14,
    GOSSIP_OPTION_FLAG_BOX_CODED      = 0x1,

    // **Greeting text** — inline char buffer immediately after the
    // options array. `Script_GetGossipText` (FUN_0058A900) is just
    // `lua_pushstring(L, &DAT_00C00EF0)`.
    VAR_GOSSIP_GREETING_TEXT          = 0x00C00EF0,

    // **Quests** — inline array of 32 entries × 532-byte stride
    // (`GOSSIP_QUESTS_STRIDE = 0x214`). Verified at the count helpers
    // FUN_0058A5D0 (available) and FUN_0058A6C0 (active): both walk
    // from `&DAT_00BFC968` in stride-0x214 increments, with the loop
    // bound at `uVar2 < 0x4280` (= 32 × 0x214). The same accessors
    // also confirm the entry sentinel and status semantics:
    //   `entry[+0x00] == 0`  → end of list
    //   `entry[+0x10] in {3, 4}` → ACTIVE quest (in player's log)
    //   `entry[+0x10] otherwise` → AVAILABLE quest (offered, not taken)
    // `entry[+0x10] == 4` specifically = "ready to turn in" — the
    // bit we surface as `isComplete` in the modern shape.
    //
    // Entry fields:
    //   +0x00  uint32 questID    — sentinel 0 = end of list
    //   +0x04  int32  questLevel
    //   +0x08  uint32 flags      — bit 0x1000 = repeatable
    //   +0x0C  uint32 extraFlag  — non-zero on some entries; 3.3.5-
    //                              specific, exposed by the engine's
    //                              5th return value but unnamed
    //   +0x10  uint32 status     — 3 = ACTIVE, 4 = ACTIVE_COMPLETE,
    //                              otherwise AVAILABLE
    //   +0x14  char title[N]     — inline null-terminated string
    //                              (stride leaves ~0x200 bytes here)
    VAR_GOSSIP_QUESTS                 = 0x00BFC968,
    GOSSIP_QUESTS_STRIDE              = 0x214,
    GOSSIP_QUESTS_MAX                 = 32,
    OFF_GOSSIP_QUEST_ID               = 0x00,
    OFF_GOSSIP_QUEST_LEVEL            = 0x04,
    OFF_GOSSIP_QUEST_FLAGS            = 0x08,
    OFF_GOSSIP_QUEST_STATUS           = 0x10,
    OFF_GOSSIP_QUEST_TITLE            = 0x14,
    GOSSIP_QUEST_FLAG_REPEATABLE      = 0x1000,
    GOSSIP_QUEST_STATUS_ACTIVE        = 3,
    GOSSIP_QUEST_STATUS_COMPLETE      = 4,

    // Engine selectors — call directly, skipping the Lua-stack stomp
    // of going through `Script_SelectGossip*`.
    //
    // SelectOption: `__cdecl(uint slot0Based, const char *password, int copperCost)`.
    // SelectAvailable/ActiveQuest: `__cdecl(int filteredIdx0Based)` —
    // index is into the FILTERED list (status != 3,4 for available;
    // status in {3,4} for active), not the raw slot.
    FUN_GOSSIP_SELECT_OPTION           = 0x0058AF10,
    FUN_GOSSIP_SELECT_AVAILABLE_QUEST  = 0x0058B070,
    FUN_GOSSIP_SELECT_ACTIVE_QUEST     = 0x0058B120,

    // `Script_CloseGossip` Lua C function — direct Lua dispatch.
    // Internally just calls FUN_0058A550 with a zeroed local buffer
    // and returns 0; reusing it instead of duplicating the body
    // means we share the engine's CMSG-send path verbatim.
    FUN_SCRIPT_CLOSE_GOSSIP            = 0x0058AA40,

    // Quest-greeting path (SMSG_QUESTGIVER_QUEST_LIST -> QUEST_GREETING,
    // the "Greetings, $N / Current Quests / Available Quests" panel). An
    // NPC with only quests and no gossip menu uses this instead of
    // SMSG_GOSSIP_MESSAGE, and its data lands in storage entirely separate
    // from the gossip arrays above — so C_GossipInfo has to serve whichever
    // questgiver session is live:
    //   gossip live  (VAR_GOSSIP_NPC_GUID != 0)                 -> gossip arrays
    //   greeting live(VAR_QUESTGIVER_GUID != 0 && == greeting)  -> greeting arrays
    //   neither                                                 -> empty / ""
    // Both storages persist after their frame closes, so the GUID gates
    // double as staleness protection (an ungated read reports the last NPC).

    // Session GUIDs (u64). VAR_GOSSIP_NPC_GUID: set by the gossip handler
    // FUN_0058A550, zeroed on close (fires GOSSIP_CLOSED 0x11C).
    // VAR_QUESTGIVER_GUID: current questgiver-panel GUID.
    // VAR_GREETING_NPC_GUID: the GUID the greeting arrays were filled for
    // (written only by the SMSG_QUESTGIVER_QUEST_LIST handler FUN_006D0240).
    VAR_GOSSIP_NPC_GUID               = 0x00C016F0,
    VAR_QUESTGIVER_GUID               = 0x00C0D648,
    VAR_GREETING_NPC_GUID             = 0x00C9D548,

    // Greeting quest arrays: available (offered) and active (in-log), 32
    // entries each, stride 0x214. Per-entry: questID @+0x00, level @+0x04,
    // title (char[0x200]) @+0x0C. Counts are separate ints. Confirmed via
    // GetAvailableTitle (FUN_0058BE30) / GetActiveTitle (FUN_0058BED0).
    VAR_GREETING_AVAILABLE_ENTRIES    = 0x00C05AE8,
    VAR_GREETING_ACTIVE_ENTRIES       = 0x00C01868,
    VAR_GREETING_AVAILABLE_COUNT      = 0x00C0D69C,
    VAR_GREETING_ACTIVE_COUNT         = 0x00C0D6A0,
    GREETING_QUESTS_STRIDE            = 0x214,
    GREETING_QUESTS_MAX               = 32,
    OFF_GREETING_QUEST_ID             = 0x00,
    OFF_GREETING_QUEST_LEVEL          = 0x04,
    OFF_GREETING_QUEST_TITLE          = 0x0C,

    // Greeting greeting-text buffer (char[0x800]) — GetGreetingText
    // (FUN_0058BD30) pushes it.
    VAR_QUEST_GREETING_TEXT           = 0x00C0CC48,

    // Greeting-session engine helpers, all `__cdecl(int idx0Based)` where
    // the index is 0-based into the respective greeting array. The select
    // workers validate the questgiver GUID + busy flag internally.
    FUN_GREETING_SELECT_AVAILABLE_QUEST = 0x0058CC20,
    FUN_GREETING_SELECT_ACTIVE_QUEST    = 0x0058CCB0,
    // `int __cdecl(uint activeIdx0Based)` -> 1 when the active greeting
    // quest at that index is complete / ready to turn in (the 2nd return
    // of GetActiveTitle). Used for the greeting `isComplete` field.
    FUN_GREETING_ACTIVE_IS_COMPLETE     = 0x0058BCB0,

    // Quest log entry array. Populated by SMSG_QUEST_QUERY_RESPONSE and
    // friends; read by every `Script_GetQuestLog*` that takes a log
    // index. Verified at `Script_GetQuestLogTitle` (FUN_005E5CC0):
    //   uVar7 = lua_arg - 1                       // Lua 1-based → 0-based
    //   if (uVar7 < 0 || uVar7 >= DAT_00c23ad0) bail
    //   if ((&DAT_00c237b8)[uVar7 * 4] != 0) "is header" → bail
    //   questID = (&DAT_00c237b0)[uVar7 * 4]
    //
    // Entry stride 0x10 (4 dwords). Layout:
    //   +0x00  questID
    //   +0x04  (level — `(&DAT_00c237b4)[uVar7 * 4]`, used as suggestedGroup
    //          source elsewhere; not needed for questID-only path)
    //   +0x08  isHeader sentinel (non-zero = category header, not a real quest)
    //   +0x0C  isComplete flag (`(&DAT_00c237bc)[uVar7 * 4]`)
    VAR_QUEST_LOG_ENTRIES              = 0x00C237B0,
    VAR_QUEST_LOG_COUNT                = 0x00C23AD0,
    QUEST_LOG_ENTRY_STRIDE             = 0x10,
    OFF_QUEST_LOG_QUEST_ID             = 0x00,
    OFF_QUEST_LOG_IS_HEADER            = 0x08,
    // Inline "active-complete" flag — non-zero = objectives done,
    // ready for turn-in. Verified in `Script_GetQuestLogTitle`
    // (FUN_005E5CC0) at the `(&DAT_00c237bc)[uVar7 * 4] != 0` test
    // that gates the "complete" return value. Same semantic as
    // gossip's `GOSSIP_QUEST_STATUS_COMPLETE = 4`.
    //
    // NOTE: this flag is only set when the *server* has marked the
    // quest complete (e.g. SMSG_QUESTUPDATE_COMPLETE landed). It is
    // NOT set for quests that are "intrinsically complete" — quests
    // with no objectives ("talk to NPC X", auto-complete quests). For
    // those, fall through to `FUN_QUEST_IS_COMPLETABLE` below.
    OFF_QUEST_LOG_IS_COMPLETE          = 0x0C,

    // `bool __cdecl IsQuestCompletable(uint32_t logIndex0, char strict)`
    // — engine helper used by `Script_GetQuestLogTitle` (FUN_005E5CC0)
    // as the second "completable" probe after the inline `+0x0C` flag.
    //
    // Walks the quest cache record (via FUN_DBCACHE_QUEST_GET_RECORD)
    // for the quest at `logIndex0`, then checks objective satisfaction
    // (item counts in inventory, monster kill counts, money earned)
    // against the live progress data on the log entry.
    //
    // The `strict` arg controls a fast-path bail (verified at 0x005E0FC7
    // in disassembly): when strict=1 AND the quest has zero declared
    // objectives AND no required money, the function returns false
    // immediately. `Script_GetQuestLogTitle` passes strict=1 because it
    // only wants to display "complete" for explicitly-marked quests.
    //
    // For `ReadyForTurnIn` semantics — "is this quest ready to hand in
    // right now?" — pass **strict=0** so the function falls through to
    // the objective-progress evaluation. For zero-objective quests with
    // no required money, the final compare `progress_money (0) >=
    // required_money (0)` returns true, matching what the user expects
    // ("talk to NPC X"-style quests are immediately turn-in-able).
    //
    // Returns false unconditionally for: out-of-range index, headers,
    // local-player resolve failure, or any quest whose cache record
    // isn't yet loaded — so callers may need to pre-warm the quest
    // cache (see [[Quest::Cache]]).
    FUN_QUEST_IS_COMPLETABLE           = 0x005E0EA0,

    // Engine event registry. The "table" at VAR_EVENT_TABLE is a
    // hash-bucketed name → entry map, not a flat array (different
    // layout from 1.12's stride-0x10 array). The simplest way to
    // check name validity is to call the engine's own lookup helper
    // directly — returns the entry struct or NULL for unknown names.
    //
    // FUN_EVENT_TABLE_FIND is __thiscall(ecx = &VAR_EVENT_TABLE,
    // const char *name) → entry * | NULL. Used internally by
    // Frame::RegisterEvent at 0x0081B380 as its first step before
    // wiring the frame's event subscription.
    VAR_EVENT_TABLE      = 0x00D3F7A8,
    FUN_EVENT_TABLE_FIND = 0x004BC410,

    // Addon TOC executor — `int __cdecl(char *toc_path, char *addon_name,
    // void *unused, void **logger)`. Reads the TOC file line-by-line and
    // dispatches each non-`#` entry through the per-file loader
    // (`FUN_00813ee0`), which routes `.lua` files to `FUN_00818f60` and
    // ultimately `lua_pcall(chunk, name, namespaceTable)`.
    //
    // We hook this for `C_AddOns.GetAddOnLocalTable`: the LoadAddOn flow
    // at FUN_005f80b0 pushes a fresh `lua_newtable` onto the Lua stack
    // immediately before calling this — that table is the per-addon
    // namespace bound as the second vararg to every addon file. By
    // dup'ing and stashing it in our own registry table at entry, we
    // keep a stable reference past the LoadAddOn flow's terminal
    // `lua_settop(L, -2)` that would otherwise drop it for GC.
    //
    // Three callers — FUN_005f80b0 (LoadAddOn), FUN_0052a980 (GameUIInit
    // loading FrameXML.toc), FUN_004da5f0 (glue-side TOC) — but only
    // the LoadAddOn path leaves a table on the Lua stack at entry, so
    // our hook gates on (a) path prefix "Interface\\AddOns\\" and
    // (b) `lua_type(L, -1) == LUA_TTABLE` to ignore the others.
    FUN_TOC_EXECUTOR = 0x00814340,

    // --- Embedded `!!!WrathClassicAPI` addon (src/addons/Embedded.cpp) ---

    // Engine's main file-content reader (MPQ or loose disk file).
    // `__stdcall`, `RET 0x1C` (7 stack args, callee-cleaned):
    //   int FileRead(int unused, const char *path, void **outBuf,
    //                size_t *outSize, size_t extraBytes,
    //                int flag1, int flag2)
    // Opens `path`, SMemAllocs a buffer of `content + extraBytes`,
    // copies the file and zero-fills the trailing `extraBytes`, stores
    // buffer/size through outBuf/outSize (outSize MAY be NULL — the TOC
    // parser calls it that way), returns 1 on success / 0 on miss. We
    // hook it to serve embedded addon files on a disk miss.
    FUN_FILE_READ = 0x00424E80,

    // Storm allocator pair. Both `__stdcall`, `RET 0x10`:
    //   void *SMemAlloc(uint size, const char *file, int line, int flags)
    //   int   SMemFree (void *ptr,  const char *file, int line, int flags)
    // A buffer we SMemAlloc in the file-read hook is freed cleanly by the
    // engine's own SMemFree when it finishes with the file.
    FUN_STORM_SMEM_ALLOC = 0x0076E540,
    FUN_STORM_SMEM_FREE = 0x0076E5A0,

    // Addon-subsystem init: `void __cdecl AddonInit(char *basePath)`.
    // Runs registry setup, then the disk scan FUN_ADDON_DISK_SCAN, then sets
    // the "addons initialized" flag `DAT_00C24918 = 1` (VAR_ADDON_INITIALIZED).
    FUN_ADDON_INIT = 0x005F9080,

    // The login disk scan, `void __cdecl DiskScan(void)` — called once by
    // FUN_ADDON_INIT after registry setup. Registers every on-disk addon (each
    // appended at the list TAIL via the intrusive-list insert) and builds the
    // reverse-LoadWith lists. Embedded.cpp PRE-hooks it to register
    // `!!!WrathClassicAPI` BEFORE the scan runs, so the embedded addon lands at
    // the HEAD of the load-order list and loads FIRST — the load pass
    // (FUN_005F84A0) walks head->tail, so a post-scan registration would load
    // LAST, after addons that consume the embedded globals.
    FUN_ADDON_DISK_SCAN = 0x005F8F50,

    // AddOnEntry security level (dword): 0 = SECURE, 1 = INSECURE, 2 = BANNED.
    // LoadAddOn (FUN_005F80B0) derives the taint stamped on the addon's Lua
    // chunks/closures from it: `taint = entry[0x24] != 0 ? addonName : 0` (the
    // taint then rides the pushed name string via FUN_0084E300, so a nonzero
    // level re-taints every file even if the global is cleared later). Tainted
    // code is BLOCKED from protected actions (ADDON_ACTION_BLOCKED — e.g. the
    // game menu on Escape). Forcing our entry to 0 loads it SECURE (untainted),
    // the same path Blizzard's own secure addons take. The load gate treats 2
    // as banned; 0 loads fine. WotLK-only (1.12 has no taint system).
    OFF_ADDON_ENTRY_SECURITY = 0x24,

    // LoadAddOn — `uint32 __cdecl(char *name, uint32 flags, int *ctx)`. Loads a
    // registered addon (recursively resolving deps), stamps the load taint from
    // the entry's security level, then runs its TOC. Pre-hooked to mark
    // `!!!WrathClassicAPI` SECURE (OFF_ADDON_ENTRY_SECURITY = 0) right before
    // each load (login and every /reload), so the write can't be raced.
    FUN_ADDON_LOADADDON = 0x005F80B0,

    // Fatal-error dispatcher: `void __cdecl FatalError(uint code)` — stores the
    // code in DAT_00B2F9A4 and tail-jumps into process teardown; the exit path
    // shows a localized popup keyed off it. Code 10 = "interface files are
    // corrupt". LoadAddOn fires it for SECURE entries whose 16-byte file digest
    // (accumulated over the TOC + every file as read) mismatches the expected
    // digest at entry+0x1D2 — Blizzard's secure addons carry a signed digest;
    // our parser-registered embedded entry has none, so the check can never
    // pass. Embedded.cpp hooks this and swallows code 10 ONLY while the
    // embedded addon is loading (flag-scoped, unlike ClassicAPI's global
    // suppression) — genuine corruption elsewhere still terminates.
    FUN_FATAL_ERROR = 0x004033C0,

    // Parse + register ONE addon by name. UNUSUAL ABI: the single
    // `const char *addonName` is passed in **EAX** (a register-call the
    // compiler emitted — both call sites load EAX and `CALL` with no
    // push), plain `RET`. Call it via the inline-asm thunk in
    // Embedded.cpp. Reads `Interface\AddOns\<name>\<name>.toc` via
    // FUN_FILE_READ, builds a registry entry, appends it at the list
    // HEAD (so a post-scan registration loads FIRST), and parses
    // `## DefaultState: enabled` into entry+0x2b. Dedup-safe: early-out
    // via a name hash lookup, so this is a no-op when the user already
    // has the addon on disk (the scan registered it first).
    FUN_TOC_PARSER = 0x005F86A0,

    // Addon name -> entry lookup (the same hash `FUN_TOC_PARSER` uses for
    // its dedup check). `__thiscall`, `RET 0x4`:
    //   AddOnEntry *Lookup(void *nameHash /*ECX*/, const char *name)
    // Returns the entry base (entry+0x14 is its name pointer) or NULL. We
    // call it after registering to fetch our own entry and hide it.
    FUN_ADDON_HASH_LOOKUP = 0x0055F4D0,

    // The addon-registry name hash table (the `this` for the lookup above;
    // `&DAT_00C2491C` at the TOC parser's call site).
    VAR_ADDON_NAME_HASH = 0x00C2491C,

    // AddOnEntry field: "filter out of the AddOns list" byte. The
    // char-select list builder `FUN_005F79A0` copies an entry into the
    // display array ONLY when this byte is 0, so setting it to 1 hides the
    // addon from the list. Does NOT affect loading (the load pass walks the
    // raw linked list and never reads it). NOTE: +0x28 is `## Secure:` and
    // +0x2c is `## LoadOnDemand:` — the filter byte is +0x29.
    OFF_ADDON_ENTRY_FILTER_OUT = 0x29,

    // --- Retail-like `/reload` hot reload (src/addons/Rescan.cpp) ---
    //
    // Registry field map (verified from the TOC parser `FUN_005F86A0`):
    //   name@+0x14, Secure@+0x28, filter-out@+0x29, DefaultState@+0x2b,
    //   LoadOnDemand@+0x2c; growable descriptors {cap,count,data,quantum}
    //   at LoadWith@+0x58 and reverse-LoadWith@+0x98.

    // AddOnEntry name pointer (`char *`). The registry walk / display
    // rebuild read it; `FUN_005F86A0` writes it (entry[5]).
    OFF_ADDON_ENTRY_NAME_PTR = 0x14,

    // `## Secure:` byte — SMSG-managed/packet-delivered entries. Excluded
    // from the evict+re-register path (the parser can't restore that state).
    OFF_ADDON_ENTRY_SECURE = 0x28,

    // The addon registry's intrusive-list control + head. Walk (verified
    // identical in the load pass FUN_005F84A0, the scan, and the display
    // builder):  linkOffset = *(int*)VAR_ADDON_LIST_CTRL;
    //            entry = *(uintptr_t*)VAR_ADDON_LIST_HEAD;
    //            next  = *(uintptr_t*)(entry + linkOffset + 4);
    //            stop when (entry & 1) || entry == 0.
    VAR_ADDON_LIST_CTRL = 0x00C24920,
    VAR_ADDON_LIST_HEAD = 0x00C24928,

    // `u8` set to 1 by FUN_ADDON_INIT once the login disk scan completes.
    // Gate: the rescan only runs once the registry is populated.
    VAR_ADDON_INITIALIZED = 0x00C24918,

    // Replay the login disk scan to register new folders. NOT __fastcall —
    // `bool __cdecl ScanDiskDirs(char *basePath, void *pattern,
    //                            void *perDirCB, void *userParam, int hidden)`.
    // Call site (FUN_005F8F50):
    //   ScanDiskDirs("Interface\\AddOns\\", "*", FUN_005F8F30, 0, 0)
    // The per-directory callback feeds names to the dedup-safe TOC parser.
    FUN_ADDON_SCAN_DISK_DIRS = 0x00462000,
    VAR_ADDON_PATH_PREFIX = 0x00A1D74C,  // "Interface\AddOns\"
    VAR_ADDON_SCAN_PATTERN = 0x009E3EC8, // "*"
    FUN_ADDON_DISK_DIR_CB = 0x005F8F30,  // __cdecl(FindInfo*)

    // Complete per-entry destructor: `void __stdcall EntryDestroy(void *entry)`
    // (`RET 0x4`). Frees the name and every owned desc array, then self-
    // unlinks from BOTH the registry list and the name hash (via FUN_007F4AC0)
    // and Storm-frees the struct. Does NOT scrub this entry's pointer out of
    // OTHER entries' reverse-LoadWith lists — the caller must do that first.
    FUN_ADDON_ENTRY_DESTROY = 0x005F7240,

    // Reverse-LoadWith descriptor {cap@+0x98, count@+0x9c, data@+0xa0,
    // quantum@+0xa4}: the entries that name THIS one in `## LoadWith:`.
    OFF_ADDON_REVLOADWITH_DESC = 0x98,
    // The grow instantiation that reallocs the reverse-LoadWith `data`
    // (`AddOnEntry*[]`): `__thiscall(desc /*ECX*/, uint newCap)`.
    FUN_ADDON_REVLOADWITH_GROW = 0x005F4E30,

    // Forward `## LoadWith:` descriptor count + data (header @+0x58).
    OFF_ADDON_LOADWITH_COUNT = 0x5C,
    OFF_ADDON_LOADWITH_ARRAY = 0x60,

    // The flat `GetNumAddOns`/`GetAddOnInfo(i)` display-array descriptor
    // {cap@0xC24944, count@0xC24948, data@0xC2494C, quantum@0xC24950} and its
    // grow (`__thiscall(desc, newCap)`). Rebuilt from the linked list after a
    // membership/identity change, then sorted with the engine's comparator.
    VAR_ADDON_ARRAY_CAP = 0x00C24944,
    FUN_ADDON_ARRAY_GROW = 0x004D85A0,
    // Name comparator (`__cdecl(void **a, void **b)`; resolves each name and
    // compares its Title case-insensitively) + the CRT qsort it feeds.
    FUN_ADDON_NAME_COMPARE = 0x005F7910,
    FUN_CRT_QSORT = 0x0040BE50,

    // Descriptor quantum calculator `uint __thiscall(void *desc, uint needed)`
    // — the engine's own grow-append uses it when a desc has no quantum yet.
    FUN_DESC_QUANTUM_CALC = 0x005D0040,

    // Loose-file index: every relative-path read resolves through a hash index
    // built ONCE at boot, so files created after boot are invisible until
    // restart. Re-run the boot root indexer per /reload to register new files
    // (dedup-safe). `void __cdecl IndexRoot(const char *basePath)` walks
    // basePath recursively, keys relative to basePath — the game-dir root
    // covers both Interface\AddOns and WTF\Account.
    FUN_VFS_INDEX_ROOT = 0x00424610,
    // `bool __cdecl IsDir(const char *path)` — boot uses it to fall back to
    // "." when the recorded base path isn't a directory.
    FUN_VFS_IS_DIR = 0x004281D0,
    VAR_VFS_BASE_PATH = 0x00B32360,   // recorded game dir (index keys are relative to it)
    VAR_VFS_INDEX_READY = 0x00B324A4, // u8 latch: loose-file index built

    // --- BAG_UPDATE_DELAYED (src/bag/UpdateDelayed.cpp) ---
    //
    // "BAG_UPDATE" is event ID 0x13A (slot 0x13A in the event-name array at
    // 0x00C24EB0). Verified by imm32-scanning .text for 0x13A and classifying
    // every hit: it fires from exactly five sites in the two functions below.
    // Post-hooking both covers every BAG_UPDATE; the drain then coalesces to
    // one BAG_UPDATE_DELAYED per frame. Both live in the 0x005Dxxxx item/
    // container region (4 and 2 callers) — quiet, no known DLL touches them.

    // Item -> container resolver: `void __cdecl(int guidLo, int guidHi)`.
    // Fires BAG_UPDATE(0/-2/-4) when the changed container is the player
    // (backpack/keyring/token bag), else scans the bag-GUID cache and fires
    // BAG_UPDATE(1..11). Four of the five fire sites.
    FUN_BAG_ITEM_TO_BAG = 0x005D7070,

    // Bag-slot diff loop: `void __cdecl(void)`. Diffs the player's bag
    // descriptor fields against the cache at 0x00C23540; fires BAG_CLOSED
    // for removed bags and BAG_UPDATE for added ones, re-registering the
    // per-bag content-change callbacks. The fifth fire site.
    FUN_BAG_SLOT_DIFF = 0x005D9960,

    // `u8`, nonzero exactly while the player is in the world: set by the
    // enter-world handler FUN_00528010 (which fires PLAYER_ENTERING_WORLD),
    // cleared by the leave-world handler FUN_00528C30 (PLAYER_LEAVING_WORLD),
    // so it is false at character select and across every loading screen.
    // The engine's own "is in game" getter FUN_008C6330 reads it.
    VAR_IN_WORLD = 0x00BD0792,

    // --- ExportInterfaceFiles console command (src/interface/Export.cpp) ---

    // MPQ listfile enumerator (by path prefix). `void __cdecl(const char
    // *pathPrefix, EnumCb cb, void *userParam)` where
    // `EnumCb = int __cdecl(const char *fullPath, void *userParam)` — the
    // archive-relative path arrives as a STACK arg; return 0 to STOP, nonzero
    // to continue. Walks each mounted archive's `(listfile)`, prefix-filtered.
    // This wrapper auto-picks the archive selector; the macro-icon builder
    // FUN_00565840 calls it with "Interface\\Icons\\".
    FUN_MPQ_ENUM_FILES = 0x00404A80,

    // Developer-console command registrar. `int __cdecl(const char *name,
    // Handler handler, int category, const char *help)` — Handler is
    // `int __cdecl(void *unused, const char *argsText)` (argsText = text after
    // the command name, "" when bare). Stores name/help BY POINTER (must be
    // static). Dedup-safe by name (returns 0 on repeat), so calling from a
    // re-firing hook is a no-op. `category` is cosmetic (help grouping);
    // 0 = "debug".
    FUN_CONSOLE_REGISTER_COMMAND = 0x00769100,
    CONSOLE_CATEGORY_DEBUG = 0,

    // Console output. `void __cdecl(const char *line, int category)` — appends
    // one line to the console buffer; no-ops safely when the console isn't
    // active. Pass 0 for the default category.
    FUN_CONSOLE_WRITE = 0x00765270,

    // --- Frame-object Lua wrapper (src/ui/FrameObject.cpp) ---
    //
    // `FrameScript_Object::ScriptRegister(this, name)` — `__thiscall`,
    // `this` = a `CFrameScriptObject *` (any scriptable engine object:
    // frame, region, chat bubble, ...). On the FIRST call for an object
    // (refcount at this+0x04 == 0) it builds the Lua wrapper table
    // `{ [0] = lightuserdata(this) }`, gives it the FrameScript metatable,
    // `luaL_ref`s it into the registry (LUA_REGISTRYINDEX = -10000),
    // writes the refkey to this+0x08, and increments the refcount. Used to
    // hand a Lua wrapper to C++-created frames that never went through Lua
    // `CreateFrame` (default nameplates, chat bubbles). Verified at
    // FUN_00819880: reads the global lua_State (VAR_LUA_STATE), calls
    // lua_createtable (0x0084E6E0), lua_pushlightuserdata (0x0084E500),
    // luaL_ref (0x0084F6C0); 50+ xrefs incl. Script_CreateFrame
    // (FUN_0081BB20), Script_CreateFont, Script_GetClickFrame — the shared
    // base-class builder, not a per-frame-type function.
    FUN_FRAMESCRIPT_OBJECT_SCRIPT_REGISTER = 0x00819880,

    // CFrameScriptObject base layout (verified in the base ctor
    // FUN_00819830 and every push site):
    //   +0x04  Lua refcount (init 0; ScriptRegister increments). Reading
    //          `== 0` is the "engine has never exposed this to Lua" probe.
    //   +0x08  Lua registry refkey (init 0xFFFFFFFE = -2 "unregistered"
    //          sentinel; ScriptRegister overwrites with a positive ref).
    //          Pushing the frame to Lua is
    //          `lua_rawgeti(L, LUA_REGISTRYINDEX, *(int*)(frame+0x08))`.
    OFF_COBJECT_LUA_REFCOUNT = 0x04,
    OFF_COBJECT_LUA_REGISTRY_REF = 0x08,

    // --- Chat bubbles (src/chatbubble/Info.cpp) ---
    //
    // Active in-world chat bubbles (`CGChatBubbleFrame`) live on an
    // intrusive Storm TSList. Head is the global pointer below; each
    // node's forward link is at node+0x2A0 (paired back-link at +0x29C).
    // End of list is the low-bit sentinel: stop when `(node & 1) != 0`
    // or node == 0. Verified across three walk functions (FUN_0056D050
    // per-frame update, FUN_0056CF80 teardown, FUN_0056C7A0) and the ctor
    // FUN_0056CAD0 (RTTI ".?AVCGChatBubbleFrame@@", textures
    // "Interface\\Tooltips\\ChatBubble-*"). `CGChatBubbleFrame` derives
    // from CFrameScriptObject (refcount/refkey at +0x04/+0x08), so each
    // node is pushed to Lua via `UI::FrameObject::Push`. The spoken-text
    // FontString child sits at node+0x2A4 (created with the bubble as its
    // parent, so it shows up in `bubble:GetRegions()`). 1.12's list used a
    // +0x318 link; 3.3.5 differs.
    VAR_CHAT_BUBBLE_LIST_HEAD = 0x00ACE600,
    OFF_CHAT_BUBBLE_NEXT_LINK = 0x2A0,

    // --- zlib (src/encoding/Compress.cpp) ---
    //
    // The client statically links zlib 1.2.2. All entry points are
    // __cdecl (caller-cleaned) in this build — NOT the __fastcall the
    // 1.12 sibling used. Verified at deflateInit2_ (0x00864DC0):
    // standard EBP frame, every arg read from [EBP+N] (no ECX/EDX),
    // `version[0] == '1'` + `stream_size == 0x38` gate returning
    // Z_VERSION_ERROR (-6), zlib windowBits sign/gzip handling, plain
    // RET epilogues; and deflateEnd (0x00863E50): validates the deflate
    // state magic (0x2A/0x71/0x29A) and frees via strm->zfree. inflate
    // (0x00865270) loads the zlib inflate error strings. Anchored on the
    // " deflate 1.2.2 Copyright ... " / z_errmsg literals.
    //
    // Signatures (all __cdecl):
    //   int deflateInit2_(strm, level, method, windowBits, memLevel,
    //                     strategy, version, stream_size)
    //   int deflate(strm, flush)
    //   int deflateEnd(strm)
    //   int inflateInit2_(strm, windowBits, version, stream_size)
    //   int inflate(strm, flush)
    //   int inflateEnd(strm)
    // Format selection rides on windowBits: Zlib = 15, Gzip = 31,
    // Deflate = -15 (raw), auto-detect = 47 (decode only; Zlib or Gzip).
    FUN_ZLIB_DEFLATE_INIT2 = 0x00864DC0,
    FUN_ZLIB_DEFLATE       = 0x00863A40,
    FUN_ZLIB_DEFLATE_END   = 0x00863E50,
    FUN_ZLIB_INFLATE_INIT2 = 0x00865080,
    FUN_ZLIB_INFLATE       = 0x00865270,
    FUN_ZLIB_INFLATE_END   = 0x00866660,
    // The version C-string ("1.2.2") the *Init2_ functions check the
    // passed version's first byte against (mismatch → Z_VERSION_ERROR).
    // Callers pass this same pointer through.
    VAR_ZLIB_VERSION_STRING = 0x00A3B558,
    ZLIB_STREAM_SIZE = 0x38, // sizeof(z_stream), 1.2.2 32-bit build

    // --- Virtual XML templates (src/xml/Templates.cpp) -------------------------
    //
    // The store that backs `inherits=` and `C_XMLUtil.*`. Every
    // `<Frame virtual="true">` (etc.) the XML loader parses is registered into a
    // Storm hash table by name via FUN_00813be0 (strings "Virtual object named
    // %s already exists" / "-- Added virtual frame %s"); the CreateFrame-from-XML
    // builder FUN_00812fa0 and the top-level file loader FUN_00813ee0 read it
    // back through the generic Storm lookup. So it's the authoritative template
    // list — fonts live in a separate registry (DAT_00D3F6E8, the frame-type
    // factory table) and are correctly excluded.
    //
    // Storm hash table object at VAR_XML_TEMPLATE_OBJECT. Its bucket-array
    // pointer is at +0x1C (VAR_XML_TEMPLATE_TABLE, 0 until first allocation) and
    // its bucket-count mask at +0x24 (VAR_XML_TEMPLATE_MASK, 0xFFFFFFFF until the
    // first template registers). Layout confirmed against the lookup FUN_0055F4D0.
    VAR_XML_TEMPLATE_OBJECT = 0x00D3F6C0,
    VAR_XML_TEMPLATE_TABLE = 0x00D3F6DC,
    VAR_XML_TEMPLATE_MASK = 0x00D3F6E4,
    // Bucket struct (0xC bytes): { linkOffset@+0x0, ?@+0x4, chainHead@+0x8 }.
    // A node's next pointer is at `*(node + linkOffset + 4)`; the tail sentinel
    // has its low bit set (chain ends on `(ptr & 1) != 0`). Identical to 1.12.
    XML_TEMPLATE_BUCKET_STRIDE = 0xC,
    OFF_XML_TEMPLATE_BUCKET_LINKOFF = 0x0,
    OFF_XML_TEMPLATE_BUCKET_HEAD = 0x8,
    // Registry node payload: hash @+0x0, template name string @+0x14, parsed
    // definition XML node @+0x18 (virtual flag @+0x1C, in-progress byte @+0x20 —
    // unused here). Confirmed in FUN_00813be0 (writes) and FUN_0055F4D0 (reads).
    OFF_XML_TEMPLATE_NODE_NAME = 0x14,
    OFF_XML_TEMPLATE_NODE_DEF = 0x18,
    // Parsed XML element node. Unlike 1.12 (tag @+0x8, child @+0x4, sibling
    // @+0x1C) the 3.3.5 node is larger: tag-name string ("Frame" / "Button" /
    // …) @+0x14 — this is the frame "type" — first child @+0x8, next sibling
    // @+0x34. Derived from the XML builder FUN_00812fa0 (`*(node+0x14)` type),
    // the <Size> parser FUN_00815740 (`*(node+8)` first child), and the file
    // loader FUN_00813ee0 (`*(node+0x34)` sibling walk).
    OFF_XML_NODE_TAG = 0x14,
    OFF_XML_NODE_CHILD = 0x8,
    OFF_XML_NODE_SIBLING = 0x34,
    // XML node's by-name attribute accessor. `__thiscall(node, name) -> value
    // string` (null if absent); attributes are 0x18-byte entries at node+0x28,
    // count at node+0x24 (name @entry+0x8, value @entry+0x14). FUN_00814730.
    FUN_XML_NODE_GET_ATTRIBUTE = 0x00814730,
    // Generic Storm hash lookup-by-name. `__thiscall(table, name) -> node`, 0 if
    // unregistered (hash + case-insensitive compare). Pass VAR_XML_TEMPLATE_OBJECT
    // for the template table; the returned node's def is at +0x18. FUN_0055F4D0.
    FUN_STORM_HASH_LOOKUP = 0x0055F4D0,
};
