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

// `C_QuestLog.RequestLoadQuestByID(questID)` + the modern
// `QUEST_DATA_LOAD_RESULT(questID, success)` event. Sibling of the
// item-data plumbing in `src/item/Data.cpp` — same pattern, different
// cache.
//
// 3.3.5's `Script_GetQuestLink` already kicks off `CMSG_QUEST_QUERY`
// for uncached quests via the engine's `_GetRecord` callback slot;
// what we add is (a) the modern Lua entry point, and (b) the
// completion event that fires after `SMSG_QUEST_QUERY_RESPONSE` lands.
// Neither exists in 3.3.5 natively.

#include "Cache.h"

#include "Game.h"
#include "event/Custom.h"

#include <cstdint>

namespace Quest::Data {

namespace {

constexpr const char *kQuestDataLoadResult = "QUEST_DATA_LOAD_RESULT";
const Event::Custom::AutoReserve _reserve{kQuestDataLoadResult};

void FireQuestDataLoadResult(int questID, int success) {
    const int eventID = Event::Custom::Lookup(kQuestDataLoadResult);
    Event::Custom::Fire(eventID, "%d%d", questID, success);
}

// `__stdcall(userData, success)` — same shape as the item cache
// callback. The engine's invocation pattern from FUN_0067DE90 is to
// push (userData, success) and `call [entry+0x18]` with the cdecl-
// to-stdcall convention (the callback's `ret 8` cleans the args).
// We encode the questID into `userData` at request time so the
// callback knows which quest completed.
void __stdcall QuestLoadCallback(void *userData, int success) {
    const auto questID = static_cast<int>(reinterpret_cast<uintptr_t>(userData));
    FireQuestDataLoadResult(questID, success != 0 ? 1 : 0);
}

using QuestLoadCallback_t = void(__stdcall *)(void *userData, int success);

// Explicit-request path. Kicks off the cache fill via the callback
// above (which fires QUEST_DATA_LOAD_RESULT on response). If the
// quest is already cached, the engine won't invoke our callback —
// synthesize the event so addons get the same notification
// regardless of cache state, matching modern semantics.
void RequestAndMaybeNotify(uint32_t questID) {
    const bool wasCached = (Cache::Peek(questID) != nullptr);
    void *userData = reinterpret_cast<void *>(static_cast<uintptr_t>(questID));
    void *cb = reinterpret_cast<void *>(&QuestLoadCallback);
    Cache::Lookup(questID, cb, userData);
    if (wasCached)
        FireQuestDataLoadResult(static_cast<int>(questID), 1);
}

int __cdecl Script_RequestLoadQuestByID(void *L) {
    if (!Game::Lua::IsNumber(L, 1))
        return Game::Lua::Error(L,
            "Usage: C_QuestLog.RequestLoadQuestByID(questID)");
    const int questID = static_cast<int>(Game::Lua::ToNumber(L, 1));
    if (questID <= 0)
        return 0;
    RequestAndMaybeNotify(static_cast<uint32_t>(questID));
    return 0;
}

void RegisterLuaFunctions() {
    Game::Lua::RegisterTableFunction("C_QuestLog", "RequestLoadQuestByID",
                                     &Script_RequestLoadQuestByID);
    // QUEST_DATA_LOAD_RESULT name reserved at file scope via AutoReserve.
}

const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace
} // namespace Quest::Data
