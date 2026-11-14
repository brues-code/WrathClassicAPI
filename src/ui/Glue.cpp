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

// `C_Glue.IsOnGlueScreen()` -> bool: true at the login and character
// screens, false in the world. Each Lua state gets its own answer.

#include "Game.h"

namespace UI::Glue {

namespace {

int __cdecl Script_IsOnGlueScreenInGame(void *L) {
    Game::Lua::PushBool(L, false);
    return 1;
}

int __cdecl Script_IsOnGlueScreenInGlue(void *L) {
    Game::Lua::PushBool(L, true);
    return 1;
}

void RegisterInGame() { Game::Lua::RegisterTableFunction("C_Glue", "IsOnGlueScreen", &Script_IsOnGlueScreenInGame); }
void RegisterGlue() { Game::Lua::RegisterTableFunction("C_Glue", "IsOnGlueScreen", &Script_IsOnGlueScreenInGlue); }

const Game::ModuleAutoRegister _autoreg{&RegisterInGame};
const Game::GlueModuleAutoRegister _autoregGlue{&RegisterGlue};

} // namespace

} // namespace UI::Glue
