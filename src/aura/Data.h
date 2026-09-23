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
//
// The array holds every aura the server ever applied, including ones the
// client's own buff frame never shows an icon for — secondary-skill
// tracking (Find Herbs, ...) and stance/shapeshift-form auras (Defensive
// Stance, ...). The engine excludes these from `UnitBuff`/`UnitDebuff` via
// a Spell.dbc predicate (`IsVisibleSlot`), not via anything in the array
// entry itself, so every enumeration here defaults to the same exclusion —
// pass `includeHidden = true` (the `C_UnitAuras.*` "HIDDEN" filter token) to
// see them anyway.

namespace Aura::Data {

// Filter for slot iteration. Mirrors modern's HELPFUL / HARMFUL
// filter strings; everything else parses to HELPFUL by default.
enum class Filter { Helpful, Harmful };

// How a resolved aura is emitted onto the Lua stack. `Table` builds the
// modern `AuraData` table (net stack +1); `Positional` pushes the 15-value
// `UnitAura` tuple (net stack +15) with no table allocation, for the
// accessors that avoid the per-call GC churn. Selection and field
// resolution are identical for both — only the terminal leaf differs.
enum class Emit { Table, Positional };

// Number of values `Emit::Positional` pushes.
constexpr int POSITIONAL_COUNT = 15;

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

// True iff the populated slot's spell doesn't carry the engine's "hidden
// aura" markers — see `SPELL_ATTR_HIDDEN_CLIENTSIDE` / `SPELL_ATTR_EX_
// NO_AURA_ICON` / `SPELL_AURA_TRACK_*` / `SPELL_AURA_MOD_STALKED` in
// Offsets.h for exactly what's excluded (secondary-skill tracking, stance/
// shapeshift-form auras, and anything else explicitly marked no-icon).
// True for an empty slot (nothing to hide) or a spell record lookup miss.
bool IsVisibleSlot(const uint8_t *unit, int slot);

// `oneBasedIndex`-th populated aura on `unit` matching `filter`. Walks the
// linear aura-entry array; returns -1 if no match. Hidden auras (see
// `IsVisibleSlot`) are skipped unless `includeHidden` is true.
int FindNthSlot(const uint8_t *unit, int oneBasedIndex, Filter filter,
                bool includeHidden = false);

// First populated slot with the given spellID. `filter==nullptr` scans the
// entire array; non-null restricts to one polarity. `includeHidden` — see
// `FindNthSlot`.
int FindSlotBySpellID(const uint8_t *unit, uint32_t spellID,
                      const Filter *filter, bool includeHidden = false);

// First populated slot at or after `fromSlot` matching `filter`, or -1 when
// the array holds no more. The enumeration primitive behind
// `C_UnitAuras.GetAuraSlots`: walking it is linear in the aura count, where
// repeated `FindNthSlot` calls re-walk from slot 0 for every index.
// `includeHidden` — see `FindNthSlot`.
int NextSlot(const uint8_t *unit, int fromSlot, Filter filter,
            bool includeHidden = false);

// Emits the aura in the populated slot at `unit + slot` onto the Lua stack:
// the modern-style `AuraData` table for `Emit::Table` (net stack +1), or the
// 15-value `UnitAura` tuple for `Emit::Positional` (net stack +15). Caller is
// responsible for having validated that the slot is populated.
//
// The positional tuple carries the same values as the table, in `UnitAura`
// order: name, icon, count, dispelType, duration, expirationTime,
// sourceUnit, isStealable, nameplateShowPersonal, spellId, canApplyAura,
// isBossDebuff, castByPlayer, nameplateShowAll, timeMod.
//
// Table fields carrying real data:
//   name, icon, applications, spellId, dispelName,
//   isHelpful, isHarmful, duration, expirationTime, sourceUnit,
//   isFromPlayerOrPlayerPet, isStealable, timeMod
//
// Modern-shape fields with no 3.3.5 source — emitted with the default the
// modern AuraData carries so consumers don't read nil:
//   points={}, isBossAura=false, isNameplateOnly=false,
//   nameplateShowAll=false, nameplateShowPersonal=false,
//   hideOnPartyFrames=false, canApplyAura=false, canActivePlayerDispel=false,
//   isRaid=false, isTankRoleAura=false, isHealerRoleAura=false,
//   isDPSRoleAura=false
//
// Omitted (nil): auraInstanceID — no per-application instance-ID system exists
// in 3.3.5, and none of its companion APIs do either.
void Push(void *L, const uint8_t *unit, int slot, Emit emit = Emit::Table);

// `Push` for a slot id that came from an enumeration and may since have gone
// stale: pushes the aura and returns true when the slot still holds a live
// one, or pushes nothing and returns false when it doesn't (caller pushes
// nil). Slot ids are plain aura-array indices, so the fetch is a direct read
// — no walk.
bool PushBySlot(void *L, const uint8_t *unit, int slot, Emit emit);

} // namespace Aura::Data
