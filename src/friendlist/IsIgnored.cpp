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

// `C_FriendList.IsIgnored(token)` / `C_FriendList.IsIgnoredByGuid(guid)` — is
// the given player on the ignore list?
//
// The ignore list is a GUID array in the social singleton (see `FriendList.h`),
// so `IsIgnoredByGuid` is a direct walk. `IsIgnored` accepts a GUID string, a
// character name, or a unit token, resolved the same way the engine's own
// IsIgnored resolves its argument (see `Resolve.h`): a name matches only a
// player the client has seen this session — the same players GetIgnoreName can
// name — because the name comes from the client's name cache.

#include "friendlist/FriendList.h"
#include "friendlist/Resolve.h"

#include "Game.h"
#include "Guid.h"

#include <cstdint>

namespace FriendList::IsIgnored {

namespace {

int __cdecl Script_IsIgnored(void *L) {
    const char *arg = Game::Lua::IsString(L, 1) ? Game::Lua::ToString(L, 1) : nullptr;
    if (arg == nullptr || *arg == '\0') {
        Game::Lua::PushBool(L, false);
        return 1;
    }
    const Guid::Pair guid = FriendList::ResolveToken(arg);
    Game::Lua::PushBool(L, guid.valid() && FriendList::IsGuidIgnored(guid.value()));
    return 1;
}

int __cdecl Script_IsIgnoredByGuid(void *L) {
    const char *arg = Game::Lua::IsString(L, 1) ? Game::Lua::ToString(L, 1) : nullptr;
    const Guid::Pair guid = Guid::Parse(arg);
    Game::Lua::PushBool(L, guid.valid() && FriendList::IsGuidIgnored(guid.value()));
    return 1;
}

void RegisterLuaFunctions() {
    Game::Lua::RegisterTableFunction("C_FriendList", "IsIgnored", &Script_IsIgnored);
    Game::Lua::RegisterTableFunction("C_FriendList", "IsIgnoredByGuid",
                                     &Script_IsIgnoredByGuid);
}

const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace

} // namespace FriendList::IsIgnored
