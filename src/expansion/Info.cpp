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

#include <cstdint>

// Backport of the modern "Classic expansion level" API surface:
//
//   number GetClassicExpansionLevel()
//   bool   ClassicExpansionAtLeast(level)
//   bool   ClassicExpansionAtMost(level)
//
// plus the LE_EXPANSION_* and LE_EXPANSION_LEVEL_CURRENT globals.
//
// On a given Classic flavour these return the fixed integer for that
// flavour, independent of `GetAccountExpansionLevel`. For this WotLK
// 3.3.5a build, that integer is 2 (LE_EXPANSION_WRATH_OF_THE_LICH_KING),
// matching `Enum.ExpansionLevel.Northrend`.
//
// Reference: https://warcraft.wiki.gg/wiki/API_GetClassicExpansionLevel
namespace Expansion::Info {

namespace {

constexpr int LE_EXPANSION_CLASSIC                 = 0;
constexpr int LE_EXPANSION_BURNING_CRUSADE         = 1;
constexpr int LE_EXPANSION_WRATH_OF_THE_LICH_KING  = 2;
constexpr int LE_EXPANSION_CATACLYSM               = 3;
constexpr int LE_EXPANSION_MISTS_OF_PANDARIA       = 4;
constexpr int LE_EXPANSION_WARLORDS_OF_DRAENOR     = 5;
constexpr int LE_EXPANSION_LEGION                  = 6;
constexpr int LE_EXPANSION_BATTLE_FOR_AZEROTH      = 7;
constexpr int LE_EXPANSION_SHADOWLANDS             = 8;
constexpr int LE_EXPANSION_DRAGONFLIGHT            = 9;
constexpr int LE_EXPANSION_WAR_WITHIN              = 10;
constexpr int LE_EXPANSION_MIDNIGHT                = 11;

// The "current" expansion for this binary. The DLL ships against
// WotLK 3.3.5a, so this is fixed at compile time — there's no
// runtime detection. Update if the DLL is ever ported to a different
// Classic flavour.
constexpr int LE_EXPANSION_LEVEL_CURRENT = LE_EXPANSION_WRATH_OF_THE_LICH_KING;

int __cdecl Script_GetClassicExpansionLevel(void *L) {
    Game::Lua::PushNumber(L, LE_EXPANSION_LEVEL_CURRENT);
    return 1;
}

int __cdecl Script_ClassicExpansionAtLeast(void *L) {
    if (!Game::Lua::IsNumber(L, 1))
        return Game::Lua::Error(L, "Usage: ClassicExpansionAtLeast(expansionLevel)");
    int level = static_cast<int>(Game::Lua::ToNumber(L, 1));
    Game::Lua::PushBool(L, LE_EXPANSION_LEVEL_CURRENT >= level);
    return 1;
}

int __cdecl Script_ClassicExpansionAtMost(void *L) {
    if (!Game::Lua::IsNumber(L, 1))
        return Game::Lua::Error(L, "Usage: ClassicExpansionAtMost(expansionLevel)");
    int level = static_cast<int>(Game::Lua::ToNumber(L, 1));
    Game::Lua::PushBool(L, LE_EXPANSION_LEVEL_CURRENT <= level);
    return 1;
}

// Sets `_G[name] = value` via the Lua C API. Used to install the
// LE_EXPANSION_* number globals at registration time.
void SetGlobalNumber(void *L, const char *name, int value) {
    Game::Lua::PushNumber(L, value);
    Game::Lua::SetGlobal(L, name);
}

void RegisterLuaFunctions() {
    Game::Lua::RegisterGlobalFunction("GetClassicExpansionLevel",
                                      &Script_GetClassicExpansionLevel);
    Game::Lua::RegisterGlobalFunction("ClassicExpansionAtLeast",
                                      &Script_ClassicExpansionAtLeast);
    Game::Lua::RegisterGlobalFunction("ClassicExpansionAtMost",
                                      &Script_ClassicExpansionAtMost);

    void *L = Game::Lua::State();
    if (L == nullptr)
        return;
    SetGlobalNumber(L, "LE_EXPANSION_LEVEL_CURRENT",        LE_EXPANSION_LEVEL_CURRENT);
    SetGlobalNumber(L, "LE_EXPANSION_CLASSIC",              LE_EXPANSION_CLASSIC);
    SetGlobalNumber(L, "LE_EXPANSION_BURNING_CRUSADE",      LE_EXPANSION_BURNING_CRUSADE);
    SetGlobalNumber(L, "LE_EXPANSION_WRATH_OF_THE_LICH_KING", LE_EXPANSION_WRATH_OF_THE_LICH_KING);
    SetGlobalNumber(L, "LE_EXPANSION_CATACLYSM",            LE_EXPANSION_CATACLYSM);
    SetGlobalNumber(L, "LE_EXPANSION_MISTS_OF_PANDARIA",    LE_EXPANSION_MISTS_OF_PANDARIA);
    SetGlobalNumber(L, "LE_EXPANSION_WARLORDS_OF_DRAENOR",  LE_EXPANSION_WARLORDS_OF_DRAENOR);
    SetGlobalNumber(L, "LE_EXPANSION_LEGION",               LE_EXPANSION_LEGION);
    SetGlobalNumber(L, "LE_EXPANSION_BATTLE_FOR_AZEROTH",   LE_EXPANSION_BATTLE_FOR_AZEROTH);
    SetGlobalNumber(L, "LE_EXPANSION_SHADOWLANDS",          LE_EXPANSION_SHADOWLANDS);
    SetGlobalNumber(L, "LE_EXPANSION_DRAGONFLIGHT",         LE_EXPANSION_DRAGONFLIGHT);
    SetGlobalNumber(L, "LE_EXPANSION_WAR_WITHIN",           LE_EXPANSION_WAR_WITHIN);
    SetGlobalNumber(L, "LE_EXPANSION_MIDNIGHT",             LE_EXPANSION_MIDNIGHT);
}

const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace
} // namespace Expansion::Info
