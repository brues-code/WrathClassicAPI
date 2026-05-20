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

// `GetTalentSpellID(tabIndex, talentIndex[, isInspect, isPet, groupIndex, rank])` and
// `GetTalentIDByIndex(tabIndex, talentIndex[, isInspect, isPet, groupIndex])` —
// modern-API-shape getters that bridge the talent UI into the spell /
// stable-ID surface.
//
// First 5 args mirror the engine's `GetTalentInfo` positional order
// exactly. That lets `GetTalentSpellID` delegate to the engine for
// currentRank lookup without reshuffling the Lua stack — the engine
// reads its args from the same positions our function received them.
// The trailing `rank` (position 6) is our extension: pin a specific
// rank instead of asking for "whatever the player has now".

#include "Game.h"
#include "Offsets.h"

#include <cstdint>

namespace Talent::Info {

namespace {

using GetTabInfo_t = const uint8_t *(__cdecl *)(uint32_t tabIndex0Based,
                                                 int isInspect, int isPet);

// Resolves (tabIndex, talentIndex, isInspect, isPet) to the
// TalentEntry pointer in the appropriate TabInfo array. Returns
// nullptr for any out-of-range index, missing data, or mismatched
// isInspect/isPet combo (engine returns NULL for invalid combos).
const uint8_t *ResolveTalentEntry(int tabIndex, int talentIndex,
                                  int isInspect, int isPet) {
    if (tabIndex < 1 || talentIndex < 1)
        return nullptr;

    auto getTabInfo = reinterpret_cast<GetTabInfo_t>(
        Offsets::FUN_TALENT_TAB_INFO_LOOKUP);
    const uint8_t *tabInfo = getTabInfo(
        static_cast<uint32_t>(tabIndex - 1), isInspect, isPet);
    if (tabInfo == nullptr)
        return nullptr;

    const int numTalents = *reinterpret_cast<const int *>(
        tabInfo + Offsets::OFF_TABINFO_NUM_TALENTS);
    if (talentIndex > numTalents)
        return nullptr;

    auto *talents = *reinterpret_cast<const uint8_t *const *>(
        tabInfo + Offsets::OFF_TABINFO_TALENT_ARRAY);
    if (talents == nullptr)
        return nullptr;

    return talents + (talentIndex - 1) * Offsets::TALENT_ENTRY_STRIDE;
}

// `GetTalentSpellID(tabIndex, talentIndex[, isInspect, isPet, groupIndex, rank])`
//
// Returns the spellID for the given talent at the requested rank, or
// `nil` if anything is out of range / the talent isn't allocated and
// no rank fallback applies.
//
// `rank` (arg 6, optional): explicit rank to look up. When omitted,
// defaults to `currentRank` (read via the engine's `Script_GetTalentInfo`
// which respects isInspect / isPet / groupIndex). If currentRank is 0
// (no points allocated, or the inspected unit doesn't have this talent
// allocated), falls back to rank 1 so the function still produces the
// canonical spellID — handy for `GameTooltip:SetSpellByID` previews.
int __cdecl Script_GetTalentSpellID(void *L) {
    if (!Game::Lua::IsNumber(L, 1) || !Game::Lua::IsNumber(L, 2))
        return Game::Lua::Error(L,
            "Usage: GetTalentSpellID(tabIndex, talentIndex[, isInspect, isPet, groupIndex, rank])");

    const int tabIndex = static_cast<int>(Game::Lua::ToNumber(L, 1));
    const int talentIndex = static_cast<int>(Game::Lua::ToNumber(L, 2));
    const int isInspect = Game::Lua::ToBoolean(L, 3);
    const int isPet = Game::Lua::ToBoolean(L, 4);

    const uint8_t *entry = ResolveTalentEntry(tabIndex, talentIndex,
                                              isInspect, isPet);
    if (entry == nullptr)
        return 0;

    int rank;
    if (Game::Lua::IsNumber(L, 6)) {
        rank = static_cast<int>(Game::Lua::ToNumber(L, 6));
        if (rank < 1 || rank > Offsets::TALENT_MAX_RANKS)
            return 0;
    } else {
        // Implicit rank: delegate to the engine's GetTalentInfo. Args
        // 1..5 already match its expected positional order
        // (tabIndex, talentIndex, isInspect, isPet, groupIndex), so
        // we call it as-is — the only "shape" detail is reading the
        // 5th return (`currentRank`) at stack index `-(n-4)` since
        // 3.3.5 pushes 10 values total.
        using GetTalentInfo_t = int(__cdecl *)(void *L);
        auto fn = reinterpret_cast<GetTalentInfo_t>(
            Offsets::FUN_SCRIPT_GET_TALENT_INFO);
        const int n = fn(L);
        if (n < 5)
            return 0;
        rank = static_cast<int>(Game::Lua::ToNumber(L, -(n - 4)));
        if (rank < 1)
            rank = 1; // unallocated → fall back to rank-1 spellID
    }

    const uint32_t spellID = *reinterpret_cast<const uint32_t *>(
        entry + Offsets::OFF_TALENT_SPELL_RANK +
        (rank - 1) * sizeof(uint32_t));
    if (spellID == 0)
        return 0; // rank slot past maxRank for this talent

    Game::Lua::PushNumber(L, static_cast<double>(spellID));
    return 1;
}

// `GetTalentIDByIndex(tabIndex, talentIndex[, isInspect, isPet, groupIndex])`
//
// Returns the Talent.dbc primary key for the talent at (tab, idx) in
// the source selected by isInspect/isPet, or `nil` for out-of-range
// input.
//
// `groupIndex` is accepted for API symmetry with the modern shape but
// doesn't affect the result here — talent IDs are class-determined
// and identical across dual-spec groups. The arg is parsed and
// ignored.
int __cdecl Script_GetTalentIDByIndex(void *L) {
    if (!Game::Lua::IsNumber(L, 1) || !Game::Lua::IsNumber(L, 2))
        return Game::Lua::Error(L,
            "Usage: GetTalentIDByIndex(tabIndex, talentIndex[, isInspect, isPet, groupIndex])");

    const int tabIndex = static_cast<int>(Game::Lua::ToNumber(L, 1));
    const int talentIndex = static_cast<int>(Game::Lua::ToNumber(L, 2));
    const int isInspect = Game::Lua::ToBoolean(L, 3);
    const int isPet = Game::Lua::ToBoolean(L, 4);

    const uint8_t *entry = ResolveTalentEntry(tabIndex, talentIndex,
                                              isInspect, isPet);
    if (entry == nullptr)
        return 0;

    const uint32_t talentID = *reinterpret_cast<const uint32_t *>(
        entry + Offsets::OFF_TALENT_DBC_ID);
    if (talentID == 0)
        return 0;

    Game::Lua::PushNumber(L, static_cast<double>(talentID));
    return 1;
}

void RegisterLuaFunctions() {
    Game::Lua::RegisterGlobalFunction("GetTalentSpellID",
                                      &Script_GetTalentSpellID);
    Game::Lua::RegisterGlobalFunction("GetTalentIDByIndex",
                                      &Script_GetTalentIDByIndex);
}

const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace
} // namespace Talent::Info
