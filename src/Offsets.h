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
    // to an "instance block" (the per-item data; itemID lives there).
    // ItemID at +0x0C inside that block. Matches 1.12's same offsets
    // (CGItem layout is older than the 1.12-vs-3.3.5 cut, so this
    // structure didn't shift).
    OFF_ITEM_INSTANCE_BLOCK = 0x08,
    OFF_INSTANCE_BLOCK_ITEM_ID = 0x0C,

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
