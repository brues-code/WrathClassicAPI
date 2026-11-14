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

// Shared owner of the Texture method-table walker hook
// (FUN_TEXTURE_METHODS_WALKER). A target carries only one detour, so this module
// owns the hook and appends each subscriber's table after the engine's own — a
// name that already exists (e.g. SetDesaturated) is replaced.
//
// Declare a file-scope
//   static const Texture::Methods::AutoRegister _methods{kTable, std::size(kTable)};
namespace Texture::Methods {

struct AutoRegister {
    AutoRegister(const Frame::Methods::Entry *table, int count);
    const Frame::Methods::Entry *table;
    int count;
    AutoRegister *next;
};

} // namespace Texture::Methods
