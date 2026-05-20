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

// `C_GossipInfo.*` — modern table-shaped wrappers around 3.3.5's
// flat gossip surface (`GetGossipText`, `GetGossipOptions`,
// `GetGossip*Quests`, `SelectGossip*`). The underlying data lives
// in two engine arrays — see `Offsets.h` for the full layout. We
// read the same fields the engine's own `Script_GetGossip*` reads,
// then shape the output as the modern API's `GossipOptionUIInfo`
// / `GossipQuestUIInfo` tables.
//
// Selectors translate the modern arg shape (`gossipOptionID` /
// `questID`) back to the engine's 0-based slot / filtered-index
// and call the engine's own helpers directly, so we share the
// engine's CMSG-send path and error semantics.
//
// Missing modern fields — 3.3.5's wire protocol simply doesn't
// transmit them, so there's nothing to surface:
//   - `rewards`, `spellID` (post-WotLK feature)
//   - per-option `status` (Available/Unavailable/Locked/Complete —
//     introduced with the post-WotLK quest-tracking rework)
//   - `overrideIconID`, `selectOptionWhenOnlyOption` (modern UX hints)

#include "Game.h"
#include "Offsets.h"

#include <cstdint>

namespace Gossip::Info {

namespace {

const uint8_t *OptionEntry(int slot) {
    return reinterpret_cast<const uint8_t *>(
        static_cast<uintptr_t>(Offsets::VAR_GOSSIP_OPTIONS) +
        static_cast<uintptr_t>(slot) *
            static_cast<uintptr_t>(Offsets::GOSSIP_OPTIONS_STRIDE));
}

const uint8_t *QuestEntry(int slot) {
    return reinterpret_cast<const uint8_t *>(
        static_cast<uintptr_t>(Offsets::VAR_GOSSIP_QUESTS) +
        static_cast<uintptr_t>(slot) *
            static_cast<uintptr_t>(Offsets::GOSSIP_QUESTS_STRIDE));
}

int32_t OptionIndex(const uint8_t *entry) {
    return *reinterpret_cast<const int32_t *>(
        entry + Offsets::OFF_GOSSIP_OPTION_INDEX);
}

uint32_t QuestID(const uint8_t *entry) {
    return *reinterpret_cast<const uint32_t *>(
        entry + Offsets::OFF_GOSSIP_QUEST_ID);
}

uint32_t QuestStatus(const uint8_t *entry) {
    return *reinterpret_cast<const uint32_t *>(
        entry + Offsets::OFF_GOSSIP_QUEST_STATUS);
}

// Engine's filter: status 3 or 4 = active (in player's log); any
// other non-zero status = available (offered but not yet taken).
// Matches both engine count helpers (FUN_0058A5D0 / FUN_0058A6C0).
bool IsActiveQuest(uint32_t status) {
    return status == Offsets::GOSSIP_QUEST_STATUS_ACTIVE ||
           status == Offsets::GOSSIP_QUEST_STATUS_COMPLETE;
}

int __cdecl Script_GetText(void *L) {
    Game::Lua::PushString(L, reinterpret_cast<const char *>(
        static_cast<uintptr_t>(Offsets::VAR_GOSSIP_GREETING_TEXT)));
    return 1;
}

// Modern shape per option:
//   gossipOptionID  — engine's `optionIndex` (the same value the
//                     server expects for `SelectGossipOption`).
//   name            — option text.
//   icon            — engine's gossip-type byte (0..N: gossip/vendor/
//                     taxi/trainer/healer/binder/banker/petition/
//                     tabard/battlemaster/auctioneer/...). Same
//                     numeric values 1.12 emitted; modern WoW uses
//                     fileIDs but addons that care typically remap.
//   flags           — bit 0 = boxCoded (password protected).
//   moneyCost       — copper required to take the option (3.3.5
//                     addition; was always 0 in 1.12).
//   orderIndex      — 1-based display position. Matches what
//                     `SelectOptionByIndex` accepts.
int __cdecl Script_GetOptions(void *L) {
    Game::Lua::SetTop(L, 0);
    Game::Lua::NewTable(L);

    int outIdx = 0;
    for (int slot = 0; slot < Offsets::GOSSIP_OPTIONS_MAX; ++slot) {
        const uint8_t *entry = OptionEntry(slot);
        if (OptionIndex(entry) < 0)
            continue;
        ++outIdx;

        Game::Lua::PushNumber(L, static_cast<double>(outIdx));
        Game::Lua::NewTable(L);

        Game::Lua::SetFieldNumber(L, "gossipOptionID",
            static_cast<double>(OptionIndex(entry)));
        Game::Lua::SetFieldString(L, "name", reinterpret_cast<const char *>(
            entry + Offsets::OFF_GOSSIP_OPTION_NAME));
        Game::Lua::SetFieldNumber(L, "icon",
            static_cast<double>(*reinterpret_cast<const uint32_t *>(
                entry + Offsets::OFF_GOSSIP_OPTION_ICON)));
        const uint32_t flags = *reinterpret_cast<const uint32_t *>(
            entry + Offsets::OFF_GOSSIP_OPTION_FLAGS);
        Game::Lua::SetFieldNumber(L, "flags", static_cast<double>(flags));
        Game::Lua::SetFieldNumber(L, "moneyCost",
            static_cast<double>(*reinterpret_cast<const uint32_t *>(
                entry + Offsets::OFF_GOSSIP_OPTION_MONEY_COST)));
        Game::Lua::SetFieldNumber(L, "orderIndex",
            static_cast<double>(outIdx));

        Game::Lua::RawSet(L, -3);
    }
    return 1;
}

// Shared body for `GetAvailableQuests` / `GetActiveQuests`. Walks
// the unified backing array; `wantActive` inverts the status filter.
int PushQuestList(void *L, bool wantActive) {
    Game::Lua::SetTop(L, 0);
    Game::Lua::NewTable(L);

    int outIdx = 0;
    for (int slot = 0; slot < Offsets::GOSSIP_QUESTS_MAX; ++slot) {
        const uint8_t *entry = QuestEntry(slot);
        const uint32_t questID = QuestID(entry);
        if (questID == 0)
            break;
        const uint32_t status = QuestStatus(entry);
        if (IsActiveQuest(status) != wantActive)
            continue;
        ++outIdx;

        Game::Lua::PushNumber(L, static_cast<double>(outIdx));
        Game::Lua::NewTable(L);

        Game::Lua::SetFieldNumber(L, "questID", static_cast<double>(questID));
        Game::Lua::SetFieldString(L, "title", reinterpret_cast<const char *>(
            entry + Offsets::OFF_GOSSIP_QUEST_TITLE));
        Game::Lua::SetFieldNumber(L, "questLevel",
            static_cast<double>(*reinterpret_cast<const int32_t *>(
                entry + Offsets::OFF_GOSSIP_QUEST_LEVEL)));
        const uint32_t flags = *reinterpret_cast<const uint32_t *>(
            entry + Offsets::OFF_GOSSIP_QUEST_FLAGS);
        Game::Lua::SetFieldBool(L, "repeatable",
            (flags & Offsets::GOSSIP_QUEST_FLAG_REPEATABLE) != 0);
        if (wantActive)
            Game::Lua::SetFieldBool(L, "isComplete",
                status == Offsets::GOSSIP_QUEST_STATUS_COMPLETE);

        Game::Lua::RawSet(L, -3);
    }
    return 1;
}

int __cdecl Script_GetAvailableQuests(void *L) {
    return PushQuestList(L, /*wantActive=*/false);
}

int __cdecl Script_GetActiveQuests(void *L) {
    return PushQuestList(L, /*wantActive=*/true);
}

int CountOptions() {
    int count = 0;
    for (int slot = 0; slot < Offsets::GOSSIP_OPTIONS_MAX; ++slot) {
        if (OptionIndex(OptionEntry(slot)) >= 0)
            ++count;
    }
    return count;
}

int CountQuests(bool wantActive) {
    int count = 0;
    for (int slot = 0; slot < Offsets::GOSSIP_QUESTS_MAX; ++slot) {
        const uint8_t *entry = QuestEntry(slot);
        if (QuestID(entry) == 0)
            break;
        if (IsActiveQuest(QuestStatus(entry)) == wantActive)
            ++count;
    }
    return count;
}

int __cdecl Script_GetNumOptions(void *L) {
    Game::Lua::PushNumber(L, static_cast<double>(CountOptions()));
    return 1;
}

int __cdecl Script_GetNumAvailableQuests(void *L) {
    Game::Lua::PushNumber(L, static_cast<double>(CountQuests(false)));
    return 1;
}

int __cdecl Script_GetNumActiveQuests(void *L) {
    Game::Lua::PushNumber(L, static_cast<double>(CountQuests(true)));
    return 1;
}

// Engine helpers — see Offsets.h for the signature derivations.
using SelectOption_t = void(__cdecl *)(uint32_t slot0Based,
                                        const char *password,
                                        int copperCost);
using SelectQuest_t = void(__cdecl *)(int filteredIdx0Based);

void EngineSelectOption(int slot0Based, const char *password,
                        int copperCost) {
    auto fn = reinterpret_cast<SelectOption_t>(
        static_cast<uintptr_t>(Offsets::FUN_GOSSIP_SELECT_OPTION));
    fn(static_cast<uint32_t>(slot0Based), password, copperCost);
}

void EngineSelectAvailableQuest(int idx0Based) {
    auto fn = reinterpret_cast<SelectQuest_t>(
        static_cast<uintptr_t>(Offsets::FUN_GOSSIP_SELECT_AVAILABLE_QUEST));
    fn(idx0Based);
}

void EngineSelectActiveQuest(int idx0Based) {
    auto fn = reinterpret_cast<SelectQuest_t>(
        static_cast<uintptr_t>(Offsets::FUN_GOSSIP_SELECT_ACTIVE_QUEST));
    fn(idx0Based);
}

// `C_GossipInfo.SelectOption(gossipOptionID[, text[, copperCost]])`.
// Walks the options array to find the matching `gossipOptionID`,
// then calls the engine selector with the 0-based slot. `text` is
// the boxCoded password (engine gates on the flag internally);
// `copperCost` is for money-charging options (3.3.5 adds; pass 0
// for free options — matches the engine's own argument default).
int __cdecl Script_SelectOption(void *L) {
    if (!Game::Lua::IsNumber(L, 1))
        return Game::Lua::Error(L,
            "Usage: C_GossipInfo.SelectOption(gossipOptionID [, text [, copperCost]])");
    const int target = static_cast<int>(Game::Lua::ToNumber(L, 1));
    const char *password =
        Game::Lua::IsString(L, 2) ? Game::Lua::ToString(L, 2) : nullptr;
    const int copperCost =
        Game::Lua::IsNumber(L, 3) ? static_cast<int>(Game::Lua::ToNumber(L, 3)) : 0;

    for (int slot = 0; slot < Offsets::GOSSIP_OPTIONS_MAX; ++slot) {
        const uint8_t *entry = OptionEntry(slot);
        const int32_t idx = OptionIndex(entry);
        if (idx < 0)
            continue;
        if (idx == target) {
            EngineSelectOption(slot, password, copperCost);
            return 0;
        }
    }
    return 0;
}

// `C_GossipInfo.SelectOptionByIndex(orderIndex)`. `orderIndex` is
// the 1-based display position emitted by `GetOptions()`. The
// engine helper takes the 0-based array slot — note that empty
// slots are SKIPPED in the orderIndex sequence, so we have to
// walk to find the Nth populated slot rather than just decrement.
int __cdecl Script_SelectOptionByIndex(void *L) {
    if (!Game::Lua::IsNumber(L, 1))
        return Game::Lua::Error(L,
            "Usage: C_GossipInfo.SelectOptionByIndex(orderIndex)");
    const int target = static_cast<int>(Game::Lua::ToNumber(L, 1));
    if (target < 1)
        return 0;

    int seen = 0;
    for (int slot = 0; slot < Offsets::GOSSIP_OPTIONS_MAX; ++slot) {
        if (OptionIndex(OptionEntry(slot)) < 0)
            continue;
        ++seen;
        if (seen == target) {
            EngineSelectOption(slot, nullptr, 0);
            return 0;
        }
    }
    return 0;
}

// Walks `GOSSIP_QUESTS`, counting rows whose status matches
// `wantActive`. On a `questID` match, calls the right engine
// helper with the 0-based position into THAT FILTERED list —
// which is the index shape the helper's own internal walk
// expects (FUN_0058B070 / FUN_0058B120 both iterate the array
// applying the same status filter and stop at the Nth match).
int SelectQuestByID(void *L, bool wantActive, const char *errUsage) {
    if (!Game::Lua::IsNumber(L, 1))
        return Game::Lua::Error(L, errUsage);
    const uint32_t target = static_cast<uint32_t>(Game::Lua::ToNumber(L, 1));

    int idx = 0;
    for (int slot = 0; slot < Offsets::GOSSIP_QUESTS_MAX; ++slot) {
        const uint8_t *entry = QuestEntry(slot);
        const uint32_t questID = QuestID(entry);
        if (questID == 0)
            break;
        if (IsActiveQuest(QuestStatus(entry)) != wantActive)
            continue;
        if (questID == target) {
            if (wantActive)
                EngineSelectActiveQuest(idx);
            else
                EngineSelectAvailableQuest(idx);
            return 0;
        }
        ++idx;
    }
    return 0;
}

int __cdecl Script_SelectAvailableQuest(void *L) {
    return SelectQuestByID(L, /*wantActive=*/false,
        "Usage: C_GossipInfo.SelectAvailableQuest(questID)");
}

int __cdecl Script_SelectActiveQuest(void *L) {
    return SelectQuestByID(L, /*wantActive=*/true,
        "Usage: C_GossipInfo.SelectActiveQuest(questID)");
}

// Delegate to the engine's `Script_CloseGossip` Lua C function
// directly — its body is just `local buf[2] = {0,0};
// FUN_0058A550(&buf); return 0`, so reusing it shares the engine's
// CMSG-send path verbatim.
using EngineScriptFn_t = int(__cdecl *)(void *L);
int __cdecl Script_CloseGossip(void *L) {
    auto fn = reinterpret_cast<EngineScriptFn_t>(
        static_cast<uintptr_t>(Offsets::FUN_SCRIPT_CLOSE_GOSSIP));
    return fn(L);
}

void RegisterLuaFunctions() {
    Game::Lua::RegisterTableFunction("C_GossipInfo", "GetText", &Script_GetText);
    Game::Lua::RegisterTableFunction("C_GossipInfo", "GetOptions",
                                     &Script_GetOptions);
    Game::Lua::RegisterTableFunction("C_GossipInfo", "GetAvailableQuests",
                                     &Script_GetAvailableQuests);
    Game::Lua::RegisterTableFunction("C_GossipInfo", "GetActiveQuests",
                                     &Script_GetActiveQuests);
    Game::Lua::RegisterTableFunction("C_GossipInfo", "GetNumOptions",
                                     &Script_GetNumOptions);
    Game::Lua::RegisterTableFunction("C_GossipInfo", "GetNumAvailableQuests",
                                     &Script_GetNumAvailableQuests);
    Game::Lua::RegisterTableFunction("C_GossipInfo", "GetNumActiveQuests",
                                     &Script_GetNumActiveQuests);
    Game::Lua::RegisterTableFunction("C_GossipInfo", "SelectOption",
                                     &Script_SelectOption);
    Game::Lua::RegisterTableFunction("C_GossipInfo", "SelectOptionByIndex",
                                     &Script_SelectOptionByIndex);
    Game::Lua::RegisterTableFunction("C_GossipInfo", "SelectAvailableQuest",
                                     &Script_SelectAvailableQuest);
    Game::Lua::RegisterTableFunction("C_GossipInfo", "SelectActiveQuest",
                                     &Script_SelectActiveQuest);
    Game::Lua::RegisterTableFunction("C_GossipInfo", "CloseGossip",
                                     &Script_CloseGossip);
}

const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace
} // namespace Gossip::Info
