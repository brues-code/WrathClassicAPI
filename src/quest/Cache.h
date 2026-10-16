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

#include <cstdint>

namespace Quest::Cache {

// Quest-load callback — `__cdecl(questID, guid, param, success)`, 4 args /
// 16 bytes, caller-cleaned (DBCache<QuestCache>::AddItem's dispatch at
// 0x0067E09E: PUSH 1; PUSH param; PUSH guidPtr; PUSH questID; CALL EDX;
// ADD ESP, 0x10 — DenyItem at 0x0067A6A3 pushes 0). `success` is a C++
// bool, so only its low byte is defined.
using LoadCallback = void(__cdecl *)(uint32_t questID, const uint64_t *guid, void *param,
                                     int success);

// Calls the engine's quest cache `GetRecord` (`FUN_DBCACHE_QUEST_GET_RECORD`).
//
// With `callback == nullptr` (use `Peek`), performs only the hash-table
// lookup; returns the cached record when loaded, or nullptr otherwise.
//
// With a non-null `callback`, attaches it to the entry (once — `unique`
// skips a callback/param pair already attached) and, for a new entry, sends
// `CMSG_QUEST_QUERY`. The server's response runs it with success = true, or
// false for a quest that doesn't exist. When the record is already loaded,
// returns it immediately and does NOT invoke the callback; the caller
// synthesizes any "fire the result event on a cache hit" notification.
const uint8_t *Lookup(uint32_t questID, LoadCallback callback);

inline const uint8_t *Peek(uint32_t questID) {
    return Lookup(questID, nullptr);
}

} // namespace Quest::Cache
