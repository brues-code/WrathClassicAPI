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

// `C_QuestLog.GetTitleForQuestID(questID)` — reads the locale-applied
// title from the engine's quest static-info cache. Returns nil if
// the cache hasn't loaded the record yet; addons should pair with
// `C_QuestLog.RequestLoadQuestByID(questID)` and listen for
// `QUEST_DATA_LOAD_RESULT` to know when to retry. This is the same
// contract modern WoW exposes — the title-only getter doesn't auto-
// warm the cache, unlike the chattier `GetQuestLink` path.

#include "Cache.h"

#include "Game.h"
#include "Offsets.h"

#include <cstdint>

namespace Quest::Title {

namespace {

int __cdecl Script_GetTitleForQuestID(void *L) {
    if (!Game::Lua::IsNumber(L, 1))
        return Game::Lua::Error(L,
            "Usage: C_QuestLog.GetTitleForQuestID(questID)");
    const int questID = static_cast<int>(Game::Lua::ToNumber(L, 1));
    if (questID <= 0)
        return 0;

    const uint8_t *data = Cache::Peek(static_cast<uint32_t>(questID));
    if (data == nullptr)
        return 0; // not cached — caller should RequestLoadQuestByID

    const char *title = reinterpret_cast<const char *>(data + Offsets::OFF_QUEST_TITLE);
    if (*title == '\0')
        return 0; // empty record (deleted / placeholder) → nil
    Game::Lua::PushString(L, title);
    return 1;
}

void RegisterLuaFunctions() {
    Game::Lua::RegisterTableFunction("C_QuestLog", "GetTitleForQuestID",
                                     &Script_GetTitleForQuestID);
}

const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace
} // namespace Quest::Title
