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

// `C_Spell.IsSpellHarmful(spellIdentifier)` / `IsSpellHelpful(spellIdentifier)`
// — true when the spell is cast at enemies / at allies respectively.
// Classification is the engine's own intent classifier
// (FUN_SPELL_CLASSIFY_INTENT — the primitive behind the native spellbook-slot
// IsHarmfulSpell / IsHelpfulSpell), given a spellID front door via the shared
// identifier resolver, so any spell classifies — not just spellbook entries.
// The two aren't strict inverses: utility / geometry-targeted spells return
// false for both.
//
// `C_Spell.IsSelfBuff(spellIdentifier)` — true iff every active effect on the
// spell targets the caster and only the caster: each slot with a non-zero
// Effect code must have both implicit targets in {TARGET_NONE, TARGET_UNIT_
// CASTER}. Mirrors the ClassicAPI implementation, on the 3.3.5 record layout.
//
// All three return false for an identifier that doesn't resolve.

#include "Game.h"
#include "Offsets.h"
#include "spell/Arg.h"
#include "spell/Lookup.h"

#include <cstdint>

namespace Spell::Classify {

namespace {

// `int __cdecl(record)` — see FUN_SPELL_CLASSIFY_INTENT in Offsets.h.
using ClassifyIntent_t = int(__cdecl *)(const uint8_t *record);

constexpr int INTENT_HELPFUL = 1;
constexpr int INTENT_HARMFUL = 2;

// Classifier verdict for the spellIdentifier at Lua stack index 1; 0 for an
// unresolvable identifier (also the classifier's own "neither" verdict).
int Intent(void *L) {
    const int spellID = Spell::Arg::ResolveSpellID(L, 1);
    if (spellID <= 0)
        return 0;
    uint8_t record[Offsets::SPELL_DBC_RECORD_SIZE];
    if (!Spell::Lookup::CopyRecord(static_cast<uint32_t>(spellID), record))
        return 0;
    return reinterpret_cast<ClassifyIntent_t>(
        static_cast<uintptr_t>(Offsets::FUN_SPELL_CLASSIFY_INTENT))(record);
}

int __cdecl Script_IsSpellHarmful(void *L) {
    Game::Lua::PushBool(L, Intent(L) == INTENT_HARMFUL);
    return 1;
}

int __cdecl Script_IsSpellHelpful(void *L) {
    Game::Lua::PushBool(L, Intent(L) == INTENT_HELPFUL);
    return 1;
}

// 3.3.5 implicit-target codes: an effect is self-only when both its targets
// are TARGET_NONE (unset slot) or TARGET_UNIT_CASTER.
bool IsSelfOnlyTarget(int target) { return target == 0 || target == 1; }

int __cdecl Script_IsSelfBuff(void *L) {
    bool selfBuff = false;
    const int spellID = Spell::Arg::ResolveSpellID(L, 1);
    uint8_t record[Offsets::SPELL_DBC_RECORD_SIZE];
    if (spellID > 0 && Spell::Lookup::CopyRecord(static_cast<uint32_t>(spellID), record)) {
        bool sawEffect = false;
        selfBuff = true;
        for (int i = 0; i < 3; ++i) {
            const int effect = *reinterpret_cast<const int *>(
                record + Offsets::OFF_SPELL_EFFECT + i * 4);
            if (effect == 0)
                continue; // unused effect slot
            sawEffect = true;
            const int targetA = *reinterpret_cast<const int *>(
                record + Offsets::OFF_SPELL_EFFECT_IMPLICIT_TARGET_A + i * 4);
            const int targetB = *reinterpret_cast<const int *>(
                record + Offsets::OFF_SPELL_EFFECT_IMPLICIT_TARGET_B + i * 4);
            if (!IsSelfOnlyTarget(targetA) || !IsSelfOnlyTarget(targetB)) {
                selfBuff = false;
                break;
            }
        }
        selfBuff = selfBuff && sawEffect;
    }
    Game::Lua::PushBool(L, selfBuff);
    return 1;
}

void RegisterLuaFunctions() {
    Game::Lua::RegisterTableFunction("C_Spell", "IsSpellHarmful", &Script_IsSpellHarmful);
    Game::Lua::RegisterTableFunction("C_Spell", "IsSpellHelpful", &Script_IsSpellHelpful);
    Game::Lua::RegisterTableFunction("C_Spell", "IsSelfBuff", &Script_IsSelfBuff);
}

const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace

} // namespace Spell::Classify
