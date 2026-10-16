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

// `UnitSubName(unit)` — the creature subtitle the unit tooltip shows under the
// name ("Innkeeper", "Weapon Master"), or nil.
//
// Reads it the way the tooltip builder does: for a non-player unit,
// CGUnit_C::GetUnitTitle — the subtitle from the unit's creature-cache record,
// null when the record hasn't arrived, the subtitle is empty, or the unit is a
// player's pet.

#include "Game.h"
#include "Offsets.h"
#include "unit/Resolve.h"

#include <cstdint>

namespace Unit::SubName {

namespace {

// `const char *__thiscall(const CGUnit_C *)`, no stack args.
using GetUnitTitle_t = const char *(__fastcall *)(const void *unit);

int __cdecl Script_UnitSubName(void *L) {
    if (!Game::Lua::IsString(L, 1))
        return Game::Lua::Error(L, "Usage: UnitSubName(\"unit\")");
    const auto *unit = static_cast<const uint8_t *>(Unit::ResolveToken(Game::Lua::ToString(L, 1)));
    const auto *fields =
        unit != nullptr ? *reinterpret_cast<const uint8_t *const *>(unit + Offsets::OFF_OBJECT_DESCRIPTORS)
                        : nullptr;
    const char *title = nullptr;
    if (fields != nullptr &&
        (*reinterpret_cast<const uint32_t *>(fields + Offsets::OFF_OBJECT_FIELD_TYPE) &
         Offsets::TYPEMASK_PLAYER) == 0)
        title = reinterpret_cast<GetUnitTitle_t>(Offsets::FUN_UNIT_GET_TITLE)(unit);
    if (title == nullptr)
        Game::Lua::PushNil(L);
    else
        Game::Lua::PushString(L, title);
    return 1;
}

void RegisterLuaFunctions() {
    Game::Lua::RegisterGlobalFunction("UnitSubName", &Script_UnitSubName);
}

const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace

} // namespace Unit::SubName
