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

// `UnitHealthMissing(unit)` — the health deficit, i.e. `UnitHealthMax(unit) -
// UnitHealth(unit)`. A convenience for healing addons (overheal checks, "missing
// health" bars) that otherwise call both engine functions and subtract in Lua
// every frame.
//
// Reads current + max health natively (no calls into Lua-exposed functions),
// mirroring how the engine's own health accessors source the values:
//   - in the client's object-sync range -> the unit descriptor's HEALTH /
//     MAXHEALTH fields;
//   - a party/raid member outside that range (no live object) -> the party then
//     raid roster HP caches, the same numbers the group frames show.
// The roster fallback is why a healer's "missing health" stays correct for raid
// members across the room; a bare descriptor read would report them at full.
//
// Health carries no display divisor, so the deficit is a plain subtraction,
// clamped at 0 (a transient current > max never surfaces as negative). Returns 0
// for a valid-but-absent / unknown unit, matching UnitHealth's 0-for-missing.

#include "Game.h"
#include "Offsets.h"
#include "unit/Resolve.h"

#include <cstdint>

namespace Unit::HealthMissing {

namespace {

uint32_t Field32(const void *base, unsigned off) {
    return *reinterpret_cast<const uint32_t *>(static_cast<const uint8_t *>(base) + off);
}

// Reads (current, max) health for `token` from whichever source the client has —
// the live object's descriptor, or the party/raid roster cache for a group
// member out of object range (via the shared Unit::ResolveMember fallthrough).
// Returns false only when the unit is unknown.
bool ReadHealth(const char *token, uint32_t *cur, uint32_t *max) {
    const Unit::Member m = Unit::ResolveMember(token);
    switch (m.source) {
    case Unit::MemberSource::Object:
        *cur = Field32(m.data, Offsets::OFF_UNIT_FIELD_HEALTH);
        *max = Field32(m.data, Offsets::OFF_UNIT_FIELD_MAXHEALTH);
        return true;
    case Unit::MemberSource::Party:
        *cur = Field32(m.data, Offsets::OFF_PARTY_MEMBER_HEALTH);
        *max = Field32(m.data, Offsets::OFF_PARTY_MEMBER_MAXHEALTH);
        return true;
    case Unit::MemberSource::Raid:
        *cur = Field32(m.data, Offsets::OFF_RAID_MEMBER_HEALTH);
        *max = Field32(m.data, Offsets::OFF_RAID_MEMBER_MAXHEALTH);
        return true;
    case Unit::MemberSource::None:
        break;
    }
    return false;
}

int __cdecl Script_UnitHealthMissing(void *L) {
    if (!Game::Lua::IsString(L, 1)) {
        Game::Lua::Error(L, "Usage: UnitHealthMissing(\"unit\")");
        return 0;
    }
    const char *token = Game::Lua::ToString(L, 1);

    uint32_t cur = 0, max = 0;
    if (token == nullptr || !ReadHealth(token, &cur, &max)) {
        Game::Lua::PushNumber(L, 0.0);
        return 1;
    }
    Game::Lua::PushNumber(L, static_cast<double>(max > cur ? max - cur : 0u));
    return 1;
}

void RegisterLuaFunctions() {
    Game::Lua::RegisterGlobalFunction("UnitHealthMissing", &Script_UnitHealthMissing);
}

const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace

} // namespace Unit::HealthMissing
