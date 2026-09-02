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

// `C_FriendList.IsFriend(token)` — is the given player on the friends list?
//
// `token` is a GUID string (the "0x…" form UnitGUID returns) or, as a
// convenience, a character name or unit token. A GUID is matched against each
// entry's stored GUID — the list keeps it for online AND offline friends. A
// name is compared against the entries directly (case-insensitive), so it holds
// for every friend, seen this session or not; anything else goes through the
// engine's name / unit-token → GUID resolver (`Offsets::FUN_NAME_TO_GUID`), so
// `"target"` works too.

#include "friendlist/FriendList.h"
#include "friendlist/Resolve.h"

#include "Game.h"
#include "Guid.h"

#include <cstdint>

namespace FriendList::IsFriend {

namespace {

int __cdecl Script_IsFriend(void *L) {
    const char *arg = Game::Lua::IsString(L, 1) ? Game::Lua::ToString(L, 1) : nullptr;
    if (arg == nullptr || *arg == '\0') {
        Game::Lua::PushBool(L, false);
        return 1;
    }
    if (!Guid::IsGuidString(arg) && FriendList::EntryByName(arg) != nullptr) {
        Game::Lua::PushBool(L, true);
        return 1;
    }
    const Guid::Pair guid = FriendList::ResolveToken(arg);
    Game::Lua::PushBool(L, guid.valid() && FriendList::EntryByGuid(guid.value()) != nullptr);
    return 1;
}

void RegisterLuaFunctions() {
    Game::Lua::RegisterTableFunction("C_FriendList", "IsFriend", &Script_IsFriend);
}

const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace

} // namespace FriendList::IsFriend
