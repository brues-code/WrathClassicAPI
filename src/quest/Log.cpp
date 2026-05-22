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

// `C_QuestLog.GetQuestIDForLogIndex(questLogIndex)` and
// `C_QuestLog.ReadyForTurnIn(questID)` — modern WoW backports. Both
// read the same engine-side quest log array that
// `Script_GetQuestLogTitle` (FUN_005E5CC0) walks. The log holds
// quests AND category headers; helpers below skip headers (`+0x08
// != 0`) so callers see modern questID-keyed semantics.

#include "Game.h"
#include "Offsets.h"

#include <cstdint>

namespace {
using IsQuestCompletable_t = bool(__cdecl *)(uint32_t logIndex0, char strict);
}

namespace Quest::Log {

namespace {

// Returns base pointer of the active entry array. Used by both
// helpers below; kept as a one-liner to keep the call sites tidy.
inline const uint8_t *EntriesBase() {
    return reinterpret_cast<const uint8_t *>(Offsets::VAR_QUEST_LOG_ENTRIES);
}

inline int EntryCount() {
    return *reinterpret_cast<const int *>(Offsets::VAR_QUEST_LOG_COUNT);
}

inline const uint8_t *EntryAt(int index0) {
    return EntriesBase() + static_cast<size_t>(index0) * Offsets::QUEST_LOG_ENTRY_STRIDE;
}

inline bool IsHeader(const uint8_t *entry) {
    return *reinterpret_cast<const uint32_t *>(entry + Offsets::OFF_QUEST_LOG_IS_HEADER) != 0;
}

int __cdecl Script_GetQuestIDForLogIndex(void *L) {
    if (!Game::Lua::IsNumber(L, 1))
        return Game::Lua::Error(L,
            "Usage: C_QuestLog.GetQuestIDForLogIndex(questLogIndex)");
    const int index1 = static_cast<int>(Game::Lua::ToNumber(L, 1));
    const int index0 = index1 - 1;
    if (index0 < 0 || index0 >= EntryCount())
        return 0;

    const uint8_t *entry = EntryAt(index0);
    if (IsHeader(entry))
        return 0;

    const uint32_t questID =
        *reinterpret_cast<const uint32_t *>(entry + Offsets::OFF_QUEST_LOG_QUEST_ID);
    if (questID == 0)
        return 0;
    Game::Lua::PushNumber(L, static_cast<double>(questID));
    return 1;
}

// `C_QuestLog.ReadyForTurnIn(questID)` — returns true iff the
// quest is in the player's log AND ready to hand in. Two-step check:
// first the inline "server marked complete" flag (`+0x0C`); for
// quests where that flag never fires (zero-objective "talk to NPC"
// style), fall back to the engine's `IsQuestCompletable` helper
// with `strict=0`. The engine itself uses strict=1 inside
// `Script_GetQuestLogTitle` because it only wants to *display*
// the completion icon for server-confirmed quests, but our
// `ReadyForTurnIn` matches retail semantics: would-this-quest-
// hand-in-now, which strict=0 evaluates via objective progress.
int __cdecl Script_ReadyForTurnIn(void *L) {
    if (!Game::Lua::IsNumber(L, 1))
        return Game::Lua::Error(L,
            "Usage: C_QuestLog.ReadyForTurnIn(questID)");
    const int questID = static_cast<int>(Game::Lua::ToNumber(L, 1));
    if (questID <= 0) {
        Game::Lua::PushBool(L, false);
        return 1;
    }

    const int count = EntryCount();
    for (int i = 0; i < count; ++i) {
        const uint8_t *entry = EntryAt(i);
        if (IsHeader(entry))
            continue;
        const uint32_t entryID =
            *reinterpret_cast<const uint32_t *>(entry + Offsets::OFF_QUEST_LOG_QUEST_ID);
        if (static_cast<int>(entryID) != questID)
            continue;
        const uint32_t isComplete =
            *reinterpret_cast<const uint32_t *>(entry + Offsets::OFF_QUEST_LOG_IS_COMPLETE);
        if (isComplete != 0) {
            Game::Lua::PushBool(L, true);
            return 1;
        }
        auto fn = reinterpret_cast<IsQuestCompletable_t>(Offsets::FUN_QUEST_IS_COMPLETABLE);
        const bool completable = fn(static_cast<uint32_t>(i), 0);
        Game::Lua::PushBool(L, completable);
        return 1;
    }
    Game::Lua::PushBool(L, false);
    return 1;
}

void RegisterLuaFunctions() {
    Game::Lua::RegisterTableFunction("C_QuestLog", "GetQuestIDForLogIndex",
                                     &Script_GetQuestIDForLogIndex);
    Game::Lua::RegisterTableFunction("C_QuestLog", "ReadyForTurnIn",
                                     &Script_ReadyForTurnIn);
}

const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace
} // namespace Quest::Log
