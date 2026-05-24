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

// `WRATH_CLASSIC_API_VERSION` global — a single integer addons can read
// for feature-gating (`if WRATH_CLASSIC_API_VERSION >= 10200 then ...`).
// The value is baked at build time from `WRATHCLASSICAPI_VERSION_VALUE`
// (set by CMake from the `v{MAJOR}.{MINOR}.{PATCH}` tag) and re-published
// on every `/reload` via the standard `ModuleAutoRegister` flow.

#include "Game.h"

namespace Api::Version {

namespace {

void RegisterLuaFunctions() {
    void *L = Game::Lua::State();
    if (L == nullptr)
        return;
    Game::Lua::SetGlobalNumber(L, "WRATH_CLASSIC_API_VERSION",
                               static_cast<double>(WRATHCLASSICAPI_VERSION_VALUE));
}

const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace

} // namespace Api::Version
