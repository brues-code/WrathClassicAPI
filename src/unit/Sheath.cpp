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

// `GetSheathState()` — which weapon type the local player has drawn. Takes no
// arguments; the return is 1-based, matching modern WoW:
//   1 = None   (weapons sheathed / put away)
//   2 = Melee  (melee weapon drawn)
//   3 = Ranged (ranged weapon drawn)
// Ported from ClassicAPI's src/unit/Sheath.cpp.
//
// The client ships `ToggleSheath` but no getter. It keeps the state 0-based
// in CGUnit_C::m_sheatheState, the field its own toggle reads and its setter
// writes, so a toggle shows up at once instead of after the server's reply.

#include "Game.h"
#include "Offsets.h"
#include "unit/Player.h"

#include <cstdint>

namespace Unit::Sheath {

namespace {

int __cdecl Script_GetSheathState(void *L) {
    const auto *player = static_cast<const uint8_t *>(Unit::LocalPlayer());
    uint32_t state = 0;
    if (player != nullptr)
        state = *reinterpret_cast<const uint32_t *>(player + Offsets::OFF_UNIT_SHEATH_STATE);
    if (state > 2)
        state = 0;
    Game::Lua::PushNumber(L, static_cast<double>(state + 1));
    return 1;
}

void RegisterLuaFunctions() {
    Game::Lua::RegisterGlobalFunction("GetSheathState", &Script_GetSheathState);
}

const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace

} // namespace Unit::Sheath
