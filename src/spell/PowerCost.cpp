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

// `C_Spell.GetSpellPowerCost(spellIdentifier) -> costs` — an array of power-cost
// tables for a spell, or nil if the identifier resolves to no spell. Each entry:
//   type            Enum.PowerType (0 = mana, 1 = rage, 3 = energy, -2 = health, …)
//   name            power-type token ("MANA", "RAGE", "ENERGY", …)
//   cost            the upfront cost for the local player
//   minCost         same as cost (3.3.5 costs are fixed)
//   costPercent     cost as a percentage of the caster's base mana
//   costPerSec      per-second cost (mana-per-second channels)
//   requiredAuraID  0 (no 3.3.5 equivalent)
//   hasRequiredAura false
//
// The cost is resolved for the local player through the engine's own calculators
// (base + per-skill scaling + %-of-base-mana + talent/aura mods) — the same path
// the native GetSpellInfo takes — so WotLK's percentage-of-base-mana spells
// report their real cost, not the flat 0 in Spell.dbc. A spell with no cost
// returns an empty array.

#include "Game.h"
#include "Offsets.h"
#include "spell/Arg.h"
#include "spell/Lookup.h"
#include "unit/Player.h"

#include <cstdint>

namespace Spell::PowerCost {

namespace {

using CostFn_t = int(__cdecl *)(const void *record, void *casterObj);

int CallCost(uintptr_t fn, const void *record, void *caster) {
    return reinterpret_cast<CostFn_t>(fn)(record, caster);
}

// Power display divisor (rage / runic power are stored x10). 1 for mana, health,
// and any out-of-range type.
int PowerDivisor(int powerType) {
    if (powerType < 0 || powerType > 6)
        return 1;
    const uint32_t d = reinterpret_cast<const uint32_t *>(
        Offsets::VAR_POWER_DISPLAY_DIVISOR_TABLE)[powerType];
    return (d != 0) ? static_cast<int>(d) : 1;
}

const char *PowerTypeName(int powerType) {
    switch (powerType) {
        case 0:  return "MANA";
        case 1:  return "RAGE";
        case 2:  return "FOCUS";
        case 3:  return "ENERGY";
        case 4:  return "HAPPINESS";
        case 5:  return "RUNES";
        case 6:  return "RUNIC_POWER";
        case -2: return "HEALTH";
        default: return "MANA";
    }
}

int __cdecl Script_C_Spell_GetSpellPowerCost(void *L) {
    const int spellID = Spell::Arg::ResolveSpellID(L, 1);
    if (spellID <= 0)
        return 0; // nil — unresolved identifier

    uint8_t record[Offsets::SPELL_DBC_RECORD_SIZE];
    if (!Spell::Lookup::CopyRecord(static_cast<uint32_t>(spellID), record))
        return 0; // nil — unknown spell

    Game::Lua::NewTable(L); // the costs array (returned; empty when there's no cost)

    void *caster = Unit::LocalPlayer();
    if (caster == nullptr)
        return 1; // no player yet — empty array

    const int powerType = *reinterpret_cast<const int *>(
        record + Offsets::OFF_SPELL_POWER_TYPE);
    const int divisor = PowerDivisor(powerType);
    const int cost = CallCost(Offsets::FUN_SPELL_POWER_COST, record, caster) / divisor;
    const int costPerSec = CallCost(Offsets::FUN_SPELL_COST_PER_SEC, record, caster) / divisor;
    const int costPercent = *reinterpret_cast<const int *>(
        record + Offsets::OFF_SPELL_POWER_COST_PERCENT);

    if (cost <= 0 && costPerSec <= 0)
        return 1; // free spell — empty array

    Game::Lua::NewTable(L); // one SpellPowerCostInfo entry
    Game::Lua::SetFieldNumber(L, "type", static_cast<double>(powerType));
    Game::Lua::SetFieldString(L, "name", PowerTypeName(powerType));
    Game::Lua::SetFieldNumber(L, "cost", static_cast<double>(cost));
    Game::Lua::SetFieldNumber(L, "minCost", static_cast<double>(cost));
    Game::Lua::SetFieldNumber(L, "costPercent", static_cast<double>(costPercent));
    Game::Lua::SetFieldNumber(L, "costPerSec", static_cast<double>(costPerSec));
    Game::Lua::SetFieldNumber(L, "requiredAuraID", 0.0);
    Game::Lua::SetFieldBool(L, "hasRequiredAura", false);
    Game::Lua::RawSetI(L, -2, 1); // costs[1] = entry

    return 1;
}

void RegisterLuaFunctions() {
    Game::Lua::RegisterTableFunction("C_Spell", "GetSpellPowerCost",
                                     &Script_C_Spell_GetSpellPowerCost);
}

const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace

} // namespace Spell::PowerCost
