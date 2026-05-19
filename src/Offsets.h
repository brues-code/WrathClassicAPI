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

    // GameUI master bootstrap. Calls in order: `FUN_00819BB0` (Lua
    // state init) → `FUN_005120E0` (register all engine Lua C
    // functions) → `FUN_0081B5F0` (FrameScript_FillEvents — populate
    // the engine's event table) → assorted other init.
    //
    // Hooked POST so our `ModuleAutoRegister` chain runs after BOTH
    // engine globals AND events are fully registered — Event::Custom
    // appends custom names via FrameScript_FillEvents from inside
    // module registration, which requires the engine's events to
    // already be in the output array.
    //
    // Importantly only runs on **first boot**. /reload uses a
    // different path (FUN_00528F00 → FUN_00512280) — see
    // [[feedback-two-load-script-paths]] in memory. Hooking that
    // path is a follow-up.
    FUN_GAME_UI_INIT = 0x0052A980,

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
    FUN_FRAMESCRIPT_FILL_EVENTS = 0x0081B5F0,

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
