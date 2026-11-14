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

#include "frame/Methods.h"

// Owner of the StatusBar method-table walker hook
// (FUN_STATUSBAR_REGISTER_SCRIPT_METHODS), as frame/Methods owns the Frame
// one: each subscriber's table is appended after the engine's own, so a name
// that already exists (SetValue) is replaced.
//
// Declare a file-scope
//   static const StatusBar::Methods::AutoRegister _methods{kTable, std::size(kTable)};
namespace StatusBar::Methods {

struct AutoRegister {
    AutoRegister(const Frame::Methods::Entry *table, int count);
    const Frame::Methods::Entry *table;
    int count;
    AutoRegister *next;
};

} // namespace StatusBar::Methods
