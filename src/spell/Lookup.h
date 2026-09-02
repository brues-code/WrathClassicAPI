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

// `Spell::Lookup::*` — shared readers for the engine's Spell.dbc and its
// indirected sub-tables (SpellIcon.dbc, SpellDispelType.dbc). Consolidates the
// record-copy + sub-record lookups that the aura, item, and spell modules all
// need, so the Spell.dbc access lives in one place.
namespace Spell::Lookup {

// Copies the Spell.dbc record for `spellID` into `out`, which must be at least
// `Offsets::SPELL_DBC_RECORD_SIZE` bytes. Returns true on hit. Always routes
// through the engine's record-copy helper: the engine keeps two record "shapes"
// (locale-split vs pre-resolved single-string), and the helper normalizes both
// into the buffer layout the `OFF_SPELL_*` offsets target — so callers read
// fields off `out` unconditionally.
bool CopyRecord(uint32_t spellID, uint8_t *out);

// SpellIcon.dbc texture path for an icon ID — a full "Interface\\Icons\\…" path
// on 3.3.5, usable directly with texture:SetTexture. nullptr for a missing /
// empty row.
const char *IconPath(uint32_t iconID);

// SpellDispelType.dbc display name for a dispel-type ID, or nullptr when the row
// carries no displayable name (the engine's +0x0C "has name" sentinel is 0).
const char *DispelTypeName(uint32_t dispelID);

// SpellCastTimes.dbc base cast time (ms) for a casting-time index — the flat
// base, ignoring per-skill scaling. 0 for an out-of-range index (and for the
// instant-cast row).
int CastTimeMs(uint32_t castTimeIndex);

// SpellRange.dbc min/max range (yards) for a range index. Writes the pair via
// the out-params and returns true on hit; writes 0/0 and returns false for an
// out-of-range index.
bool Range(uint32_t rangeIndex, float *minRange, float *maxRange);

// Convenience: localized name for a spellID, or nullptr if the spell is unknown
// or has no name in the current locale.
const char *NameForSpell(uint32_t spellID);

// Convenience: full icon texture path for a spellID (copies the record, reads
// its SpellIcon ID, resolves the path), or nullptr if either the spell or its
// icon row is missing.
const char *IconPathForSpell(uint32_t spellID);

} // namespace Spell::Lookup
