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

// The C_CVar namespace's core: GetCVar, SetCVar, GetCVarDefault and
// RegisterCVar under the namespace, plus GetCVarBool, GetCVarInfo,
// DoesCVarExist and AreCVarsLoaded. Ported from ClassicAPI's src/cvar/.
//
// GetCVar, SetCVar, GetCVarDefault and RegisterCVar are the client's own Lua
// functions registered a second time under C_CVar, so the namespaced and
// global forms behave identically. Each Lua state gets its own state's
// functions; the login screen has no RegisterCVar.
//
// GetCVarBool and GetCVarInfo are native because the modern returns differ:
// real booleans, and seven GetCVarInfo returns instead of four.

#include "Game.h"
#include "Offsets.h"
#include "cvar/CVar.h"

#include <cstdint>

namespace CVar::Info {

namespace {

using StringToBool_t = int(__cdecl *)(const char *value);

Game::Lua::CFunction Engine(uintptr_t address) {
    return reinterpret_cast<Game::Lua::CFunction>(address);
}

// C_CVar.GetCVarBool(name) — the value as a boolean, or nil for a name that
// isn't a cvar. Same coercion as the global GetCVarBool.
int __cdecl Script_GetCVarBool(void *L) {
    if (!Game::Lua::IsString(L, 1))
        return Game::Lua::Error(L, "Usage: C_CVar.GetCVarBool(\"cvar\")");
    CVar::Handle cvar = CVar::Find(Game::Lua::ToString(L, 1));
    if (cvar == nullptr) {
        Game::Lua::PushNil(L);
        return 1;
    }
    const auto toBool = reinterpret_cast<StringToBool_t>(Offsets::FUN_STRING_TO_BOOL);
    Game::Lua::PushBool(L, toBool(CVar::Value(cvar)) != 0);
    return 1;
}

// C_CVar.GetCVarInfo(name) — value, defaultValue, isStoredServerAccount,
// isStoredServerCharacter, isLockedFromUser, isSecure, isReadOnly; nil for a
// name that isn't a cvar. Every flag but isLockedFromUser is the client's
// own; no flag locks a cvar from the user.
int __cdecl Script_GetCVarInfo(void *L) {
    if (!Game::Lua::IsString(L, 1))
        return Game::Lua::Error(L, "Usage: C_CVar.GetCVarInfo(\"cvar\")");
    CVar::Handle cvar = CVar::Find(Game::Lua::ToString(L, 1));
    if (cvar == nullptr) {
        Game::Lua::PushNil(L);
        return 1;
    }
    const uint32_t flags = CVar::Flags(cvar);
    Game::Lua::PushString(L, CVar::Value(cvar));
    Game::Lua::PushString(L, CVar::Default(cvar));
    Game::Lua::PushBool(L, (flags & Offsets::CVAR_FLAG_ACCOUNT) != 0);
    Game::Lua::PushBool(L, (flags & Offsets::CVAR_FLAG_CHARACTER) != 0);
    Game::Lua::PushBool(L, false);
    Game::Lua::PushBool(L, (flags & Offsets::CVAR_FLAG_PROTECTED) != 0);
    Game::Lua::PushBool(L, CVar::IsReadOnly(cvar));
    return 7;
}

// C_CVar.DoesCVarExist(name) — a ClassicAPI extension: whether GetCVar and
// SetCVar can see this name. False for anything that isn't a string.
int __cdecl Script_DoesCVarExist(void *L) {
    Game::Lua::PushBool(L, Game::Lua::IsString(L, 1) &&
                               CVar::Find(Game::Lua::ToString(L, 1)) != nullptr);
    return 1;
}

// C_CVar.AreCVarsLoaded() — the cvar system's own started flag. Always true
// by the time Lua runs: cvars load from Config.wtf at startup.
int __cdecl Script_AreCVarsLoaded(void *L) {
    Game::Lua::PushBool(L, *reinterpret_cast<const uint8_t *>(
                               static_cast<uintptr_t>(Offsets::VAR_CVAR_INITIALIZED)) != 0);
    return 1;
}

void RegisterNative() {
    Game::Lua::RegisterTableFunction("C_CVar", "GetCVarBool", &Script_GetCVarBool);
    Game::Lua::RegisterTableFunction("C_CVar", "GetCVarInfo", &Script_GetCVarInfo);
    Game::Lua::RegisterTableFunction("C_CVar", "DoesCVarExist", &Script_DoesCVarExist);
    Game::Lua::RegisterTableFunction("C_CVar", "AreCVarsLoaded", &Script_AreCVarsLoaded);
}

void RegisterLuaFunctions() {
    RegisterNative();
    Game::Lua::RegisterTableFunction("C_CVar", "GetCVar", Engine(Offsets::FUN_SCRIPT_GET_CVAR));
    Game::Lua::RegisterTableFunction("C_CVar", "SetCVar", Engine(Offsets::FUN_SCRIPT_SET_CVAR));
    Game::Lua::RegisterTableFunction("C_CVar", "GetCVarDefault",
                                     Engine(Offsets::FUN_SCRIPT_GET_CVAR_DEFAULT));
    Game::Lua::RegisterTableFunction("C_CVar", "RegisterCVar",
                                     Engine(Offsets::FUN_SCRIPT_REGISTER_CVAR));
}

void RegisterGlueFunctions() {
    RegisterNative();
    Game::Lua::RegisterTableFunction("C_CVar", "GetCVar",
                                     Engine(Offsets::FUN_GLUE_SCRIPT_GET_CVAR));
    Game::Lua::RegisterTableFunction("C_CVar", "SetCVar",
                                     Engine(Offsets::FUN_GLUE_SCRIPT_SET_CVAR));
    Game::Lua::RegisterTableFunction("C_CVar", "GetCVarDefault",
                                     Engine(Offsets::FUN_GLUE_SCRIPT_GET_CVAR_DEFAULT));
}

const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};
const Game::GlueModuleAutoRegister _glueAutoreg{&RegisterGlueFunctions};

} // namespace

} // namespace CVar::Info
