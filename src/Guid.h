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

#include <cstddef>
#include <cstdint>
#include <cstdio>

// Shared GUID primitives — parsing / formatting a hex GUID string and resolving
// a GUID to an in-world object. Anything that takes a GUID string
// (UnitNameFromGUID, UnitTokenFromGUID, C_Item's GUID form, C_FriendList, …)
// funnels through here instead of re-declaring the engine's HexString2Guid /
// ObjectMgr::Get thunks.

namespace Guid {

// A 64-bit GUID split into the low/high dwords the engine's by-GUID functions
// take. `valid()` is the engine's own "guid != 0" test.
struct Pair {
    uint32_t lo = 0;
    uint32_t hi = 0;
    bool valid() const { return lo != 0 || hi != 0; }
    uint64_t value() const { return (static_cast<uint64_t>(hi) << 32) | lo; }
};

inline Pair Split(uint64_t g) {
    return {static_cast<uint32_t>(g), static_cast<uint32_t>(g >> 32)};
}

// True when `s` carries the `"0x"` prefix of a GUID string (the form UnitGUID
// returns). The gate for APIs that accept either a GUID string or a character
// name: the engine's hex parser happily reads a name made of hex letters
// ("Abe", "Dad") as a number, so the prefix — not parse success — decides.
inline bool IsGuidString(const char *s) {
    return s != nullptr && s[0] == '0' && (s[1] == 'x' || s[1] == 'X');
}

// Minimum buffer size for `Format`'s output — "0x" + 16 hex digits + NUL.
constexpr size_t STRING_SIZE = 19;

// Formats `guid` as "0xHHHHHHHHLLLLLLLL" (high dword then low, upper-case hex) —
// the form UnitGUID returns and Parse accepts. `cap` must be at least
// STRING_SIZE. Returns `buf` for chaining into PushString.
inline const char *Format(Pair guid, char *buf, size_t cap) {
    std::snprintf(buf, cap, "0x%08X%08X", guid.hi, guid.lo);
    return buf;
}

// Parse a hex GUID string ("0xHHHHHHHHLLLLLLLL", or bare hex) via the engine's
// HexString2Guid. Returns {0, 0} for null / empty / non-hex input.
inline Pair Parse(const char *s) {
    if (s == nullptr || s[0] == '\0')
        return {};
    using Fn = uint64_t(__cdecl *)(const char *);
    const uint64_t g = reinterpret_cast<Fn>(Offsets::FUN_HEXSTRING_TO_GUID)(s);
    return {static_cast<uint32_t>(g), static_cast<uint32_t>(g >> 32)};
}

// Resolve `guid` to its in-world object matching `typeMask` (an OBJ_FLAGS_*
// value), or nullptr if the GUID isn't in the object manager or isn't that type.
inline void *ResolveObject(Pair guid, int typeMask) {
    using Fn = void *(__cdecl *)(uint32_t lo, uint32_t hi, int mask);
    return reinterpret_cast<Fn>(Offsets::FUN_OBJECT_RESOLVE_BY_GUID)(guid.lo, guid.hi, typeMask);
}

} // namespace Guid
