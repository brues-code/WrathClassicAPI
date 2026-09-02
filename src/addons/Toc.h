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

#include <cstddef>

namespace Addons::Toc {

// Fold an ASCII letter to lower case (leaves every other byte unchanged).
inline char Lower(char c) {
    return (c >= 'A' && c <= 'Z') ? static_cast<char>(c + 32) : c;
}

// Case-insensitive equality of the `n` bytes at `s` against the NUL-terminated
// `lit` (ASCII). True only when the lengths match too — used to compare TOC
// directive keywords and `[...]` tokens against known names.
inline bool EqCI(const char *s, size_t n, const char *lit) {
    for (size_t i = 0; i < n; ++i)
        if (lit[i] == '\0' || Lower(s[i]) != Lower(lit[i]))
            return false;
    return lit[n] == '\0';
}

} // namespace Addons::Toc
