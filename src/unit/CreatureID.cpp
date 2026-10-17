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

// `UnitCreatureID(unit)` — the creature-template (NPC) ID of a unit, or nil.
// Ported from ClassicAPI's src/unit/CreatureID.cpp.
//
// Reads the unit's OBJECT_FIELD_ENTRY, the template ID the engine looks the
// unit's creature-cache record up by. A player's entry is not a creature
// template, so players give nil. Read live off the unit rather than parsed from
// its GUID: a pet's GUID carries its pet number, not its template.

#include "Game.h"
#include "Offsets.h"
#include "unit/Resolve.h"

#include <cstdint>

namespace Unit::CreatureID {

namespace {

int __cdecl Script_UnitCreatureID(void *L) {
    if (!Game::Lua::IsString(L, 1))
        return Game::Lua::Error(L, "Usage: UnitCreatureID(\"unit\")");
    const auto *unit = static_cast<const uint8_t *>(Unit::ResolveToken(Game::Lua::ToString(L, 1)));
    const auto *fields =
        unit != nullptr ? *reinterpret_cast<const uint8_t *const *>(unit + Offsets::OFF_OBJECT_DESCRIPTORS)
                        : nullptr;
    uint32_t entry = 0;
    if (fields != nullptr &&
        (*reinterpret_cast<const uint32_t *>(fields + Offsets::OFF_OBJECT_FIELD_TYPE) &
         Offsets::TYPEMASK_PLAYER) == 0)
        entry = *reinterpret_cast<const uint32_t *>(fields + Offsets::OFF_DESCRIPTOR_ENTRY);
    if (entry == 0)
        Game::Lua::PushNil(L);
    else
        Game::Lua::PushNumber(L, static_cast<double>(entry));
    return 1;
}

void RegisterLuaFunctions() {
    Game::Lua::RegisterGlobalFunction("UnitCreatureID", &Script_UnitCreatureID);
}

const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace

} // namespace Unit::CreatureID
