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

// `UnitRaceBase(unit) -> (raceFile, raceID)` — the locale-independent race
// token (`"Human"`, `"Orc"`, `"Dwarf"`, `"NightElf"`, `"Scourge"`, `"Tauren"`,
// `"Gnome"`, `"Troll"`, `"BloodElf"`, `"Draenei"`) plus the numeric race ID, or
// `(nil, nil)` if the unit can't be resolved.
//
// 3.3.5's `UnitRace(unit)` returns `(localizedName, raceFile)`; modern Blizzard's
// `UnitRaceBase` drops the localized name and adds the race ID. The race token is
// the `Filename` (clientFileString) column of `ChrRaces.dbc` — the same string
// `UnitRace` returns as its second value — and the race ID is the descriptor byte
// `UnitRaceID` already reads. This pairs them in the modern shape.

#include "Game.h"
#include "Offsets.h"
#include "unit/Resolve.h"

#include <cstdint>
#include <cstring>

namespace Unit::RaceBase {

namespace {

// UNIT_FIELD_BYTES_0 race byte for `token` (0 when unresolvable / not yet
// synced). Mirrors Script_UnitRaceID: the "player" token reads the login-session
// global (valid before the in-world descriptor exists); all others go through the
// resolved unit's descriptor.
uint8_t RaceByte(const char *token) {
    if (std::strcmp(token, "player") == 0)
        return *reinterpret_cast<const uint8_t *>(Offsets::VAR_LOCAL_PLAYER_RACE_BYTE);

    const uint8_t *desc = Unit::Descriptor(Unit::ResolveToken(token));
    if (desc == nullptr)
        return 0;
    return *(desc + Offsets::OFF_UNIT_DESCRIPTOR_RACE_BYTE);
}

// Locale-independent race token from ChrRaces.dbc for `raceID`, or nullptr for an
// out-of-range id / empty record.
const char *RaceToken(int raceID) {
    const int minIndex = *reinterpret_cast<const int32_t *>(Offsets::VAR_CHRRACES_DBC_MIN_INDEX);
    const int maxIndex = *reinterpret_cast<const int32_t *>(Offsets::VAR_CHRRACES_DBC_MAX_INDEX);
    if (raceID < minIndex || raceID > maxIndex)
        return nullptr;
    auto *table = *reinterpret_cast<const uint8_t *const *const *>(
        Offsets::VAR_CHRRACES_DBC_INDEX_TABLE);
    if (table == nullptr)
        return nullptr;
    const uint8_t *record = table[raceID - minIndex];
    if (record == nullptr)
        return nullptr;
    return *reinterpret_cast<const char *const *>(
        record + Offsets::OFF_CHRRACES_CLIENT_FILE_STRING);
}

int __cdecl Script_UnitRaceBase(void *L) {
    if (!Game::Lua::IsString(L, 1)) {
        Game::Lua::Error(L, "Usage: UnitRaceBase(\"unit\")");
        return 0;
    }
    const char *token = Game::Lua::ToString(L, 1);

    const uint8_t raceByte = (token != nullptr) ? RaceByte(token) : 0;
    const char *raceFile = (raceByte != 0) ? RaceToken(raceByte) : nullptr;
    if (raceFile == nullptr) {
        Game::Lua::PushNil(L);
        Game::Lua::PushNil(L);
        return 2;
    }

    Game::Lua::PushString(L, raceFile);
    Game::Lua::PushNumber(L, static_cast<double>(raceByte));
    return 2;
}

void RegisterLuaFunctions() {
    Game::Lua::RegisterGlobalFunction("UnitRaceBase", &Script_UnitRaceBase);
}

const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace

} // namespace Unit::RaceBase
