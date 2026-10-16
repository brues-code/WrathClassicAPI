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

#pragma once

#include "Game.h"

// Shared owner of the Frame method-table walker hook (FUN_FRAME_METHODS_WALKER).
// Every frame subtype's walker calls that one first, so an entry appended there
// reaches every frame type. A target carries only one detour, so this module owns
// the hook and appends each subscriber's table after the engine's own — a name
// that already exists (e.g. IsEventRegistered) is replaced.
//
// Declare a file-scope
//   static const Frame::Methods::AutoRegister _methods{kTable, std::size(kTable)};

namespace Frame::Methods {

// (name, func) pair in the engine's 8-byte method-table layout.
struct Entry {
    const char *name;
    Game::Lua::CFunction func;
};

struct AutoRegister {
    AutoRegister(const Entry *table, int count);
    const Entry *table;
    int count;
    AutoRegister *next;
};

} // namespace Frame::Methods
