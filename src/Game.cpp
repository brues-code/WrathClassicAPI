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

#include "Game.h"
#include "MinHook.h"
#include "Offsets.h"

namespace Game {

namespace Lua {
// Each entry binds a typed function pointer in `Game::Lua::` to the
// corresponding raw VA in `Offsets`. The X-macro keeps the column-aligned
// list visually scannable and removes the cast boilerplate.
#define WRATHCLASSICAPI_LUA_BINDINGS(F)               \
    F(IsNumber,    lua_isnumber,    LUA_IS_NUMBER)    \
    F(IsString,    lua_isstring,    LUA_IS_STRING)    \
    F(ToNumber,    lua_tonumber,    LUA_TO_NUMBER)    \
    F(ToLString,   lua_tolstring,   LUA_TO_LSTRING)   \
    F(PushNumber,  lua_pushnumber,  LUA_PUSH_NUMBER)  \
    F(PushNil,     lua_pushnil,     LUA_PUSH_NIL)     \
    F(PushBoolean, lua_pushboolean, LUA_PUSH_BOOLEAN) \
    F(PushString,  lua_pushstring,  LUA_PUSH_STRING)  \
    F(PushValue,   lua_pushvalue,   LUA_PUSH_VALUE)   \
    F(PushCClosure,lua_pushcclosure,LUA_PUSH_CCLOSURE)\
    F(CreateTable, lua_createtable, LUA_CREATE_TABLE) \
    F(GetField,    lua_getfield,    LUA_GET_FIELD)    \
    F(SetField,    lua_setfield,    LUA_SET_FIELD)    \
    F(RawGet,      lua_rawget,      LUA_RAW_GET)      \
    F(RawSet,      lua_rawset,      LUA_RAW_SET)      \
    F(Insert,      lua_insert,      LUA_INSERT)       \
    F(Remove,      lua_remove,      LUA_REMOVE)       \
    F(GetTop,      lua_gettop,      LUA_GET_TOP)      \
    F(SetTop,      lua_settop,      LUA_SET_TOP)      \
    F(Type,        lua_type,        LUA_TYPE)         \
    F(ToUserdata,  lua_touserdata,  LUA_TO_USERDATA)  \
    F(ToBoolean,   lua_toboolean,   LUA_TO_BOOLEAN)   \
    F(Error,       luaL_error,      LUAL_ERROR)

#define WRATHCLASSICAPI_BIND_LUA(Name, Typedef, Offset) \
    const Typedef##_t Name = reinterpret_cast<Typedef##_t>(Offsets::Offset);
WRATHCLASSICAPI_LUA_BINDINGS(WRATHCLASSICAPI_BIND_LUA)
#undef WRATHCLASSICAPI_BIND_LUA
#undef WRATHCLASSICAPI_LUA_BINDINGS

namespace {
// FrameScript_RegisterFunction in 3.3.5 is cdecl(name, func) — the
// vanilla 1.12 build used fastcall, which is the load-bearing ABI
// difference. Reading: lua_pushcclosure(L, func, 0);
// lua_pushstring(L, name); lua_insert(L, -2);
// lua_settable(L, LUA_GLOBALSINDEX).
using FrameScript_RegisterFunction_t = void(__cdecl *)(const char *name, CFunction func);
} // namespace

void *State() {
    return *reinterpret_cast<void **>(static_cast<uintptr_t>(Offsets::VAR_LUA_STATE));
}

void RegisterGlobalFunction(const char *name, CFunction func) {
    auto fn = reinterpret_cast<FrameScript_RegisterFunction_t>(
        Offsets::FUN_FRAMESCRIPT_REGISTER_FUNCTION);
    fn(name, func);
}

// Registers `func` at `_G[tableName][methodName]`. If the namespace
// doesn't already exist, creates an empty table for it.
//
// Pattern borrowed from awesome_wotlk's `lua_openlibnameplates`
// (`C:\Git\awesome_wotlk\src\AwesomeWotlkLib\NamePlates.cpp`). The
// crucial bits are:
//   * Use `lua_setfield` instead of pushstring + insert + settable —
//     it's a single C call that handles the stack rearrangement
//     internally, so we don't have to get the dance right.
//   * Use `lua_setglobal` (= setfield on GLOBALSINDEX) to commit the
//     table once it's fully populated. The first iteration of this
//     helper tried to do `_G[name] = empty_table; t = _G[name]; t.foo
//     = closure` and crashed deep in `luaH_resize → numusearray` —
//     because what was *actually* on the stack was a `lua_State`
//     (lua_newthread, type tag 8), not a table. Always use
//     `lua_createtable` here, not whatever was at the old
//     `LUA_NEW_TABLE` offset.
void RegisterTableFunction(const char *tableName, const char *methodName, CFunction func) {
    void *L = State();
    if (L == nullptr)
        return;

    // Pull `_G[tableName]` if it exists; otherwise create it.
    GetField(L, GLOBALS_INDEX, tableName); // [_G[name]]
    if (Type(L, -1) != TYPE_TABLE) {
        SetTop(L, -2);                       // pop the non-table.        []
        CreateTable(L, 0, 1);                // fresh hash-sized table.   [tbl]
        PushValue(L, -1);                    // dup so SetGlobal can pop. [tbl, tbl]
        SetField(L, GLOBALS_INDEX, tableName); // _G[name] = tbl; pops.   [tbl]
    }

    PushCClosure(L, func, 0);                // [tbl, closure]
    SetField(L, -2, methodName);             // tbl[methodName] = closure; pops closure. [tbl]
    SetTop(L, -2);                           // pop tbl. []
}

} // namespace Lua

namespace {
ModuleAutoRegister *g_moduleHead = nullptr;
HookAutoRegister *g_hookHead = nullptr;
} // namespace

ModuleAutoRegister::ModuleAutoRegister(Fn f) : fn(f), next(g_moduleHead) {
    g_moduleHead = this;
}

void RunModuleRegistrations() {
    for (auto *node = g_moduleHead; node != nullptr; node = node->next)
        node->fn();
}

HookAutoRegister::HookAutoRegister(uintptr_t target, void *hook, void **original)
    : target(target), hook(hook), original(original), next(g_hookHead) {
    g_hookHead = this;
}

bool RunHookRegistrations() {
    for (auto *node = g_hookHead; node != nullptr; node = node->next) {
        auto *targetPtr = reinterpret_cast<LPVOID>(node->target);
        if (MH_CreateHook(targetPtr, node->hook,
                          reinterpret_cast<LPVOID *>(node->original)) != MH_OK)
            return false;
        if (MH_EnableHook(targetPtr) != MH_OK)
            return false;
    }
    return true;
}

} // namespace Game
