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

namespace Spell::Arg {

// Resolves a Lua arg at 1-based stack `idx` to a spellID, matching retail's
// `spellIdentifier` forms:
//   - number                          → spellID
//   - "|c…|Hspell:N…|h…" / "spell:N"   → ID parsed from the link
//   - "12345" (numeric string)        → atoi → spellID
//   - "name" / "name(subtext)"        → spellbook name lookup (highest known
//                                        rank, or the exact rank when a subtext
//                                        like "(Rank 4)" is given)
// A localized name resolves only if the spell is in the player's or pet's
// spellbook (the engine's name index is spellbook-scoped). Returns 0 for nil,
// tables, an empty string, or a name the spellbook doesn't contain.
int ResolveSpellID(void *L, int idx);

// Standalone name → spellID resolver — the engine's spellbook name index
// (honors a trailing "(Rank N)"). Returns 0 if `name` isn't in the player's or
// pet's spellbook.
int NameToSpellID(const char *name);

} // namespace Spell::Arg
