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

// `C_Spell.GetSpellTexture(spellIdentifier) -> textureFile` — the icon texture
// for a spell. `spellIdentifier` is a spell ID, name, name(subtext), or link.
// A localized name resolves only when the spell is in your (or your pet's)
// spellbook.
//
// Returns the icon path string (e.g. "Interface\\Icons\\Spell_Fire_FlameBolt"),
// which feeds directly into texture:SetTexture. Returns nil for an identifier
// that resolves to no spell, or a spell with no icon. Reads Spell.dbc →
// SpellIcon.dbc entirely from client data (no server query).

#include "Game.h"
#include "spell/Arg.h"
#include "spell/Lookup.h"

#include <cstdint>

namespace Spell::Texture {

namespace {

int __cdecl Script_C_Spell_GetSpellTexture(void *L) {
    const int spellID = Spell::Arg::ResolveSpellID(L, 1);
    if (spellID <= 0)
        return 0; // nil — unresolved identifier
    const char *path =
        Spell::Lookup::IconPathForSpell(static_cast<uint32_t>(spellID));
    if (path == nullptr || *path == '\0')
        return 0; // nil — spell exists but has no icon row
    Game::Lua::PushString(L, path);
    return 1;
}

void RegisterLuaFunctions() {
    Game::Lua::RegisterTableFunction("C_Spell", "GetSpellTexture",
                                     &Script_C_Spell_GetSpellTexture);
}

const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace

} // namespace Spell::Texture
