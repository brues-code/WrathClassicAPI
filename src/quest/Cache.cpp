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

#include "Cache.h"

#include "Offsets.h"

#include <cstdint>

namespace Quest::Cache {

// `__thiscall(this=cache, questID, const WOWGUID &guid, callback, param, bool unique)`.
// `guid` and `param` are stored on the callback node and handed back to the
// callback; we need neither, so pass a zero GUID and no param.
using GetQuestRecord_t = const uint8_t *(__thiscall *)(void *cache, uint32_t questID,
                                                       const uint64_t *guid,
                                                       LoadCallback callback, void *param,
                                                       int unique);

const uint8_t *Lookup(uint32_t questID, LoadCallback callback) {
    auto fn = reinterpret_cast<GetQuestRecord_t>(
        static_cast<uintptr_t>(Offsets::FUN_DBCACHE_QUEST_GET_RECORD));
    auto *cache = reinterpret_cast<void *>(
        static_cast<uintptr_t>(Offsets::VAR_QUEST_CACHE));
    const uint64_t zeroGuid = 0;
    return fn(cache, questID, &zeroGuid, callback, nullptr, /*unique=*/1);
}

} // namespace Quest::Cache
