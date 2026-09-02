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

// `UnitDistanceSquared("unit") -> (distanceSquared, checkedPosition)` — the raw
// squared world distance from the player to `unit`. Squared because nearly every
// consumer compares against a threshold (`distSq <= range*range`) or ranks by
// nearest, neither of which needs the sqrt; that's why the modern API of the same
// name exposes only the squared form. Center-to-center (not reach-adjusted).
//
// Both units resolve through the engine's unit-token resolver, and each object's
// world position comes from its CGObject `GetPosition` virtual — the same path
// `CheckInteractDistance` walks. On a position miss (unresolvable token, or an
// object with no known position yet — e.g. a party member outside the client's
// sync range) it returns `(0, false)`; consumers must branch on the second
// return, since a real `0` (self, or exactly co-located units) is
// indistinguishable from the miss placeholder by value alone.

#include "Game.h"
#include "unit/Position.h"

#include <cstdint>

namespace Unit::DistanceSquared {

namespace {

int __cdecl Script_UnitDistanceSquared(void *L) {
    if (!Game::Lua::IsString(L, 1)) {
        Game::Lua::Error(L, "Usage: UnitDistanceSquared(\"unit\")");
        return 0;
    }
    const char *token = Game::Lua::ToString(L, 1);

    float unitPos[3] = {};
    float playerPos[3] = {};
    if (token == nullptr || !Unit::Position::ReadToken(token, unitPos) ||
        !Unit::Position::ReadToken("player", playerPos)) {
        Game::Lua::PushNumber(L, 0.0);
        Game::Lua::PushBool(L, false);
        return 2;
    }

    const float dx = unitPos[0] - playerPos[0];
    const float dy = unitPos[1] - playerPos[1];
    const float dz = unitPos[2] - playerPos[2];
    const float distSq = dx * dx + dy * dy + dz * dz;

    Game::Lua::PushNumber(L, static_cast<double>(distSq));
    Game::Lua::PushBool(L, true);
    return 2;
}

void RegisterLuaFunctions() {
    Game::Lua::RegisterGlobalFunction("UnitDistanceSquared", &Script_UnitDistanceSquared);
}

const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace

} // namespace Unit::DistanceSquared
