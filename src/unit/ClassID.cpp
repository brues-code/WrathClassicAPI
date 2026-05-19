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

#include "Game.h"
#include "Offsets.h"

#include <cstdint>
#include <cstring>

// `UnitClassID(unit)` → number
//
// Returns the numeric class ID (1=Warrior, 2=Paladin, 3=Hunter,
// 4=Rogue, 5=Priest, 6=Death Knight, 7=Shaman, 8=Mage, 9=Warlock,
// 11=Druid) for the unit, or `nil` if the unit can't be resolved.
//
// Why this exists: in 3.3.5a, neither `UnitClass(unit)` nor
// `UnitClassBase(unit)` returns the class ID — both return
// `(localizedName, englishToken)`. Modern Blizzard's `UnitClass`
// adds a third `classID` return, and `UnitClassBase` returns
// `(englishToken, classID)`; this function backports just the
// integer so addons can use `LOCALIZED_CLASS_NAMES_MALE[…]`-style
// dispatch without having to maintain a string→ID lookup table.
//
// Accepts any standard unit token via the engine's
// `ResolveUnitToken` (`"player"`, `"target"`, `"partyN"`, `"raidN"`,
// `"mouseover"`, etc.). The engine's own `Script_UnitClassBase`
// only handles `"player"` + GUID-string forms, so this function
// fills that gap too.

namespace Unit::ClassID {

namespace {

using ResolveUnitToken_t = void *(__cdecl *)(const char *token);

int __cdecl Script_UnitClassID(void *L) {
    if (!Game::Lua::IsString(L, 1))
        return Game::Lua::Error(L, "Usage: UnitClassID(\"unit\")");

    const char *token = Game::Lua::ToString(L, 1);
    if (token == nullptr) {
        Game::Lua::PushNil(L);
        return 1;
    }

    // Fast path for `"player"` — matches what the engine's own
    // `Script_UnitClass` / `Script_UnitClassBase` do via
    // `FUN_006B1080`. The descriptor path below would race with
    // engine init at first login (descriptor pointer isn't
    // populated yet), returning nil; this global is set during
    // login session setup so it's always valid in-game.
    if (std::strcmp(token, "player") == 0) {
        const uint8_t classByte =
            *reinterpret_cast<const uint8_t *>(Offsets::VAR_LOCAL_PLAYER_CLASS_BYTE);
        if (classByte == 0) {
            Game::Lua::PushNil(L);
            return 1;
        }
        Game::Lua::PushNumber(L, static_cast<double>(classByte));
        return 1;
    }

    auto resolve = reinterpret_cast<ResolveUnitToken_t>(Offsets::FUN_RESOLVE_UNIT_TOKEN);
    auto *unit = static_cast<const uint8_t *>(resolve(token));
    if (unit == nullptr) {
        Game::Lua::PushNil(L);
        return 1;
    }
    auto *desc = *reinterpret_cast<const uint8_t *const *>(
        unit + Offsets::OFF_UNIT_DESCRIPTOR);
    if (desc == nullptr) {
        Game::Lua::PushNil(L);
        return 1;
    }
    const uint8_t classByte = *(desc + Offsets::OFF_UNIT_DESCRIPTOR_CLASS_BYTE);
    if (classByte == 0) {
        Game::Lua::PushNil(L);
        return 1;
    }
    Game::Lua::PushNumber(L, static_cast<double>(classByte));
    return 1;
}

void RegisterLuaFunctions() {
    Game::Lua::RegisterGlobalFunction("UnitClassID", &Script_UnitClassID);
}

const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace
} // namespace Unit::ClassID
