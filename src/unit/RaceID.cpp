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

#include "Game.h"
#include "Offsets.h"
#include "unit/Resolve.h"

#include <cstdint>
#include <cstring>

// `UnitRaceID(unit)` → number
//
// Returns the numeric race ID (1=Human, 2=Orc, 3=Dwarf, 4=Night Elf,
// 5=Undead, 6=Tauren, 7=Gnome, 8=Troll, 10=Blood Elf, 11=Draenei) for
// the unit, or `nil` if the unit can't be resolved.
//
// Why this exists: in 3.3.5a, `UnitRace(unit)` returns
// `(localizedName, englishToken)` with no race ID. Modern Blizzard's
// `UnitRace` adds a third `raceID` return; this function backports just
// the integer so addons can dispatch on it without maintaining a
// string→ID lookup table.
//
// The sibling of `UnitClassID` — same descriptor/byte layout, one field
// over (`UNIT_FIELD_BYTES_0` byte 0 instead of byte 1). Accepts any
// standard unit token via the engine's `ResolveUnitToken` (`"player"`,
// `"target"`, `"partyN"`, `"raidN"`, `"mouseover"`, etc.).

namespace Unit::RaceID {

namespace {

int __cdecl Script_UnitRaceID(void *L) {
    if (!Game::Lua::IsString(L, 1))
        return Game::Lua::Error(L, "Usage: UnitRaceID(\"unit\")");

    const char *token = Game::Lua::ToString(L, 1);
    if (token == nullptr) {
        Game::Lua::PushNil(L);
        return 1;
    }

    // Fast path for `"player"` — matches what the engine's own
    // `Script_UnitRace` does via `FUN_006B1070`. The descriptor path
    // below would race with engine init at first login (descriptor
    // pointer isn't populated yet), returning nil; this global is set
    // during login session setup so it's always valid in-game.
    if (std::strcmp(token, "player") == 0) {
        const uint8_t raceByte =
            *reinterpret_cast<const uint8_t *>(Offsets::VAR_LOCAL_PLAYER_RACE_BYTE);
        if (raceByte == 0) {
            Game::Lua::PushNil(L);
            return 1;
        }
        Game::Lua::PushNumber(L, static_cast<double>(raceByte));
        return 1;
    }

    const uint8_t *desc = Unit::Descriptor(Unit::ResolveToken(token));
    if (desc == nullptr) {
        Game::Lua::PushNil(L);
        return 1;
    }
    const uint8_t raceByte = *(desc + Offsets::OFF_UNIT_DESCRIPTOR_RACE_BYTE);
    if (raceByte == 0) {
        Game::Lua::PushNil(L);
        return 1;
    }
    Game::Lua::PushNumber(L, static_cast<double>(raceByte));
    return 1;
}

void RegisterLuaFunctions() {
    Game::Lua::RegisterGlobalFunction("UnitRaceID", &Script_UnitRaceID);
}

const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace
} // namespace Unit::RaceID
