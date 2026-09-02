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

#include <cstdint>

// The client's player name cache — the store behind GetPlayerInfoByGUID, holding
// a record for every player the client has learned about this session (group,
// chat, combat, /who, friends, …) whether or not they are in view. Shared by
// UnitNameFromGUID and C_FriendList.

namespace Unit::NameCache {

// FUN_PLAYER_NAME_CACHE_GET — `__thiscall DBCache::GetRecord` on the cache
// object at VAR_PLAYER_NAME_CACHE; see Offsets.h for the exact call shape.
using Get_t = const uint8_t *(__thiscall *)(void *cache, uint32_t guidLo, uint32_t guidHi,
                                            uint32_t *scratch, uint32_t a5, uint32_t a6,
                                            char a7);

// The cached record for a player GUID, or null if the client has none. Pure
// lookup — null callback / flags, so a miss issues no name query and has no
// side effects. Record fields: name inline @OFF_PLAYER_NAME_REC_NAME, realm
// inline @OFF_PLAYER_NAME_REC_REALM, gender @OFF_PLAYER_NAME_REC_GENDER, class
// ID @OFF_PLAYER_NAME_REC_CLASS.
inline const uint8_t *Record(uint32_t guidLo, uint32_t guidHi) {
    uint32_t scratch[2] = {0, 0};
    auto *cache = reinterpret_cast<void *>(Offsets::VAR_PLAYER_NAME_CACHE);
    return reinterpret_cast<Get_t>(Offsets::FUN_PLAYER_NAME_CACHE_GET)(cache, guidLo, guidHi,
                                                                       scratch, 0, 0, 0);
}

} // namespace Unit::NameCache
