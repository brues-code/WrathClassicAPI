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

#include "item/Arg.h"

#include "Game.h"

#include <cstdlib>
#include <cstring>

namespace Item::Arg {

Resolved Resolve(void *L, int idx) {
    Resolved out{0, nullptr};
    if (Game::Lua::IsNumber(L, idx)) {
        out.itemID = static_cast<int>(Game::Lua::ToNumber(L, idx));
        return out;
    }
    if (Game::Lua::Type(L, idx) != Game::Lua::TYPE_STRING) {
        return out;
    }
    const char *s = Game::Lua::ToString(L, idx);
    if (s == nullptr) {
        return out;
    }
    if (const char *m = std::strstr(s, "item:")) {
        out.itemID = std::atoi(m + 5);
        return out;
    }
    // Plain string — try numeric, otherwise expose as a name for
    // callers that support name-based matching.
    const int numeric = std::atoi(s);
    if (numeric > 0) {
        out.itemID = numeric;
    } else {
        out.name = s;
    }
    return out;
}

int ResolveItemID(void *L, int idx) {
    return Resolve(L, idx).itemID;
}

} // namespace Item::Arg
