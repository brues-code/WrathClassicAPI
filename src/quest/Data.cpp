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
#include "Offsets.h"
#include "event/Custom.h"
#include "net/Packet.h"
#include "tick/FrameTick.h"

#include <cstdint>
#include <unordered_map>
#include <vector>

namespace Quest::Data {

namespace {

constexpr const char *kQuestDataLoadResult = "QUEST_DATA_LOAD_RESULT";
const Event::Custom::AutoReserve _reserve{kQuestDataLoadResult};

void FireQuestDataLoadResult(int questID, int success) {
    const int eventID = Event::Custom::Lookup(kQuestDataLoadResult);
    Event::Custom::Fire(eventID, "%d%d", questID, success);
}

bool InWorld() {
    return *reinterpret_cast<const volatile uint8_t *>(
               static_cast<uintptr_t>(Offsets::VAR_IN_WORLD)) != 0;
}

// --- Requests we owe a completion event for -------------------------------
//
// The cache calls our callback when the server answers, with success or
// with the server's "no such quest" reply, so the callback is the single
// completion path. What the cache can't handle is a lost query: the server
// drops quest queries that arrive while the player isn't in the world
// (login and every loading screen), and the cache never asks again for an
// entry it already has. So, as for items (src/item/Data.cpp):
//
//   - A request made outside the world waits here, unsent, and goes out
//     on the next frame in the world.
//   - A query sent just before a loading screen can still reach the server
//     after it has taken the player out of the world. On the first frame
//     back in the world, anything sent and still not loaded is asked for
//     again with a bare CMSG_QUEST_QUERY; the response fills the existing
//     entry and runs its callbacks.

// questID -> sent (our callback is attached and the cache has the query).
std::unordered_map<uint32_t, bool> g_requests;
bool g_wasInWorld = false;

// Fires and forgets the request for `questID`, if we owe one. Erases before
// firing, so an OnEvent handler that requests the quest again starts fresh.
void Complete(uint32_t questID, bool success) {
    if (g_requests.erase(questID) == 0)
        return;
    FireQuestDataLoadResult(static_cast<int>(questID), success ? 1 : 0);
}

// See Cache::LoadCallback for the ABI.
void __cdecl OnQuestLoaded(uint32_t questID, const uint64_t *guid, void *param, int success) {
    (void)guid;
    (void)param;
    Complete(questID, (success & 0xFF) != 0);
}

void Send(uint32_t questID) {
    g_requests[questID] = true;
    // Loaded between the request and now (a response that was already on
    // its way): the cache won't call back for a loaded entry.
    if (Cache::Lookup(questID, &OnQuestLoaded) != nullptr)
        Complete(questID, true);
}

// Explicit-request path. If the quest is already cached, the engine won't
// invoke our callback — fire the event now so addons get the same
// notification regardless of cache state, matching modern semantics.
void Request(uint32_t questID) {
    if (Cache::Peek(questID) != nullptr) {
        FireQuestDataLoadResult(static_cast<int>(questID), 1);
        return;
    }
    const bool sent = g_requests.try_emplace(questID, false).first->second;
    if (!sent && InWorld())
        Send(questID);
}

std::vector<uint32_t> RequestIDs(bool sent) {
    std::vector<uint32_t> ids;
    for (const auto &[questID, isSent] : g_requests)
        if (isSent == sent)
            ids.push_back(questID);
    return ids;
}

// On the first frame in the world: re-ask for queries a loading screen
// swallowed, then send everything that waited.
void OnTick() {
    const bool inWorld = InWorld();
    const bool entered = inWorld && !g_wasInWorld;
    g_wasInWorld = inWorld;
    if (!entered)
        return;
    for (const uint32_t questID : RequestIDs(/*sent=*/true)) {
        if (Cache::Peek(questID) != nullptr)
            Complete(questID, true);
        else
            Net::Packet::SendUint32(Offsets::OPCODE_CMSG_QUEST_QUERY, questID);
    }
    for (const uint32_t questID : RequestIDs(/*sent=*/false))
        Send(questID);
}

const Tick::FrameTick::AutoSubscribe _tickSub{&OnTick};

// The Lua state is going away (/reload or logout), and with it every
// listener for the events we owe. A callback still attached to a cache
// entry finds nothing here and does nothing; a request for the same quest
// from the new UI reuses it.
void PrepareForReload() { g_requests.clear(); }

const Game::ReloadAutoRegister _reload{&PrepareForReload};

int __cdecl Script_RequestLoadQuestByID(void *L) {
    if (!Game::Lua::IsNumber(L, 1))
        return Game::Lua::Error(L,
            "Usage: C_QuestLog.RequestLoadQuestByID(questID)");
    const int questID = static_cast<int>(Game::Lua::ToNumber(L, 1));
    if (questID <= 0)
        return 0;
    Request(static_cast<uint32_t>(questID));
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
