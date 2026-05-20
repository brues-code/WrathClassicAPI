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

#pragma once

#include <cstdint>

// Shared aura-table primitives consumed by `C_UnitAuras.*`. Unlike
// ClassicAPI's 1.12 sibling — which has to walk the unit's updatefields
// descriptor and consult a player-buff side table for duration data —
// 3.3.5 stores per-aura state (duration, expiration, caster GUID,
// stacks) directly on each `CGUnit` for every unit. So our walk is a
// flat scan of the aura-entry array at `unit + 0xC50`; "slot" here is
// just the unfiltered linear index into that array.
//
// Filter is per-entry (a flag bit on the entry itself), not a slot
// partition like 1.12's "0..31 = buffs, 32..47 = debuffs".

namespace Aura::Data {

// Filter for slot iteration. Mirrors modern's HELPFUL / HARMFUL
// filter strings; everything else parses to HELPFUL by default.
enum class Filter { Helpful, Harmful };

// Number of aura entries on `unit`. Walks the inline-vs-overflow
// switch at `unit + OFF_CGUNIT_AURA_INLINE_COUNT`. Returns 0 for
// a null unit pointer.
int SlotCount(const uint8_t *unit);

// Linear pointer into the aura-entry array. Returns nullptr for
// out-of-range slots, null unit, or empty (spellID == 0) entries
// — caller treats nullptr as "not populated".
const uint8_t *EntryAt(const uint8_t *unit, int slot);

// True iff the slot has a non-zero spellID. Empty slots between
// populated ones are legal in the engine's storage.
bool IsSlotPopulated(const uint8_t *unit, int slot);

// True iff entry's helpful bit (0x80 in the byte at OFF_AURA_FLAGS)
// is set. The engine pre-classifies each aura at apply time —
// helpful auras are buffs, harmful auras are debuffs.
bool IsHelpful(const uint8_t *entry);

// `oneBasedIndex`-th populated aura on `unit` matching `filter`.
// Walks the linear aura-entry array; returns -1 if no match.
int FindNthSlot(const uint8_t *unit, int oneBasedIndex, Filter filter);

// First populated slot with the given spellID. `filter==nullptr`
// scans the entire array; non-null restricts to one polarity.
int FindSlotBySpellID(const uint8_t *unit, uint32_t spellID,
                      const Filter *filter);

// Builds the modern-style `AuraData` table on top of the Lua stack
// for the populated slot at `unit + slot`. Caller is responsible
// for having validated that the slot is populated. Net stack
// effect: +1 (the table).
//
// Real-data fields:
//   name, icon, applications, spellId, dispelName,
//   isHelpful, isHarmful, duration, expirationTime, sourceUnit,
//   isFromPlayerOrPlayerPet, isStealable, timeMod
//
// Vanilla-truthful defaults (modern provides them; 3.3.5 lacks
// the underlying systems):
//   charges=0, maxCharges=0, points=nil, auraInstanceID=nil,
//   isBossAura=false, isNameplateOnly=false,
//   nameplateShowAll=false, nameplateShowPersonal=false,
//   canApplyAura=false, shouldConsolidate=false, isRaid=false
void Push(void *L, const uint8_t *unit, int slot);

} // namespace Aura::Data
