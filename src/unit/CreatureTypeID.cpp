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

// `UnitCreatureTypeID(unit)` — the unit's CreatureType.dbc ID (1 Beast,
// 3 Demon, 7 Humanoid, …), or nil. Ported from ClassicAPI.
//
// The stock UnitCreatureType (0x00611780) returns only the localized name of
// CGUnit_C::GetCreatureType's result; this returns the ID itself, from the
// same call — so a shapeshift form's type wins (Cat Form is a Beast), then the
// creature-cache record, then the player's race.

#include "Game.h"
#include "Offsets.h"
#include "unit/Resolve.h"

#include <cstdint>

namespace Unit::CreatureTypeID {

namespace {

// `int32_t __thiscall(const CGUnit_C *)`, no stack args.
using GetCreatureType_t = int32_t(__fastcall *)(const void *unit);

int __cdecl Script_UnitCreatureTypeID(void *L) {
    if (!Game::Lua::IsString(L, 1))
        return Game::Lua::Error(L, "Usage: UnitCreatureTypeID(\"unit\")");
    const void *unit = Unit::ResolveToken(Game::Lua::ToString(L, 1));
    const int32_t type =
        unit != nullptr ? reinterpret_cast<GetCreatureType_t>(Offsets::FUN_UNIT_GET_CREATURE_TYPE)(unit)
                        : 0;
    if (type <= 0)
        Game::Lua::PushNil(L);
    else
        Game::Lua::PushNumber(L, static_cast<double>(type));
    return 1;
}

void RegisterLuaFunctions() {
    Game::Lua::RegisterGlobalFunction("UnitCreatureTypeID", &Script_UnitCreatureTypeID);
}

const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace

} // namespace Unit::CreatureTypeID
