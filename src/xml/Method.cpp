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

#include "xml/Method.h"

#include "Game.h"
#include "Offsets.h"
#include "xml/Node.h"

namespace Xml::Method {

int Ref(void *L, int index, const char *method, void *status, const char *owner, const char *element) {
    Game::Lua::GetField(L, index, method); // [value]
    if (Game::Lua::Type(L, -1) != Game::Lua::TYPE_FUNCTION) {
        Game::Lua::SetTop(L, -2);
        Warn(status, "%s: Unknown method %s in element %s", owner, method, element);
        return 0;
    }
    return reinterpret_cast<int(__cdecl *)(void *, int)>(Offsets::LUAL_REF)(L, Game::Lua::REGISTRY_INDEX);
}

} // namespace Xml::Method
