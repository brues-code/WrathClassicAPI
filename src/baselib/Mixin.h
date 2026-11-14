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

namespace BaseLib::Mixin {

// `Mixin(dst, src)`: copies every pair of the table at `srcIdx` onto the table
// at `dstIdx` (both absolute stack indices). Leaves the stack as it found it.
void Into(void *L, int dstIdx, int srcIdx);

} // namespace BaseLib::Mixin
