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

// `UnitNameFromGUID(guid) -> (name, realm)` — the name of a unit/player by GUID,
// or nil if the client doesn't know it. Not native in 3.3.5; built from the two
// name sources the engine's own `UnitName` uses:
//   * the player name cache (the same store `GetPlayerInfoByGUID` reads) — covers
//     any cached player (group, chat, combat, /who) whether or not they're in
//     view; gives name + realm.
//   * the live CGUnit — covers any player or creature currently in the object
//     manager (you can see it); gives name (realm nil for creatures).
//
// `realm` is nil for a same-realm player (always, on a single-realm 3.3.5 server)
// and for creatures. A GUID the client has no name for returns nil — no name
// query is issued (pure cache lookup), matching the modern call.

#include "Game.h"
#include "Guid.h"
#include "Offsets.h"

#include <cstdint>

namespace Unit::NameFromGUID {

namespace {

using UnitNameFromObject_t = const char *(__thiscall *)(void *unit, const char **realmOut,
                                                        int flag);
using PlayerNameCacheGet_t = const uint8_t *(__thiscall *)(void *cache, uint32_t guidLo,
                                                           uint32_t guidHi, uint32_t *scratch,
                                                           uint32_t a5, uint32_t a6, char a7);

// Cached player (in view or not). Pure lookup — null callback / flags, so a miss
// has no side effects. Record: name inline @+0x00, realm inline @+0x34.
bool ReadPlayerCache(uint32_t lo, uint32_t hi, const char **name, const char **realm) {
    uint32_t scratch[2] = {0, 0};
    auto *cache = reinterpret_cast<void *>(Offsets::VAR_PLAYER_NAME_CACHE);
    const uint8_t *rec = reinterpret_cast<PlayerNameCacheGet_t>(
        Offsets::FUN_PLAYER_NAME_CACHE_GET)(cache, lo, hi, scratch, 0, 0, 0);
    if (rec == nullptr)
        return false;
    *name = reinterpret_cast<const char *>(rec + Offsets::OFF_PLAYER_NAME_REC_NAME);
    *realm = reinterpret_cast<const char *>(rec + Offsets::OFF_PLAYER_NAME_REC_REALM);
    return true;
}

// Any in-world unit (player or creature) — resolve the GUID to its CGUnit and
// read its name via the engine's unit-name getter (which fills realm, null for
// creatures). Resolving with the UNIT type mask returns null for non-units.
bool ReadInWorldUnit(uint32_t lo, uint32_t hi, const char **name, const char **realm) {
    void *unit = Guid::ResolveObject({lo, hi}, Offsets::OBJ_FLAGS_UNIT);
    if (unit == nullptr)
        return false;
    *realm = nullptr;
    *name = reinterpret_cast<UnitNameFromObject_t>(Offsets::FUN_UNIT_NAME_FROM_OBJECT)(unit, realm,
                                                                                       1);
    return *name != nullptr;
}

int __cdecl Script_UnitNameFromGUID(void *L) {
    if (!Game::Lua::IsString(L, 1))
        return 0; // nil for a nil / non-string argument
    const char *guidStr = Game::Lua::ToString(L, 1);
    const Guid::Pair g = Guid::Parse(guidStr);
    if (!g.valid())
        return 0;
    const uint32_t lo = g.lo;
    const uint32_t hi = g.hi;

    const char *name = nullptr;
    const char *realm = nullptr;
    // Cached players first (covers out-of-view group/combat/chat names), then any
    // in-world unit (creatures, plus visible players missing from the cache).
    if (!ReadPlayerCache(lo, hi, &name, &realm) && !ReadInWorldUnit(lo, hi, &name, &realm))
        return 0; // nil — the client has no name for this GUID

    Game::Lua::PushString(L, name);
    if (realm != nullptr && realm[0] != '\0')
        Game::Lua::PushString(L, realm);
    else
        Game::Lua::PushNil(L);
    return 2;
}

void RegisterLuaFunctions() {
    Game::Lua::RegisterGlobalFunction("UnitNameFromGUID", &Script_UnitNameFromGUID);
}

const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace

} // namespace Unit::NameFromGUID
