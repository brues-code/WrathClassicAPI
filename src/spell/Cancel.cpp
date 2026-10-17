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

// `C_Spell.CancelSpellByID(spellID)` — cancel the player's aura from `spellID`,
// as right-clicking its buff icon does. Ported from ClassicAPI's
// src/spell/Cancel.cpp.
//
// Follows the engine's own CancelUnitBuff path for the player, by spell ID
// instead of buff index: find the aura, apply the same "can the player cancel
// this" test (a positive aura, or a channelled spell), and cancel it
// through Spell_C_CancelAura — which also handles the auras that aren't a
// plain CMSG_CANCEL_AURA (mounts, companion pets). Not a protected action in
// this client: CancelUnitBuff makes no CanPerformAction check either.

#include "Game.h"
#include "Offsets.h"
#include "aura/Data.h"
#include "spell/Lookup.h"
#include "unit/Player.h"

#include <cstdint>

namespace Spell::Cancel {

namespace {

using CancelAura_t = void(__cdecl *)(const uint8_t *spell, const void *unit, int32_t slot);

int __cdecl Script_CancelSpellByID(void *L) {
    if (!Game::Lua::IsNumber(L, 1))
        return Game::Lua::Error(L, "Usage: C_Spell.CancelSpellByID(spellID)");
    const auto spellID = static_cast<uint32_t>(Game::Lua::ToNumber(L, 1));
    const auto *player = static_cast<const uint8_t *>(Unit::LocalPlayer());
    uint8_t spell[Offsets::SPELL_DBC_RECORD_SIZE];
    if (player == nullptr || !Spell::Lookup::CopyRecord(spellID, spell))
        return 0;
    const bool channeled =
        (*reinterpret_cast<const uint32_t *>(spell + Offsets::OFF_SPELL_RECORD_ATTRIBUTES_EX) &
         Offsets::SPELL_ATTR_EX_CHANNELED) != 0;
    const int count = Aura::Data::SlotCount(player);
    for (int slot = 0; slot < count; ++slot) {
        const uint8_t *entry = Aura::Data::EntryAt(player, slot);
        if (entry == nullptr ||
            *reinterpret_cast<const uint32_t *>(entry + Offsets::OFF_AURA_SPELL_ID) != spellID)
            continue;
        if (channeled || (entry[Offsets::OFF_AURA_FLAGS] & Offsets::AURA_FLAG_POSITIVE) != 0) {
            reinterpret_cast<CancelAura_t>(Offsets::FUN_SPELL_CANCEL_AURA)(spell, player, slot);
            break;
        }
    }
    return 0;
}

void RegisterLuaFunctions() {
    Game::Lua::RegisterTableFunction("C_Spell", "CancelSpellByID", &Script_CancelSpellByID);
}

const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace

} // namespace Spell::Cancel
