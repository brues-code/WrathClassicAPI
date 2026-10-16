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

// `UnitGUID("player")` during addon loading. The engine resolves "player"
// through the player object, which the object manager creates only when the
// server's first object update arrives — after FrameXML and the addons have
// loaded. So the engine returns nil until then.
//
// The client already knows the GUID: char select stores the character it
// logs in as (CGlueMgr::GetActive, the GUID sent in CMSG_PLAYER_LOGIN), and
// it stays set for the session. A post-hook on Script_UnitGUID answers
// "player" from it when the engine has no answer yet. Every other token, and
// "player" once the object exists, is the engine's own result.

#include "Game.h"
#include "Guid.h"
#include "Offsets.h"

#include <cstdint>
#include <cstring>

namespace Unit::GUID {

namespace {

using GetActiveCharacter_t = uint64_t(__cdecl *)();

Game::Lua::CFunction g_unitGuidOriginal = nullptr;

int __cdecl Script_UnitGUID_h(void *L) {
    const int results = g_unitGuidOriginal(L);
    if (results != 1 || Game::Lua::Type(L, -1) != Game::Lua::TYPE_NIL ||
        _stricmp(Game::Lua::ToString(L, 1), "player") != 0)
        return results;
    const Guid::Pair guid = Guid::Split(
        reinterpret_cast<GetActiveCharacter_t>(Offsets::FUN_GLUEMGR_GET_ACTIVE_CHARACTER)());
    if (!guid.valid())
        return results;
    char buf[Guid::STRING_SIZE];
    Game::Lua::SetTop(L, -2);
    Game::Lua::PushString(L, Guid::Format(guid, buf, sizeof buf));
    return 1;
}

const Game::HookAutoRegister _hook{
    Offsets::FUN_SCRIPT_UNIT_GUID, reinterpret_cast<void *>(&Script_UnitGUID_h),
    reinterpret_cast<void **>(&g_unitGuidOriginal)};

} // namespace

} // namespace Unit::GUID
