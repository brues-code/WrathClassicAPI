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

#include "Offsets.h"
#include "unit/Resolve.h"

#include <cstdint>

// Shared world-position read for any CGObject-derived unit. The position comes
// from the object's `GetPosition` virtual (vtable slot at
// OFF_CGOBJECT_VTBL_GET_POSITION) — the same one `CheckInteractDistance` uses.
// Header-only so callers (UnitPosition, UnitDistanceSquared) don't each re-derive
// the vtable-slot math.

namespace Unit::Position {

using GetPosition_t = float *(__thiscall *)(void *self, float out[3]);

// Reads `obj`'s world position into `out[3]` — engine C3Vector order
// {x = north, y = west, z = up}. Calls `obj->vtable[slot](out)`; the returned
// float* may be `out` (object filled it) or an internal cached field, so copy
// from whichever it hands back. Returns false for a null object or a unit with
// no known position yet.
inline bool Read(void *obj, float out[3]) {
    if (obj == nullptr)
        return false;
    auto **vtable = *reinterpret_cast<void ***>(obj);
    auto fn = reinterpret_cast<GetPosition_t>(
        vtable[Offsets::OFF_CGOBJECT_VTBL_GET_POSITION / 4]);
    float *p = fn(obj, out);
    if (p == nullptr)
        return false;
    if (p != out) {
        out[0] = p[0];
        out[1] = p[1];
        out[2] = p[2];
    }
    return true;
}

// Convenience: resolve a token and read its position in one step.
inline bool ReadToken(const char *token, float out[3]) {
    return Read(Unit::ResolveToken(token), out);
}

} // namespace Unit::Position
