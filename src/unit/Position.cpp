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

// `UnitPosition("unit") -> (positionX, positionY, positionZ, mapID)` — the modern
// world-position accessor. The world position of any visible unit lives on its
// CGObject; we read it through the shared `Unit::Position` helper (the object's
// GetPosition virtual). The engine's C3Vector is {x = north, y = west, z = up},
// which is exactly the order modern UnitPosition returns — so we push x, y, z
// straight through with no swap.
//
// `mapID` is the currently-loaded Map.dbc id (VAR_CURRENT_MAP_ID) — every visible
// unit shares the player's instance, so the player's map id is the unit's.
//
// Returns nothing (nil) when the unit has no known position: an unresolvable
// token, or a unit outside the client's sync range. Unlike retail there's no
// party/raid restriction — any *visible* unit reads back.

#include "Game.h"
#include "Offsets.h"
#include "unit/Position.h"

#include <cstdint>

namespace Unit::PositionApi {

namespace {

int __cdecl Script_UnitPosition(void *L) {
    if (!Game::Lua::IsString(L, 1)) {
        Game::Lua::Error(L, "Usage: UnitPosition(\"unit\")");
        return 0;
    }
    const char *token = Game::Lua::ToString(L, 1);

    float pos[3] = {};
    if (token == nullptr || !Unit::Position::ReadToken(token, pos))
        return 0; // nil — no known position

    Game::Lua::PushNumber(L, static_cast<double>(pos[0])); // positionX (north)
    Game::Lua::PushNumber(L, static_cast<double>(pos[1])); // positionY (west)
    Game::Lua::PushNumber(L, static_cast<double>(pos[2])); // positionZ (up)
    const int mapID = *reinterpret_cast<const int *>(
        static_cast<uintptr_t>(Offsets::VAR_CURRENT_MAP_ID));
    Game::Lua::PushNumber(L, static_cast<double>(mapID));
    return 4;
}

void RegisterLuaFunctions() {
    Game::Lua::RegisterGlobalFunction("UnitPosition", &Script_UnitPosition);
}

const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace

} // namespace Unit::PositionApi
