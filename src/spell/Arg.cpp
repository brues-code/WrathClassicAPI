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

#include "spell/Arg.h"

#include "Game.h"
#include "Offsets.h"

#include <cstdint>
#include <cstdlib>
#include <cstring>

namespace Spell::Arg {

namespace {

using NameToSpellID_t = int(__cdecl *)(const char *name, int *outBookType);

// Parses the spellID out of a spell hyperlink ("…|Hspell:N:…|h[Name]|h|r") or a
// bare "spell:N" fragment. Returns 0 when the string carries no "spell:" token,
// so the caller falls through to the numeric / name paths.
int SpellIDFromLink(const char *s) {
    const char *p = std::strstr(s, "spell:");
    if (p == nullptr)
        return 0;
    return std::atoi(p + 6); // past "spell:"
}

} // namespace

int NameToSpellID(const char *name) {
    if (name == nullptr || *name == '\0')
        return 0;
    int bookType = 0;
    auto fn = reinterpret_cast<NameToSpellID_t>(
        static_cast<uintptr_t>(Offsets::FUN_SPELL_NAME_TO_ID));
    return fn(name, &bookType);
}

int ResolveSpellID(void *L, int idx) {
    // Key on the concrete Lua type: a numeric string is still a string in Lua
    // 5.1, so it flows through the string branch's atoi below rather than being
    // mistaken for a name.
    if (Game::Lua::Type(L, idx) == Game::Lua::TYPE_NUMBER)
        return static_cast<int>(Game::Lua::ToNumber(L, idx));
    if (Game::Lua::Type(L, idx) != Game::Lua::TYPE_STRING)
        return 0;
    const char *s = Game::Lua::ToString(L, idx);
    if (s == nullptr || *s == '\0')
        return 0;

    if (const int fromLink = SpellIDFromLink(s))
        return fromLink;
    if (const int numeric = std::atoi(s); numeric > 0)
        return numeric;
    return NameToSpellID(s);
}

} // namespace Spell::Arg
