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

// `UnitCastingDuration(unit)` and `UnitChannelDuration(unit)`: the cast or
// channel UnitCastingInfo / UnitChannelInfo report, as a new duration object
// each call, or nothing when they report none. The object is a snapshot:
// retail's keeps the times it was made with after the cast ends (ClassicAPI's
// retail probe), so pushback reaches only a later call. Pushback taken so far
// is in it, as in UnitCastingInfo's end time.
//
// The cast is read as the engine's Script_UnitCastingInfo (0x00611DF0) and
// Script_UnitChannelInfo (0x00612090) read it: the unit's spell ID, start and
// end, reported while the spell's record exists and the end has not passed.

#include "Game.h"
#include "Offsets.h"
#include "duration/Object.h"
#include "spell/Lookup.h"
#include "time/Clock.h"
#include "unit/Resolve.h"

#include <cstdint>

namespace Spell::CastDuration {

namespace {

namespace Lua = Game::Lua;

uint32_t Field(const void *unit, unsigned off) {
    return *reinterpret_cast<const uint32_t *>(static_cast<const uint8_t *>(unit) + off);
}

struct Fields {
    unsigned spellID, startMs, endMs;
};

constexpr Fields kCast{Offsets::OFF_UNIT_CASTING_SPELL_ID, Offsets::OFF_UNIT_CAST_START_MS,
                       Offsets::OFF_UNIT_CAST_END_MS};
constexpr Fields kChannel{Offsets::OFF_UNIT_CHANNEL_SPELL_ID, Offsets::OFF_UNIT_CHANNEL_START_MS,
                          Offsets::OFF_UNIT_CHANNEL_END_MS};

int PushDuration(void *L, const char *usage, const Fields &f) {
    if (!Lua::IsString(L, 1)) {
        Lua::Error(L, usage);
        return 0;
    }
    const void *unit = Unit::ResolveToken(Lua::ToString(L, 1));
    if (unit == nullptr)
        return 0;
    uint8_t record[Offsets::SPELL_DBC_RECORD_SIZE];
    const uint32_t endMs = Field(unit, f.endMs);
    if (!Spell::Lookup::CopyRecord(Field(unit, f.spellID), record) ||
        static_cast<int32_t>(Time::Clock::NowMs() - endMs) >= 0)
        return 0;
    Duration::Object::PushFromMs(L, Field(unit, f.startMs), endMs);
    return 1;
}

int __cdecl Script_UnitCastingDuration(void *L) {
    return PushDuration(L, "Usage: UnitCastingDuration(\"unit\")", kCast);
}

int __cdecl Script_UnitChannelDuration(void *L) {
    return PushDuration(L, "Usage: UnitChannelDuration(\"unit\")", kChannel);
}

void RegisterLuaFunctions() {
    Lua::RegisterGlobalFunction("UnitCastingDuration", &Script_UnitCastingDuration);
    Lua::RegisterGlobalFunction("UnitChannelDuration", &Script_UnitChannelDuration);
}

const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace

} // namespace Spell::CastDuration
