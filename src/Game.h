// This file is part of WrathClassicAPI.
//
// WrathClassicAPI is free software: you can redistribute it and/or modify it under the terms
// of the GNU General Public License as published by the Free Software Foundation, either
// version 3 of the License, or (at your option) any later version.
//
// WrathClassicAPI is distributed in the hope that it will be useful, but WITHOUT ANY
// WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE. See the GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License along with
// WrathClassicAPI. If not, see <https://www.gnu.org/licenses/>.

#pragma once

#include <cstdint>

namespace Game {

namespace Lua {

// 3.3.5 keeps Lua's public `lua_CFunction` typedef — `int (L)` —
// but the engine's calling convention for these C functions is
// __cdecl (vanilla 1.12 used __fastcall). The lua_pushcclosure
// helper in this build pushes the function pointer as a raw cdecl
// callable; declaring our scripts as `int __cdecl(L)` matches.
using CFunction = int(__cdecl *)(void *L);

// Lua 5.1 pseudo-index for the globals table. Vanilla 1.12 was
// -10001 (Lua 5.0); the WotLK upgrade to 5.1 shifted it to -10002,
// visible in the registrar at FUN_FRAMESCRIPT_REGISTER_FUNCTION as
// the `push 0xFFFFD8EE` immediately before the `lua_settable` call.
constexpr int GLOBALS_INDEX = -10002;
// Lua 5.1 registry pseudo-index (state-local protected table).
constexpr int REGISTRY_INDEX = -10000;
// LUA_UPVALUEINDEX(i) — pseudo-index for accessing the i-th upvalue
// of a C closure. Lua 5.1 layout: `LUA_GLOBALSINDEX - i`.
constexpr int UpvalueIndex(int i) { return GLOBALS_INDEX - i; }

// `lua_call` / `lua_pcall` nresults sentinel meaning "all".
constexpr int MULTRET = -1;

// Type tag values returned by `Type()` (lua_type). Matches Lua 5.1
// layout (TFUNCTION moved up to make room for TLIGHTUSERDATA at 2).
constexpr int TYPE_NONE = -1;
constexpr int TYPE_NIL = 0;
constexpr int TYPE_BOOLEAN = 1;
constexpr int TYPE_LIGHTUSERDATA = 2;
constexpr int TYPE_NUMBER = 3;
constexpr int TYPE_STRING = 4;
constexpr int TYPE_TABLE = 5;
constexpr int TYPE_FUNCTION = 6;
constexpr int TYPE_USERDATA = 7;
constexpr int TYPE_THREAD = 8;

// All Lua C API entry points in 3.3.5 are __cdecl (args on the
// stack, caller cleans up). Same Lua 5.1 ABI as upstream — the
// types here mirror the signatures from lua.h.
using lua_isnumber_t = int(__cdecl *)(void *L, int idx);
using lua_isstring_t = int(__cdecl *)(void *L, int idx);
using lua_tonumber_t = double(__cdecl *)(void *L, int idx);
using lua_tolstring_t = const char *(__cdecl *)(void *L, int idx, unsigned int *len);
using lua_pushnumber_t = void(__cdecl *)(void *L, double n);
using lua_pushnil_t = void(__cdecl *)(void *L);
using lua_pushboolean_t = void(__cdecl *)(void *L, int b);
using lua_pushstring_t = void(__cdecl *)(void *L, const char *s);
using lua_pushlstring_t = void(__cdecl *)(void *L, const char *s, unsigned int len);
using lua_pushvalue_t = void(__cdecl *)(void *L, int idx);
using lua_pushcclosure_t = void(__cdecl *)(void *L, CFunction fn, int n);
using lua_createtable_t = void(__cdecl *)(void *L, int narr, int nrec);
using lua_getfield_t = void(__cdecl *)(void *L, int idx, const char *name);
using lua_setfield_t = void(__cdecl *)(void *L, int idx, const char *name);
using lua_rawget_t = void(__cdecl *)(void *L, int idx);
using lua_rawset_t = void(__cdecl *)(void *L, int idx);
using lua_settable_t = void(__cdecl *)(void *L, int idx);
using lua_insert_t = void(__cdecl *)(void *L, int idx);
using lua_remove_t = void(__cdecl *)(void *L, int idx);
using lua_gettop_t = int(__cdecl *)(void *L);
using lua_settop_t = void(__cdecl *)(void *L, int idx);
using lua_type_t = int(__cdecl *)(void *L, int idx);
using lua_touserdata_t = void *(__cdecl *)(void *L, int idx);
using lua_toboolean_t = int(__cdecl *)(void *L, int idx);
// luaL_error is variadic — the typedef stops at the format string;
// callers supply additional args at the call site (cdecl pushes them).
using luaL_error_t = int(__cdecl *)(void *L, const char *fmt, ...);
using lua_pcall_t = int(__cdecl *)(void *L, int nargs, int nresults, int errfunc);
using lua_rawseti_t = void(__cdecl *)(void *L, int idx, int n);
using lua_next_t = int(__cdecl *)(void *L, int idx);

extern const lua_isnumber_t IsNumber;
extern const lua_isstring_t IsString;
extern const lua_tonumber_t ToNumber;
extern const lua_tolstring_t ToLString;
extern const lua_pushnumber_t PushNumber;
extern const lua_pushnil_t PushNil;
extern const lua_pushboolean_t PushBoolean;
// Convenience overload — takes a real `bool` so callers don't have to
// `static_cast<int>(...)` at every site. The engine's lua_pushboolean
// takes int (any non-zero = true), so this just funnels through.
inline void PushBool(void *L, bool b) { PushBoolean(L, b ? 1 : 0); }
extern const lua_pushstring_t PushString;
// Binary-safe string push — keeps embedded NULs, does not stop at the
// first one like PushString. Use for any output that can contain raw
// bytes (hex/base64/compressed/serialized blobs).
extern const lua_pushlstring_t PushLString;
extern const lua_pushvalue_t PushValue;
extern const lua_pushcclosure_t PushCClosure;
extern const lua_createtable_t CreateTable;
// Helper: lua_newtable(L) is lua_createtable(L, 0, 0) — pushes a fresh empty table.
inline void NewTable(void *L) { CreateTable(L, 0, 0); }
extern const lua_getfield_t GetField;
extern const lua_setfield_t SetField;
extern const lua_rawget_t RawGet;
extern const lua_rawset_t RawSet;
// Metamethod-aware table set (lua_settable): `t[k] = v` honoring __newindex,
// unlike RawSet. Mixin uses it to preserve `object[k] = v` copy semantics.
extern const lua_settable_t SetTable;
// Helper: lua_setglobal(L, name) is lua_setfield(L, LUA_GLOBALSINDEX, name).
inline void SetGlobal(void *L, const char *name) { SetField(L, GLOBALS_INDEX, name); }
inline void GetGlobal(void *L, const char *name) { GetField(L, GLOBALS_INDEX, name); }
// Convenience: `_G[name] = value` for a numeric global.
inline void SetGlobalNumber(void *L, const char *name, double value) {
    PushNumber(L, value);
    SetGlobal(L, name);
}
// Helpers: `table[name] = value` for the table currently on top of
// the stack. PushNumber/PushString puts the value at -1 (table shifts
// to -2); SetField on -2 sets table[name] = top and pops the value,
// leaving the table on top.
inline void SetFieldNumber(void *L, const char *name, double value) {
    PushNumber(L, value);
    SetField(L, -2, name);
}
inline void SetFieldString(void *L, const char *name, const char *value) {
    PushString(L, value);
    SetField(L, -2, name);
}
inline void SetFieldBool(void *L, const char *name, bool value) {
    PushBoolean(L, value ? 1 : 0);
    SetField(L, -2, name);
}
extern const lua_insert_t Insert;
extern const lua_remove_t Remove;
extern const lua_gettop_t GetTop;
extern const lua_settop_t SetTop;
extern const lua_type_t Type;
extern const lua_touserdata_t ToUserdata;
extern const lua_toboolean_t ToBoolean;
extern const luaL_error_t Error;
extern const lua_pcall_t PCall;
extern const lua_rawseti_t RawSetI;
// lua_next(L, idx): pops a key from the stack, pushes the next key+value
// pair from the table at `idx`, and returns non-zero — or pushes nothing
// and returns 0 at the end of the table. The standard table-iteration
// primitive (JSON/CBOR serialization walk arbitrary Lua tables with it).
extern const lua_next_t Next;

// lua_tostring is implemented in 5.1 as `lua_tolstring(L, idx, NULL)`.
// Wrap it so callers can write `Game::Lua::ToString(L, n)`.
inline const char *ToString(void *L, int idx) { return ToLString(L, idx, nullptr); }

// Byte length of the string at `idx` (binary-safe — the string's stored
// length, not a strlen to the first NUL). Implemented via lua_tolstring's
// out-length. Callers should already have validated the value is a string
// (lua_tolstring converts a number in place, which is a benign no-op here
// but not intended for other types).
inline unsigned int StrLen(void *L, int idx) {
    unsigned int len = 0;
    ToLString(L, idx, &len);
    return len;
}

// Convenience: lua_rawgeti emulation via PushNumber + RawGet. Pushes
// `T[i]` where T is at absolute stack index `idx`. Caller is
// responsible for popping the result when done.
inline void RawGetI(void *L, int idx, int i) {
    PushNumber(L, static_cast<double>(i));
    RawGet(L, idx);
}

// Returns the global `lua_State *` (read on demand from the engine's global).
// Callable outside a Lua callback, e.g. during LoadScriptFunctions setup.
void *State();

// Registers a single global Lua function (e.g. `GetSpellInfo`). The function
// must use the engine's Lua C-function ABI: `int __cdecl(void *L)`.
void RegisterGlobalFunction(const char *name, CFunction func);

// Registers `func` at `_G[tableName][methodName]`, creating the namespace
// table if it doesn't already exist. This is how modern WoW C_*-style APIs
// are bound — the engine has no built-in support for table-bound Lua
// functions, so we manipulate the globals table directly via the Lua C API.
void RegisterTableFunction(const char *tableName, const char *methodName,
                           CFunction func);

// Key/value pair for `RegisterIntegerEnum`. `key` becomes a field name
// (PascalCase, matching Blizzard's `Enum.*` naming) and `value` is the
// integer the enum field maps to.
struct EnumIntegerEntry {
    const char *key;
    int value;
};

// Registers `_G[parent][sub] = { key = value, ... }`, creating the
// `_G[parent]` table if it doesn't already exist. Used for Blizzard-style
// `Enum.Base64Variant = { Standard = 0, UrlSafe = 1 }` shapes.
void RegisterIntegerEnum(const char *parent, const char *sub,
                         const EnumIntegerEntry *entries, int count);

} // namespace Lua

// Self-registration for API modules. Each module .cpp declares a file-scope
// `static const Game::ModuleAutoRegister _r{&RegisterLuaFunctions};`, which
// chains itself onto a global list at DLL-load time. `RunModuleRegistrations`
// is called once from the GameUI post-hook to fire them all, so DllMain.cpp
// doesn't need to know the modules exist.
//
// Order is unspecified (LIFO of static-init order across TUs). Modules must
// not depend on each other's registration side effects.
struct ModuleAutoRegister {
    using Fn = void (*)();
    explicit ModuleAutoRegister(Fn fn);
    Fn fn;
    ModuleAutoRegister *next;
};

void RunModuleRegistrations();

// Login-screen / glue registration. Callbacks fire once from the `Load()`
// export (LichCore calls it at `CGlueMgr::Initialize` — the login-screen init,
// after GlueXML has loaded — on the main thread), so anything registered here
// exists at the login screen and persists for the session. Use for
// process-global registrations that must be available BEFORE entering the world
// — e.g. developer-console commands, which the in-game `ModuleAutoRegister`
// bootstrap (in-world only) registers too late for the login console. Declare a
// file-scope `static const Game::GlueModuleAutoRegister _g{&Register};`.
//
// Only fires on the LichCore `Load()` path; the no-LichCore fallback worker
// installs hooks but has no login-screen-timed signal, so it skips these.
struct GlueModuleAutoRegister {
    using Fn = void (*)();
    explicit GlueModuleAutoRegister(Fn fn);
    Fn fn;
    GlueModuleAutoRegister *next;
};

void RunGlueModuleRegistrations();

// A loader-provided detour installer. The core records its feature hooks via
// HookAutoRegister and its lifecycle hooks via InstallCoreHooks, then installs
// both through this interface — so no core translation unit references a specific
// hook engine, and the same core sources build into either front-end. The
// front-ends supply the implementation:
//   * the LichLoader-injected DLL backs it with MinHook (create + queue-enable,
//     then one MH_ApplyQueued for the whole batch);
//   * a WXL extension backs it with WXL's chained hook registry, which also lets
//     our detours coexist with other extensions hooking the same engine seam.
// `Install` patches `target`, routes it to `detour`, and writes the callable
// trampoline to `*original`. Returns false on failure so the installer can
// fail-fast. Any batching/commit is the front-end's concern, performed after all
// Install calls have returned.
struct IHookHost {
    virtual bool Install(uintptr_t target, void *detour, void **original) = 0;

protected:
    ~IHookHost() = default;
};

// Installs the engine hooks that drive the module lifecycle, through `host`:
//   * in-game Lua-state ready    -> RunModuleRegistrations()  (bootstrap signal)
//   * in-game Lua-state teardown -> RunReloadCleanups()       (on /reload, /logout)
// and performs the data write that admits our DLL-resident C closures past the
// engine's "function pointer must live in Wow.exe's .text" gate. Both front-ends
// call this with their own IHookHost, so the bootstrap wiring — detours, offsets,
// and the gate write — lives in exactly one place; only the host differs.
// Returns false on the first Install failure.
bool InstallCoreHooks(IHookHost &host);

// Declarative hook registration. Each feature module declares a file-scope
// `static const Game::HookAutoRegister _hookreg{target, &hook_fn,
// reinterpret_cast<void**>(&original_fn)};`, which chains itself onto a global
// list at DLL-load time. `RunHookRegistrations(host)` walks the list and installs
// each hook through `host`, returning false on the first Install failure so the
// installer can fail-fast. The front-end applies/commits the batch afterwards.
//
// A target may carry only ONE detour — two modules registering the same address
// is rejected (see RunHookRegistrations). When two features need the same seam,
// one module owns the hook and calls them in an explicit order; see
// src/addons/TocExecutor.cpp.
struct HookAutoRegister {
    HookAutoRegister(uintptr_t target, void *hook, void **original);
    uintptr_t target;
    void *hook;
    void **original;
    HookAutoRegister *next;
};

bool RunHookRegistrations(IHookHost &host);

// Self-registration for per-/reload state cleanup. A module that keeps
// file-static state which goes STALE across a game /reload or /logout
// declares a file-scope
// `static const Game::ReloadAutoRegister _reload{&PrepareForReload};`
// right after its `void PrepareForReload()`. `RunReloadCleanups()` — called
// from the FrameScript-shutdown hook in DllMain BEFORE the engine destroys
// the in-game Lua state — fires them all, so DllMain doesn't hand-maintain
// the list and a new module can't forget to wire itself in.
//
// State is "reload-fragile" when it survives in C++ but its meaning does not:
//   - Lua registry refs / handler cells (the Lua state reset invalidates them);
//   - maps/sets keyed by a frame/object POINTER (the allocator recycles
//     addresses, so an old entry aliases an unrelated new object);
//   - cached event-slot indices (rebuilt when the event table is rebuilt).
// The callback runs while the OLD state is still valid, so it can release
// Lua refs. A pure-C++ cache keyed by spellID/itemID is NOT fragile and must
// not register.
//
// Same lifetime + ordering rules as ModuleAutoRegister: static-init chaining,
// undefined cross-TU order — fine, because each callback clears only its OWN
// state.
struct ReloadAutoRegister {
    using Fn = void (*)();
    explicit ReloadAutoRegister(Fn fn);
    Fn fn;
    ReloadAutoRegister *next;
};

void RunReloadCleanups();

} // namespace Game
