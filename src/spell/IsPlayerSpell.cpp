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

// `IsPlayerSpell(spellID)` — Cataclysm-era addition that 3.3.5 doesn't
// register, but the underlying data structure the function reads on
// modern WoW DOES exist in 3.3.5: a dword bitmap with one bit per
// spellID, populated by the engine's spell-learner whenever a spell
// is granted from any source (trained ability, racial, talent passive,
// profession recipe, item-granted, etc.).
//
// Critically, profession recipes set the bitmap bit but DON'T land in
// the displayable spellbook arrays at `DAT_00BE6D88` — which is why
// the engine's own `IsSpellKnown` returns false for them. Reading the
// bitmap directly closes that gap.
//
// Same algorithm and bounds-check pattern as ClassicAPI's 1.12 port
// (`src/spell/Usable.cpp::PlayerKnowsSpell`). Index formula matches
// the engine's reader at `FUN_0053C5B0`.

#include "Game.h"
#include "Offsets.h"

#include <cstdint>

namespace Spell::IsPlayerSpell {

namespace {

bool PlayerKnowsSpell(int spellID) {
    if (spellID < 1)
        return false;
    const uint32_t maxSpellID =
        *reinterpret_cast<const uint32_t *>(Offsets::VAR_MAX_SPELL_ID);
    if (static_cast<uint32_t>(spellID) > maxSpellID)
        return false;
    auto *bitmap = *reinterpret_cast<const uint32_t *const *>(
        Offsets::VAR_PLAYER_SPELL_BITMAP);
    if (bitmap == nullptr)
        return false;
    const uint32_t bit = 1u << (static_cast<uint32_t>(spellID) & 31);
    return (bitmap[spellID >> 5] & bit) != 0;
}

int __cdecl Script_IsPlayerSpell(void *L) {
    if (!Game::Lua::IsNumber(L, 1))
        return Game::Lua::Error(L, "Usage: IsPlayerSpell(spellID)");
    const int spellID = static_cast<int>(Game::Lua::ToNumber(L, 1));
    Game::Lua::PushBool(L, PlayerKnowsSpell(spellID));
    return 1;
}

// `CanDualWield()` — true if the local player can equip a weapon in the off
// hand. Reads the engine's dual-wield latch (see Offsets.h): the spellID of
// the granting spell, set on learning any spell whose first effect is
// SPELL_EFFECT_DUAL_WIELD and cleared when that spell is unlearned — so a
// shaman leaving Enhancement drops it, matching the server's capability flag
// exactly.
int __cdecl Script_CanDualWield(void *L) {
    const uint32_t grantSpellID = *reinterpret_cast<const uint32_t *>(
        static_cast<uintptr_t>(Offsets::VAR_DUAL_WIELD_GRANT_SPELL_ID));
    Game::Lua::PushBool(L, grantSpellID != 0);
    return 1;
}

void RegisterLuaFunctions() {
    Game::Lua::RegisterGlobalFunction("IsPlayerSpell", &Script_IsPlayerSpell);
    Game::Lua::RegisterGlobalFunction("CanDualWield", &Script_CanDualWield);
}

const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace
} // namespace Spell::IsPlayerSpell
