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

#include <cstdint>

namespace Quest::Cache {

// Calls the engine's quest cache `_GetRecord`
// (`FUN_DBCACHE_QUEST_GET_RECORD`). Same five-arg shape as the item
// cache.
//
// With `callback == nullptr` (use `Peek`), performs only the hash-
// table lookup; returns the cached data block (`entry + 0x18` per
// the engine's accessor) when loaded, or nullptr otherwise.
//
// With a non-null `callback`, queues a `CMSG_QUEST_QUERY` when the
// data isn't already loaded and registers the supplied callback
// (`__stdcall(userData, success)`, called once `SMSG_QUEST_QUERY_RESPONSE`
// lands). When the data is already loaded, returns the record
// immediately and does NOT invoke the callback — same as the item
// cache. Caller synthesizes any "fire the result event on a cache
// hit" notification.
const uint8_t *Lookup(uint32_t questID, void *callback, void *userData);

inline const uint8_t *Peek(uint32_t questID) {
    return Lookup(questID, nullptr, nullptr);
}

} // namespace Quest::Cache
