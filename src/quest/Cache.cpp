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

#include "Cache.h"

#include "Offsets.h"

#include <cstdint>

namespace Quest::Cache {

// `__thiscall(this=cache, questID, *outBuf, callback, userData, char unused)`.
// `outBuf` is engine bookkeeping (8 bytes get stored into the pending
// entry — see FUN_0067DE90 `piVar3[4] = *param_2; piVar3[5] = param_2[1];`).
// We pass an 8-byte zero block; the engine doesn't actually consume the
// data for our use case, it's just a side-channel for paths that want
// to associate a GUID/secondary key with the request.
using GetQuestRecord_t = const uint8_t *(__thiscall *)(void *cache, uint32_t questID,
                                                       const uint64_t *outBuf,
                                                       void *callback, void *userData,
                                                       int unused);

const uint8_t *Lookup(uint32_t questID, void *callback, void *userData) {
    auto fn = reinterpret_cast<GetQuestRecord_t>(
        static_cast<uintptr_t>(Offsets::FUN_DBCACHE_QUEST_GET_RECORD));
    auto *cache = reinterpret_cast<void *>(
        static_cast<uintptr_t>(Offsets::VAR_QUEST_CACHE));
    const uint64_t outBuf = 0;
    return fn(cache, questID, &outBuf, callback, userData, 0);
}

} // namespace Quest::Cache
