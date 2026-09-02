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

#include "Guid.h"
#include "Offsets.h"

#include <cstdint>

namespace FriendList {

// Resolve a `token` argument to a GUID: a "0x…" GUID string parses directly;
// anything else goes through the engine's own name / unit-token resolver
// (`Offsets::FUN_NAME_TO_GUID`, the one its IsIgnored uses) — the player name
// cache by name, else a unit token like "target". Invalid (zero) when nothing
// matches, e.g. a name the client has never seen.
inline Guid::Pair ResolveToken(const char *token) {
    if (Guid::IsGuidString(token))
        return Guid::Parse(token);
    using NameToGuid_t = uint8_t(__cdecl *)(const char *s, uint32_t *out);
    uint32_t out[2] = {0, 0};
    if (reinterpret_cast<NameToGuid_t>(static_cast<uintptr_t>(Offsets::FUN_NAME_TO_GUID))(
            token, out) == 0)
        return {};
    return {out[0], out[1]};
}

} // namespace FriendList
