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

// `UnitOwnerGUID(unit)` — the GUID of the unit's owner: whoever charms it, else
// whoever created it (the summoner of a pet, guardian or totem). Ported from
// ClassicAPI's src/unit/Pet.cpp.
//
// The owner rule is the engine's CGUnit::GetCharmerOrCreatorGuid (0x004F5F20),
// which the tooltip's "summoned by" line and the combat log use: CHARMEDBY when
// set, else CREATEDBY. It only picks between two descriptor fields, read here
// directly.

#include "Game.h"
#include "Guid.h"
#include "Offsets.h"
#include "unit/Resolve.h"

#include <cstdint>

namespace Unit::OwnerGUID {

namespace {

Guid::Pair GuidAt(const uint8_t *desc, uintptr_t offset) {
    const auto *g = reinterpret_cast<const uint32_t *>(desc + offset);
    return {g[0], g[1]};
}

int __cdecl Script_UnitOwnerGUID(void *L) {
    if (!Game::Lua::IsString(L, 1))
        return Game::Lua::Error(L, "Usage: UnitOwnerGUID(\"unit\")");
    const uint8_t *desc = Unit::Descriptor(Unit::ResolveToken(Game::Lua::ToString(L, 1)));
    Guid::Pair owner;
    if (desc != nullptr) {
        owner = GuidAt(desc, Offsets::OFF_UNIT_FIELD_CHARMED_BY);
        if (!owner.valid())
            owner = GuidAt(desc, Offsets::OFF_UNIT_FIELD_CREATED_BY);
    }
    if (!owner.valid()) {
        Game::Lua::PushNil(L);
        return 1;
    }
    char buf[Guid::STRING_SIZE];
    Game::Lua::PushString(L, Guid::Format(owner, buf, sizeof buf));
    return 1;
}

void RegisterLuaFunctions() {
    Game::Lua::RegisterGlobalFunction("UnitOwnerGUID", &Script_UnitOwnerGUID);
}

const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace

} // namespace Unit::OwnerGUID
