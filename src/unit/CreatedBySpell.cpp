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

// `UnitCreatedBySpell(unit)` — the spellID of the spell that summoned this
// unit: the totem-drop spell for a totem, the summon spell for a pet /
// guardian / wild summon. Read from the unit's UNIT_CREATED_BY_SPELL
// descriptor field — the same field the engine's unit-title builder consults
// to pick the "%s's Pet" / "%s's Guardian" / "%s's Minion" title. It's a
// broadcast field, so it works for any unit in range, not just your own
// summons. Returns nil for an unresolved unit token and for anything not
// summoned by a spell (players, world creatures).
//
// Note this is the *summoning* spell — the spells a totem casts are
// server-side and never reach the client. Mirrors the sibling 1.12 build's
// UnitCreatedBySpell.

#include "Game.h"
#include "Offsets.h"
#include "unit/Resolve.h"

#include <cstdint>

namespace Unit::CreatedBySpell {

namespace {

int __cdecl Script_UnitCreatedBySpell(void *L) {
    if (!Game::Lua::IsString(L, 1))
        return 0;
    const uint8_t *desc = Unit::Descriptor(Unit::ResolveToken(Game::Lua::ToString(L, 1)));
    if (desc == nullptr)
        return 0;
    const uint32_t spellID = *reinterpret_cast<const uint32_t *>(
        desc + Offsets::OFF_UNIT_FIELD_CREATED_BY_SPELL);
    if (spellID == 0)
        return 0;
    Game::Lua::PushNumber(L, static_cast<double>(spellID));
    return 1;
}

void RegisterLuaFunctions() {
    Game::Lua::RegisterGlobalFunction("UnitCreatedBySpell", &Script_UnitCreatedBySpell);
}

const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace

} // namespace Unit::CreatedBySpell
