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

// `C_Reputation.*` + `GetFactionIDByIndex` — factionID-centric reputation
// accessors. The stock 3.3.5 surface (`GetFactionInfo`, `GetWatchedFactionInfo`,
// `SetWatchedFactionIndex`) is display-index-based and never exposes a factionID;
// these fill that gap.
//
// The reads go engine-direct: the displayed-list resolver turns an index into a
// factionID, `Faction.dbc` gives name/description/rep-slot index, and the engine's
// own __cdecl(factionID) helpers give band / standing / at-war / etc. (See the
// Reputation block in Offsets.h for the derivation.)

#include "Game.h"
#include "Offsets.h"
#include "unit/Resolve.h"

#include <cstdint>

namespace Faction::Info {

namespace {

// --- engine helpers (all __cdecl; see Offsets.h) ------------------------------
using GetBand_t = unsigned char(__cdecl *)(int factionID);
using GetStanding_t = int(__cdecl *)(int factionID);
using GetBool_t = unsigned char(__cdecl *)(int factionID); // at-war / peace / watched / child
using HasRep_t = unsigned char(__cdecl *)(const void *record);
using SetWatched_t = void(__cdecl *)(int factionID);

template <typename Fn> Fn At(uintptr_t addr) { return reinterpret_cast<Fn>(addr); }

// Value of a 32-bit engine global.
int32_t Global32(uintptr_t addr) { return *reinterpret_cast<const int32_t *>(addr); }

// Faction.dbc record pointer for `factionID`, or nullptr for out-of-range ids
// (headers / pseudo-rows use factionID 0) and empty slots.
const uint8_t *FactionRecord(int factionID) {
    const int minIndex = Global32(Offsets::VAR_FACTION_DBC_MIN_INDEX);
    const int maxIndex = Global32(Offsets::VAR_FACTION_DBC_MAX_INDEX);
    if (factionID < minIndex || factionID > maxIndex)
        return nullptr;
    auto *table = *reinterpret_cast<const uint8_t *const *const *>(
        Offsets::VAR_FACTION_DBC_INDEX_TABLE);
    if (table == nullptr)
        return nullptr;
    return table[factionID - minIndex];
}

// 0-based display index → factionID. Returns 0 for out-of-range indices and for
// the header / "Other" / "Inactive" pseudo-rows that carry factionID 0.
int ResolveIndex(int idx0) {
    if (idx0 < 0 || idx0 > Global32(Offsets::VAR_FACTION_VISIBLE_MAX_INDEX))
        return 0;
    auto *list = *reinterpret_cast<const uint8_t *const *const *>(
        Offsets::VAR_FACTION_DISPLAY_LIST);
    if (list == nullptr)
        return 0;
    const uint8_t *entry = list[idx0];
    if (entry == nullptr)
        return 0;
    return *reinterpret_cast<const int32_t *>(entry + Offsets::OFF_FACTION_DISPLAY_ENTRY_ID);
}

// Player's watched rep-slot index, or -1 if unresolvable / nothing watched.
int WatchedRepIndex() {
    auto *player = static_cast<const uint8_t *>(Unit::ResolveToken("player"));
    if (player == nullptr)
        return -1;
    auto *info = *reinterpret_cast<const uint8_t *const *>(
        player + Offsets::OFF_CGPLAYER_INFO);
    if (info == nullptr)
        return -1;
    return *reinterpret_cast<const int32_t *>(
        info + Offsets::OFF_CGPLAYER_INFO_WATCHED_REP_LIST_ID);
}

const uint8_t *RepSlot(int repIndex) {
    if (repIndex < 0 || repIndex >= Offsets::MAX_REP_SLOTS)
        return nullptr;
    return reinterpret_cast<const uint8_t *>(
        static_cast<uintptr_t>(Offsets::VAR_PLAYER_REP_SLOTS) +
        static_cast<uintptr_t>(repIndex) * Offsets::REP_SLOT_STRIDE);
}

// isHeader / isCollapsed from the faction's position in the displayed-list header
// array (same walk Script_GetFactionInfo does). Collapsed = header's mask bit clear.
void HeaderState(int factionID, bool *isHeader, bool *isCollapsed) {
    *isHeader = false;
    *isCollapsed = false;
    if (factionID == 0)
        return;
    const int count = Global32(Offsets::VAR_FACTION_HEADER_COUNT);
    auto *headers = reinterpret_cast<const int32_t *>(
        static_cast<uintptr_t>(Offsets::VAR_FACTION_HEADER_LIST));
    const int cap = count < Offsets::MAX_FACTION_HEADERS ? count : Offsets::MAX_FACTION_HEADERS;
    for (int i = 0; i < cap; ++i) {
        if (headers[i] == factionID) {
            *isHeader = true;
            const uint32_t mask = *reinterpret_cast<const uint32_t *>(
                static_cast<uintptr_t>(Offsets::VAR_FACTION_COLLAPSED_BITMASK));
            *isCollapsed = (mask & (1u << (static_cast<unsigned>(i) & 31))) == 0;
            break;
        }
    }
}

// Everything the modern FactionData table needs for one faction. Populated
// engine-direct; `ReadFactionData` returns false only when the factionID has no
// Faction.dbc record.
struct FactionData {
    int factionID;
    int repListIndex; // -1 if the record has no rep slot
    const char *name;
    const char *description;
    int reaction; // 1..8 (Hated..Exalted)
    int currentReactionThreshold;
    int nextReactionThreshold;
    int currentStanding;
    bool atWarWith;
    bool canToggleAtWar;
    bool isHeader;
    bool isHeaderWithRep;
    bool isCollapsed;
    bool isWatched;
    bool isChild;
};

bool ReadFactionData(int factionID, FactionData *out) {
    *out = {};
    const uint8_t *record = FactionRecord(factionID);
    if (record == nullptr)
        return false;

    out->factionID = factionID;
    out->repListIndex = *reinterpret_cast<const int32_t *>(
        record + Offsets::OFF_FACTION_REP_LIST_INDEX);
    out->name = *reinterpret_cast<const char *const *>(record + Offsets::OFF_FACTION_NAME);
    out->description = *reinterpret_cast<const char *const *>(
        record + Offsets::OFF_FACTION_DESCRIPTION);
    if (out->name == nullptr)
        out->name = "";
    if (out->description == nullptr)
        out->description = "";

    const int band = At<GetBand_t>(Offsets::FUN_REPUTATION_GET_REACTION_BAND)(factionID);
    out->reaction = band + 1;
    out->currentReactionThreshold =
        Global32(static_cast<uintptr_t>(Offsets::VAR_REACTION_MIN_TABLE) + band * 4u);
    out->nextReactionThreshold =
        Global32(static_cast<uintptr_t>(Offsets::VAR_REACTION_MAX_TABLE) + band * 4u);
    out->currentStanding = At<GetStanding_t>(Offsets::FUN_REPUTATION_GET_STANDING)(factionID);

    out->atWarWith = At<GetBool_t>(Offsets::FUN_REPUTATION_GET_AT_WAR)(factionID) != 0;
    out->canToggleAtWar =
        (out->currentStanding >= -3000) &&
        At<GetBool_t>(Offsets::FUN_REPUTATION_GET_PEACE_FORCED)(factionID) == 0;
    HeaderState(factionID, &out->isHeader, &out->isCollapsed);
    out->isHeaderWithRep = At<HasRep_t>(Offsets::FUN_REPUTATION_HEADER_HAS_REP)(record) != 0;
    out->isWatched = At<GetBool_t>(Offsets::FUN_REPUTATION_IS_WATCHED)(factionID) != 0;
    out->isChild = At<GetBool_t>(Offsets::FUN_REPUTATION_IS_CHILD)(factionID) != 0;
    return true;
}

// Builds the modern FactionData-shape table on the Lua stack.
void PushFactionDataTable(void *L, const FactionData &d) {
    Game::Lua::NewTable(L);
    Game::Lua::SetFieldNumber(L, "factionID", static_cast<double>(d.factionID));
    Game::Lua::SetFieldString(L, "name", d.name);
    Game::Lua::SetFieldString(L, "description", d.description);
    Game::Lua::SetFieldNumber(L, "reaction", static_cast<double>(d.reaction));
    Game::Lua::SetFieldNumber(L, "currentReactionThreshold",
                              static_cast<double>(d.currentReactionThreshold));
    Game::Lua::SetFieldNumber(L, "nextReactionThreshold",
                              static_cast<double>(d.nextReactionThreshold));
    Game::Lua::SetFieldNumber(L, "currentStanding", static_cast<double>(d.currentStanding));
    Game::Lua::SetFieldBool(L, "atWarWith", d.atWarWith);
    Game::Lua::SetFieldBool(L, "canToggleAtWar", d.canToggleAtWar);
    Game::Lua::SetFieldBool(L, "isHeader", d.isHeader);
    Game::Lua::SetFieldBool(L, "isHeaderWithRep", d.isHeaderWithRep);
    Game::Lua::SetFieldBool(L, "isCollapsed", d.isCollapsed);
    Game::Lua::SetFieldBool(L, "isWatched", d.isWatched);
    Game::Lua::SetFieldBool(L, "canSetInactive", !d.isHeader && d.repListIndex >= 0);
    Game::Lua::SetFieldBool(L, "isChild", d.isChild);
    // Modern flags with no 3.3.5 source — stubbed for API parity.
    Game::Lua::SetFieldBool(L, "hasBonusRepGain", false);
    Game::Lua::SetFieldBool(L, "isAccountWide", false);
}

// `GetFactionIDByIndex(factionIndex)` — factionID for a 1-based displayed-list
// index, `0` for header / pseudo rows, `nil` for out-of-range.
int __cdecl Script_GetFactionIDByIndex(void *L) {
    if (!Game::Lua::IsNumber(L, 1)) {
        Game::Lua::Error(L, "Usage: GetFactionIDByIndex(factionIndex)");
        return 0;
    }
    const int idx0 = static_cast<int>(Game::Lua::ToNumber(L, 1)) - 1;
    if (idx0 < 0 || idx0 > Global32(Offsets::VAR_FACTION_VISIBLE_MAX_INDEX))
        return 0; // nil for out-of-range
    const int factionID = ResolveIndex(idx0);
    Game::Lua::PushNumber(L, static_cast<double>(factionID > 0 ? factionID : 0));
    return 1;
}

// `C_Reputation.GetFactionDataByIndex(factionSortIndex)` — modern table over the
// displayed list; nil for out-of-range or the record-less pseudo-rows.
int __cdecl Script_GetFactionDataByIndex(void *L) {
    if (!Game::Lua::IsNumber(L, 1)) {
        Game::Lua::Error(L, "Usage: C_Reputation.GetFactionDataByIndex(factionSortIndex)");
        return 0;
    }
    const int idx0 = static_cast<int>(Game::Lua::ToNumber(L, 1)) - 1;
    if (idx0 < 0 || idx0 > Global32(Offsets::VAR_FACTION_VISIBLE_MAX_INDEX))
        return 0;

    FactionData d;
    if (!ReadFactionData(ResolveIndex(idx0), &d))
        return 0;

    Game::Lua::SetTop(L, 0);
    PushFactionDataTable(L, d);
    return 1;
}

// `C_Reputation.GetWatchedFactionData()` — modern table for the faction shown
// over the XP bar, or nil when nothing is watched.
int __cdecl Script_GetWatchedFactionData(void *L) {
    const uint8_t *slot = RepSlot(WatchedRepIndex());
    if (slot == nullptr)
        return 0;
    const int factionID = *reinterpret_cast<const int32_t *>(
        slot + Offsets::OFF_REP_SLOT_FACTION_ID);

    FactionData d;
    if (!ReadFactionData(factionID, &d))
        return 0;
    d.isWatched = true;

    Game::Lua::SetTop(L, 0);
    PushFactionDataTable(L, d);
    return 1;
}

// `C_Reputation.SetWatchedFactionByID(factionID)` — set the faction shown over
// the XP bar by ID (0 clears). Delegates to the engine's setter, which resolves
// the rep slot and syncs to the server.
int __cdecl Script_SetWatchedFactionByID(void *L) {
    if (!Game::Lua::IsNumber(L, 1)) {
        Game::Lua::Error(L, "Usage: C_Reputation.SetWatchedFactionByID(factionID)");
        return 0;
    }
    const int factionID = static_cast<int>(Game::Lua::ToNumber(L, 1));
    if (factionID < 0)
        return 0;
    At<SetWatched_t>(Offsets::FUN_PLAYER_SET_WATCHED_FACTION)(factionID);
    return 0;
}

// `C_Reputation.GetFactionStandings()` — flat `{ [factionID] = currentStanding }`
// over every populated rep slot. Reads the slot array directly, so it doesn't
// depend on the reputation pane having been opened.
int __cdecl Script_GetFactionStandings(void *L) {
    Game::Lua::SetTop(L, 0);
    Game::Lua::NewTable(L);

    for (int i = 0; i < Offsets::MAX_REP_SLOTS; ++i) {
        const uint8_t *slot = RepSlot(i);
        const int factionID = *reinterpret_cast<const int32_t *>(
            slot + Offsets::OFF_REP_SLOT_FACTION_ID);
        if (factionID <= 0)
            continue;
        const int base = *reinterpret_cast<const int32_t *>(
            slot + Offsets::OFF_REP_SLOT_BASE_STANDING);
        const int delta = *reinterpret_cast<const int32_t *>(
            slot + Offsets::OFF_REP_SLOT_DELTA_STANDING);
        Game::Lua::PushNumber(L, static_cast<double>(factionID));
        Game::Lua::PushNumber(L, static_cast<double>(base + delta));
        Game::Lua::SetTable(L, -3);
    }
    return 1;
}

void RegisterLuaFunctions() {
    Game::Lua::RegisterGlobalFunction("GetFactionIDByIndex", &Script_GetFactionIDByIndex);
    Game::Lua::RegisterTableFunction("C_Reputation", "GetFactionDataByIndex",
                                     &Script_GetFactionDataByIndex);
    Game::Lua::RegisterTableFunction("C_Reputation", "GetWatchedFactionData",
                                     &Script_GetWatchedFactionData);
    Game::Lua::RegisterTableFunction("C_Reputation", "SetWatchedFactionByID",
                                     &Script_SetWatchedFactionByID);
    Game::Lua::RegisterTableFunction("C_Reputation", "GetFactionStandings",
                                     &Script_GetFactionStandings);
}

const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace

} // namespace Faction::Info
