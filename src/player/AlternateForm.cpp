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

// `C_PlayerInfo.GetAlternateFormInfo()` -> hasAlternateForm, inAlternateForm
//
// Whether the player's race has a second form (the later clients' worgen
// and dracthyr) and is in it. No race of this client has one.

#include "Game.h"

namespace Player::AlternateForm {

namespace {

int __cdecl Script_GetAlternateFormInfo(void *L) {
    Game::Lua::PushBool(L, false);
    Game::Lua::PushBool(L, false);
    return 2;
}

void RegisterLuaFunctions() {
    Game::Lua::RegisterTableFunction("C_PlayerInfo", "GetAlternateFormInfo", &Script_GetAlternateFormInfo);
}

const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace

} // namespace Player::AlternateForm
