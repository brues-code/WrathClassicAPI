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

// `IsInGroup([groupType])` and `IsInRaid([groupType])`, plus the
// LE_PARTY_CATEGORY_HOME / LE_PARTY_CATEGORY_INSTANCE constants they take.
// Ported from ClassicAPI's src/group/Info.cpp.
//
// The modern categories map onto the two groups this client already keeps
// (see the Offsets.h group block):
//   - Home: your own group, the client's "real" group.
//   - Instance: the battleground or arena group you're in. The current
//     group is that group exactly when the current map is a battleground or
//     arena, the test CGRaidInfo::IsPVPRaid makes.
//   - No argument: either.

#include "Game.h"
#include "Offsets.h"
#include "dbc/Store.h"

#include <cstdint>

namespace Group::Info {

namespace {

constexpr int LE_PARTY_CATEGORY_HOME = 1;
constexpr int LE_PARTY_CATEGORY_INSTANCE = 2;

uint32_t ReadU32(uintptr_t va) { return *reinterpret_cast<const uint32_t *>(va); }

bool CurrentParty() {
    const auto *guids = reinterpret_cast<const uint64_t *>(
        static_cast<uintptr_t>(Offsets::VAR_PARTY_MEMBER_GUIDS));
    for (int i = 0; i < Offsets::PARTY_MEMBER_SLOTS; ++i)
        if (guids[i] != 0)
            return true;
    return false;
}

bool CurrentRaid() { return ReadU32(Offsets::VAR_RAID_NUM_MEMBERS) > 0; }
bool HomeParty() { return ReadU32(Offsets::VAR_PARTY_REAL_NUM_MEMBERS) > 0; }
bool HomeRaid() { return ReadU32(Offsets::VAR_RAID_REAL_NUM_MEMBERS) > 0; }

// The engine's CGRaidInfo::IsPVPRaid, which itself dereferences a missing
// map record.
bool OnPvPMap() {
    const auto mapID = static_cast<int>(ReadU32(Offsets::VAR_CURRENT_MAP_ID));
    const uint8_t *map = DBC::Record(Offsets::VAR_MAP_DBC_MIN_INDEX,
                                     Offsets::VAR_MAP_DBC_MAX_INDEX,
                                     Offsets::VAR_MAP_DBC_INDEX_TABLE, mapID);
    if (map == nullptr)
        return false;
    const uint32_t type = *reinterpret_cast<const uint32_t *>(map + Offsets::OFF_MAP_INSTANCE_TYPE);
    return type == Offsets::MAP_INSTANCE_TYPE_BATTLEGROUND ||
           type == Offsets::MAP_INSTANCE_TYPE_ARENA;
}

// Any other value, nil included, asks about either group.
int GroupType(void *L) {
    if (Game::Lua::Type(L, 1) != Game::Lua::TYPE_NUMBER)
        return 0;
    return static_cast<int>(Game::Lua::ToNumber(L, 1));
}

// IsInGroup([groupType]) — in a party or a raid.
int __cdecl Script_IsInGroup(void *L) {
    bool result;
    switch (GroupType(L)) {
        case LE_PARTY_CATEGORY_HOME:
            result = HomeParty() || HomeRaid();
            break;
        case LE_PARTY_CATEGORY_INSTANCE:
            result = OnPvPMap() && (CurrentParty() || CurrentRaid());
            break;
        default:
            result = CurrentParty() || CurrentRaid() || HomeParty() || HomeRaid();
            break;
    }
    Game::Lua::PushBool(L, result);
    return 1;
}

// IsInRaid([groupType]) — in a raid; a party answers false.
int __cdecl Script_IsInRaid(void *L) {
    bool result;
    switch (GroupType(L)) {
        case LE_PARTY_CATEGORY_HOME:
            result = HomeRaid();
            break;
        case LE_PARTY_CATEGORY_INSTANCE:
            result = OnPvPMap() && CurrentRaid();
            break;
        default:
            result = CurrentRaid() || HomeRaid();
            break;
    }
    Game::Lua::PushBool(L, result);
    return 1;
}

void RegisterLuaFunctions() {
    Game::Lua::RegisterGlobalFunction("IsInGroup", &Script_IsInGroup);
    Game::Lua::RegisterGlobalFunction("IsInRaid", &Script_IsInRaid);
    void *L = Game::Lua::State();
    Game::Lua::SetGlobalNumber(L, "LE_PARTY_CATEGORY_HOME", LE_PARTY_CATEGORY_HOME);
    Game::Lua::SetGlobalNumber(L, "LE_PARTY_CATEGORY_INSTANCE", LE_PARTY_CATEGORY_INSTANCE);
}

const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace

} // namespace Group::Info
