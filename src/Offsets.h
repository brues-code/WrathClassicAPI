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
    OFF_ITEMSTATS_INVENTORY_TYPE = 0x28,
    OFF_ITEMSTATS_ITEM_LEVEL = 0x34,
    OFF_ITEMSTATS_STACK_COUNT = 0x5C,
    OFF_ITEMSTATS_NAME = 0x1F4,

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

    // Caster GUID → unit-token resolver. `__cdecl(uint *guidPair)` →
    // "player" | "target" | "partyN" | "raidN" | "pet" | "partypetN" |
    // "raidpetN" | "arenaN" | "vehicle" | "focus" | "mouseover" |
    // "npc" | NULL. Writes party/raid/arena formats into a shared
    // static buffer at 0x00C25B7C — caller must consume (e.g. push to
    // Lua, which copies into Lua heap) before the next call.
    FUN_AURA_GUID_TO_TOKEN             = 0x0060B420,

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
};
