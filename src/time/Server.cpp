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

#include "Server.h"

#include "Game.h"
#include "Offsets.h"

#include <cstdint>
#include <ctime>

#include <windows.h>

namespace Time::Server {

namespace {

// Interpolation anchor. The 3.3.5 wire protocol carries time at minute
// granularity — `SMSG_LOGIN_SETTIMESPEED`'s packed gametime field has
// no seconds — so the engine's stored hour/minute fields only step
// every minute. To produce a Unix timestamp that ticks every second
// (which is what callers of `GetServerTime` actually need), we anchor
// to `GetTickCount` whenever we observe the engine's minute change,
// then add `(now - anchor) / 1000` to interpolate within the minute.
//
// First-call accuracy: we have no way to know how far into the current
// minute we are when the engine first reports it, so the cold-start
// minute is reported as :00 (off by 0..59 seconds). After the first
// minute rollover we observe, the anchor lands at the rollover boundary
// and subsequent calls are accurate.
int g_lastYear = 0;
int g_lastMonth = -1;
int g_lastDay = -1;
int g_lastHour = -1;
int g_lastMinute = -1;
DWORD g_anchorTick = 0;

int64_t ComputeCurrentEpoch() {
    auto *base = reinterpret_cast<const uint8_t *>(
        static_cast<uintptr_t>(Offsets::VAR_GAMETIME_STRUCT));

    const int year = *reinterpret_cast<const int *>(base + Offsets::OFF_GAMETIME_YEAR);
    if (year <= 0)
        return 0; // pre-login / not yet sync'd

    const int month = *reinterpret_cast<const int *>(base + Offsets::OFF_GAMETIME_MONTH);
    const int day = *reinterpret_cast<const int *>(base + Offsets::OFF_GAMETIME_DAY);
    const int hour = *reinterpret_cast<const int *>(base + Offsets::OFF_GAMETIME_HOUR);
    const int minute = *reinterpret_cast<const int *>(base + Offsets::OFF_GAMETIME_MINUTE);

    const DWORD now = GetTickCount();
    if (year != g_lastYear || month != g_lastMonth || day != g_lastDay ||
        hour != g_lastHour || minute != g_lastMinute) {
        g_lastYear = year;
        g_lastMonth = month;
        g_lastDay = day;
        g_lastHour = hour;
        g_lastMinute = minute;
        g_anchorTick = now;
    }

    std::tm t{};
    // Engine stores `year - 2000` (e.g. 8 = 2008). `tm_year` is years
    // since 1900, so add 100. The `% 100` is defensive against builds
    // that might store the full year instead — both shapes normalize
    // to the same `tm_year`.
    t.tm_year = (year % 100) + 100;
    t.tm_mon = month;        // engine stores 0-based, feeds tm_mon directly
    t.tm_mday = day + 1;     // engine stores 0-based, engine `+1`s before exposing
    t.tm_hour = hour;
    t.tm_min = minute;
    t.tm_sec = 0;
    t.tm_isdst = -1;

    const std::time_t minuteStart = _mkgmtime(&t);
    if (minuteStart < 0)
        return 0;

    // GetTickCount is unsigned and wraps every ~49 days; subtraction
    // in DWORD arithmetic gives the right elapsed value across
    // wraparound.
    const DWORD elapsedMs = now - g_anchorTick;
    int elapsedSec = static_cast<int>(elapsedMs / 1000);
    if (elapsedSec > 59) elapsedSec = 59; // clamp at minute end

    return static_cast<int64_t>(minuteStart) + elapsedSec;
}

// `GetServerTime()` — returns the current server clock as a Unix
// epoch timestamp (seconds since 1970-01-01 UTC). 3.3.5's stock
// `GetTime()` is frame-relative seconds-since-login, useless for any
// addon that needs wall-clock alignment (calendar, log timestamps,
// cooldown sync).
//
// Reads year/month/day/hour/minute from the engine's game-time struct
// at `VAR_GAMETIME_STRUCT` and converts via `_mkgmtime`, with
// `GetTickCount`-based interpolation between minute boundaries.
//
// Returns `nil` before login while the struct is BSS-zero.
int __cdecl Script_GetServerTime(void *L) {
    const int64_t epoch = ComputeCurrentEpoch();
    if (epoch <= 0)
        return 0;
    Game::Lua::PushNumber(L, static_cast<double>(epoch));
    return 1;
}

void RegisterLuaFunctions() {
    Game::Lua::RegisterGlobalFunction("GetServerTime", &Script_GetServerTime);
}

const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace

int64_t CurrentEpoch() { return ComputeCurrentEpoch(); }

} // namespace Time::Server
