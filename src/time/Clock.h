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

#include "Offsets.h"

#include <cstdint>

// The clock GetTime() reads: OsGetAsyncTimeMs, a wrapping millisecond count.
namespace Time::Clock {

inline uint32_t NowMs() { return reinterpret_cast<uint32_t(__cdecl *)()>(Offsets::FUN_OS_GET_ASYNC_TIME_MS)(); }

// GetTime() now (Script_GetTime 0x006081F0: OsGetAsyncTimeMs() * 0.001).
inline double NowSeconds() { return NowMs() * 0.001; }

} // namespace Time::Clock
