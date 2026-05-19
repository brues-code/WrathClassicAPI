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

namespace Item::Arg {

// Resolved item-arg shape. `itemID` covers number and link inputs;
// `name` is set only when the arg was a string with no recognizable
// link, for callers that support name-based matching. Callers that
// only want the itemID can ignore `name` and treat `itemID == 0` as
// "no usable input".
struct Resolved {
    int itemID;
    const char *name;
};

// Resolves a Lua arg at 1-based stack `idx` to an item reference.
// Accepts:
//   - number              → itemID
//   - "item:N..." (bare)  → ID parsed after `item:`
//   - "|cff…|Hitem:N…|h…" → ID parsed after the embedded `item:`
//   - "12345" (numeric)   → atoi → itemID
//   - any other string    → returned via `name` for name-match callers
// Returns `{0, nullptr}` for nil, tables, or other unsupported types.
Resolved Resolve(void *L, int idx);

// Convenience: itemID only, dropping any name info. Equivalent to
// `Resolve(L, idx).itemID`. Suitable for callers that don't support
// name-based matching.
int ResolveItemID(void *L, int idx);

} // namespace Item::Arg
