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

// `C_Spell.DoesSpellExist(spellIdentifier)` — true iff the identifier names a
// spell the client has Spell.dbc data for. Ported from ClassicAPI's
// src/spell/Exists.cpp, widened to retail's spellIdentifier forms (ID, link,
// name, "name(subtext)") through Spell::Arg; a name resolves only from the
// spellbook, as retail documents.

#include "Game.h"
#include "Offsets.h"
#include "spell/Arg.h"
#include "spell/Lookup.h"

#include <cstdint>

namespace Spell::Exists {

namespace {

int __cdecl Script_DoesSpellExist(void *L) {
    const int spellID = Spell::Arg::ResolveSpellID(L, 1);
    uint8_t buf[Offsets::SPELL_DBC_RECORD_SIZE];
    Game::Lua::PushBool(L, spellID > 0 &&
                               Spell::Lookup::CopyRecord(static_cast<uint32_t>(spellID), buf));
    return 1;
}

void RegisterLuaFunctions() {
    Game::Lua::RegisterTableFunction("C_Spell", "DoesSpellExist", &Script_DoesSpellExist);
}

const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace

} // namespace Spell::Exists
