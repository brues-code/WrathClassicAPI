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
using lua_pushvalue_t = void(__cdecl *)(void *L, int idx);
using lua_pushcclosure_t = void(__cdecl *)(void *L, CFunction fn, int n);
using lua_createtable_t = void(__cdecl *)(void *L, int narr, int nrec);
using lua_getfield_t = void(__cdecl *)(void *L, int idx, const char *name);
using lua_setfield_t = void(__cdecl *)(void *L, int idx, const char *name);
using lua_rawget_t = void(__cdecl *)(void *L, int idx);
using lua_rawset_t = void(__cdecl *)(void *L, int idx);
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
extern const lua_pushvalue_t PushValue;
extern const lua_pushcclosure_t PushCClosure;
extern const lua_createtable_t CreateTable;
// Helper: lua_newtable(L) is lua_createtable(L, 0, 0) — pushes a fresh empty table.
inline void NewTable(void *L) { CreateTable(L, 0, 0); }
extern const lua_getfield_t GetField;
extern const lua_setfield_t SetField;
extern const lua_rawget_t RawGet;
extern const lua_rawset_t RawSet;
// Helper: lua_setglobal(L, name) is lua_setfield(L, LUA_GLOBALSINDEX, name).
inline void SetGlobal(void *L, const char *name) { SetField(L, GLOBALS_INDEX, name); }
inline void GetGlobal(void *L, const char *name) { GetField(L, GLOBALS_INDEX, name); }
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

// lua_tostring is implemented in 5.1 as `lua_tolstring(L, idx, NULL)`.
// Wrap it so callers can write `Game::Lua::ToString(L, n)`.
inline const char *ToString(void *L, int idx) { return ToLString(L, idx, nullptr); }

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

// Declarative MinHook registration. Each feature module declares a
// file-scope `static const Game::HookAutoRegister _hookreg{target,
// &hook_fn, reinterpret_cast<void**>(&original_fn)};` and
// `RunHookRegistrations` (called from DllMain after `MH_Initialize`)
// walks the list installing each one. Returns false on the first
// `MH_CreateHook` or `MH_EnableHook` failure so DllMain can fail-fast.
//
// Use only for feature hooks; the core GameUI hook in DllMain itself
// is interleaved with module-registration logic and stays there.
struct HookAutoRegister {
    HookAutoRegister(uintptr_t target, void *hook, void **original);
    uintptr_t target;
    void *hook;
    void **original;
    HookAutoRegister *next;
};

bool RunHookRegistrations();

} // namespace Game
