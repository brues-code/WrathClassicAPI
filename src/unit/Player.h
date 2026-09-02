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

namespace Unit {

// The local player's CGPlayer object — the pointer the engine's `__thiscall`
// player helpers (cost calculators, skill lookups, …) expect. Resolved the same
// way the engine's own script functions do: the local-player GUID from the
// thread-local ObjectMgr slot, looked up with the PLAYER type mask. Returns
// nullptr before entering the world.
inline void *LocalPlayer() {
    using GetGuid_t = uint64_t(__cdecl *)();
    const uint64_t g = reinterpret_cast<GetGuid_t>(
        static_cast<uintptr_t>(Offsets::FUN_LOCAL_PLAYER_GUID))();
    Guid::Pair guid{static_cast<uint32_t>(g), static_cast<uint32_t>(g >> 32)};
    if (!guid.valid())
        return nullptr;
    return Guid::ResolveObject(guid, Offsets::OBJ_FLAGS_PLAYER);
}

} // namespace Unit
