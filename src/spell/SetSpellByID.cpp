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

// `GameTooltip:SetSpellByID` behavioral extension.
//
// The 3.3.5 implementation gates tooltip building on a player+pet
// spellbook walk plus a 10-slot action-bar fallback (see
// `Offsets::FUN_SET_SPELL_BY_ID_GATE`). That walk doesn't cover
// profession recipes, item-granted spells, or anything else that
// only sets a bit in `VAR_PLAYER_SPELL_BITMAP` — so calls like
// `GameTooltip:SetSpellByID(2657)` (Smelt Copper) return nil even
// for spells the player knows.
//
// We unconditionally hook the gate to "spellID is non-zero", which
// matches the behavior modern WoW (5.4+) ships after Blizzard
// removed the gate. The tooltip builder downstream handles unknown
// spells gracefully — it produces a static tooltip from `Spell.dbc`
// with no player-specific state (cooldown remaining, charges) filled
// in, same shape the engine produces for an "unknown but valid"
// spell anyway.
//
// Single xref to the gate function (only `Script_GameTooltip_
// SetSpellByID` calls it), so the blast radius is exactly the one
// tooltip method. No other engine behavior changes.

#include "Game.h"
#include "Offsets.h"

#include <cstdint>

namespace Spell::SetSpellByID {

namespace {

using Gate_t = bool(__cdecl *)(uint32_t spellID, int isPet);
Gate_t Gate_o = nullptr;

bool __cdecl Gate_h(uint32_t spellID, int /*isPet*/) {
    return spellID != 0;
}

const Game::HookAutoRegister _hookreg{
    Offsets::FUN_SET_SPELL_BY_ID_GATE,
    reinterpret_cast<void *>(&Gate_h),
    reinterpret_cast<void **>(&Gate_o)};

} // namespace
} // namespace Spell::SetSpellByID
