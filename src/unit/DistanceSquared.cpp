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
#include "Offsets.h"

#include <cstdint>

namespace Unit::DistanceSquared {

namespace {

using ResolveUnitToken_t = void *(__cdecl *)(const char *token);
using GetPosition_t = float *(__thiscall *)(void *self, float out[3]);

// Resolves `token` to a unit object and reads its world position into `out`.
// Returns false when the token doesn't resolve or the object yields no position.
bool ReadUnitPosition(const char *token, float out[3]) {
    if (token == nullptr)
        return false;
    void *obj = reinterpret_cast<ResolveUnitToken_t>(Offsets::FUN_RESOLVE_UNIT_TOKEN)(token);
    if (obj == nullptr)
        return false;
    auto **vtable = *reinterpret_cast<void ***>(obj);
    auto fn = reinterpret_cast<GetPosition_t>(
        vtable[Offsets::OFF_CGOBJECT_VTBL_GET_POSITION / 4]);
    // GetPosition returns a float* that may be `out` (it filled the buffer) or a
    // pointer at an internal cached field — copy from whichever it hands back.
    float *p = fn(obj, out);
    if (p == nullptr)
        return false;
    if (p != out) {
        out[0] = p[0];
        out[1] = p[1];
        out[2] = p[2];
    }
    return true;
}

int __cdecl Script_UnitDistanceSquared(void *L) {
    if (!Game::Lua::IsString(L, 1)) {
        Game::Lua::Error(L, "Usage: UnitDistanceSquared(\"unit\")");
        return 0;
    }
    const char *token = Game::Lua::ToString(L, 1);

    float unitPos[3] = {};
    float playerPos[3] = {};
    if (!ReadUnitPosition(token, unitPos) || !ReadUnitPosition("player", playerPos)) {
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
