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

#pragma once

#include <cstdint>

namespace Time::Server {

// Reads the engine's game-time struct and returns the current server
// clock as a Unix epoch (seconds since 1970-01-01 UTC). Same
// interpolation trick `Script_GetServerTime` uses — `GetTickCount`
// deltas fill in the sub-minute portion that the wire protocol doesn't
// carry.
//
// Returns 0 before login while the struct is BSS-zero. Cold-start
// accuracy: the first reported minute lands at :00 (off by up to
// 59s); subsequent calls are accurate within a few hundred ms.
int64_t CurrentEpoch();

} // namespace Time::Server
