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

// `UnitPowerMissing(unit[, powerType])` — the power deficit, i.e.
// `UnitPowerMax(unit, powerType) - UnitPower(unit, powerType)`. The power analog
// of UnitHealthMissing: a convenience for UI that would otherwise call both and
// subtract every frame.
//
// `powerType` (0 mana, 1 rage, 2 focus, 3 energy, 4 happiness, 5 runes, 6 runic
// power) is optional — omitted (or 7), the unit's active power type is used, same
// as UnitPower/UnitPowerMax. Reads current/max power natively (no calls into
// Lua-exposed functions) from whichever source the client has: the unit
// descriptor's POWER/MAXPOWER fields in range, or the party/raid roster cache for
// a group member out of range (via the shared Unit::ResolveMember fallthrough).
// The roster caches only the member's active power type, so requesting a
// different explicit type for an out-of-range member reads as 0.
//
// Rage and runic power are stored x10, so each side is divided by the power's
// display divisor (matching UnitPower) before subtracting; the deficit is clamped
// at 0. Returns 0 for an absent/unknown unit — the same 0 UnitPowerMax - UnitPower
// would give.

#include "Game.h"
#include "Offsets.h"
#include "unit/Resolve.h"

#include <cstdint>

namespace Unit::PowerMissing {

namespace {

// Sentinel meaning "the unit's active power type" — the engine's own default
// when UnitPower is called without an explicit type.
constexpr int kActivePowerType = 7;

struct Power {
    uint32_t cur;
    uint32_t max;
    int type; // resolved concrete type (0..6), for the divisor lookup
};

uint32_t U16(const uint8_t *p, unsigned off) {
    return *reinterpret_cast<const uint16_t *>(p + off);
}
uint32_t U32(const uint8_t *p, unsigned off) {
    return *reinterpret_cast<const uint32_t *>(p + off);
}

// The x10-style display divisor for `type` (1 for most, 10 for rage / runic).
uint32_t Divisor(int type) {
    if (type < 0 || type > 6)
        return 1;
    const uint32_t d = *reinterpret_cast<const uint32_t *>(
        static_cast<uintptr_t>(Offsets::VAR_POWER_DISPLAY_DIVISOR_TABLE) +
        static_cast<uintptr_t>(type) * 4u);
    return d != 0 ? d : 1;
}

// Reads raw (current, max) power + the concrete power type for `token` and
// `reqType` (kActivePowerType = use the unit's active type). Returns false when
// no reading applies — unknown unit, or an explicit type an out-of-range roster
// member doesn't cache.
bool ReadPower(const char *token, int reqType, Power *out) {
    const Unit::Member m = Unit::ResolveMember(token);
    switch (m.source) {
    case Unit::MemberSource::Object: {
        int type = (reqType == kActivePowerType)
                       ? *(m.data + Offsets::OFF_UNIT_DESCRIPTOR_POWER_TYPE)
                       : reqType;
        if (type < 0 || type > 6)
            return false;
        out->cur = U32(m.data, Offsets::OFF_UNIT_FIELD_POWER_BASE + type * 4);
        out->max = U32(m.data, Offsets::OFF_UNIT_FIELD_MAXPOWER_BASE + type * 4);
        out->type = type;
        return true;
    }
    case Unit::MemberSource::Party: {
        const int stored = *(m.data + Offsets::OFF_PARTY_MEMBER_POWER_TYPE);
        if (reqType != kActivePowerType && reqType != stored)
            return false;
        out->cur = U16(m.data, Offsets::OFF_PARTY_MEMBER_POWER);
        out->max = U16(m.data, Offsets::OFF_PARTY_MEMBER_MAXPOWER);
        out->type = stored;
        return true;
    }
    case Unit::MemberSource::Raid: {
        const int stored = *(m.data + Offsets::OFF_RAID_MEMBER_POWER_TYPE);
        if (reqType != kActivePowerType && reqType != stored)
            return false;
        out->cur = U16(m.data, Offsets::OFF_RAID_MEMBER_POWER);
        out->max = U16(m.data, Offsets::OFF_RAID_MEMBER_MAXPOWER);
        out->type = stored;
        return true;
    }
    case Unit::MemberSource::None:
        break;
    }
    return false;
}

int __cdecl Script_UnitPowerMissing(void *L) {
    if (!Game::Lua::IsString(L, 1)) {
        Game::Lua::Error(L, "Usage: UnitPowerMissing(\"unit\"[, powerType])");
        return 0;
    }
    const char *token = Game::Lua::ToString(L, 1);

    int reqType = kActivePowerType;
    if (Game::Lua::IsNumber(L, 2)) {
        reqType = static_cast<int>(Game::Lua::ToNumber(L, 2));
        if (reqType < 0 || reqType > 7) {
            Game::Lua::PushNumber(L, 0.0); // out-of-range type — same as the engine
            return 1;
        }
    }

    Power p;
    if (token == nullptr || !ReadPower(token, reqType, &p)) {
        Game::Lua::PushNumber(L, 0.0);
        return 1;
    }

    // Divide each side like UnitPower does (integer, per-side) so the result is
    // exactly UnitPowerMax - UnitPower in display units.
    const uint32_t div = Divisor(p.type);
    const uint32_t cur = p.cur / div;
    const uint32_t max = p.max / div;
    Game::Lua::PushNumber(L, static_cast<double>(max > cur ? max - cur : 0u));
    return 1;
}

void RegisterLuaFunctions() {
    Game::Lua::RegisterGlobalFunction("UnitPowerMissing", &Script_UnitPowerMissing);
}

const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace

} // namespace Unit::PowerMissing
