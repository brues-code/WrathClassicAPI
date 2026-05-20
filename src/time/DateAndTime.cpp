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

// `C_DateAndTime.*` — backport of modern's date-math API. Six of the
// seven functions we ship are pure arithmetic on a `CalendarTime`
// table (year/month/monthDay/weekday/hour/minute) — no engine state
// touched at all. The seventh, `GetSecondsUntilDailyReset`, treats
// reset as midnight in *server* wall-clock time. That falls out of
// `Time::Server::CurrentEpoch()` naturally: that helper converts the
// engine's server-time fields to an epoch by treating them AS UTC,
// so `epoch % 86400 == 0` lands precisely on server midnight.
//
// `GetSecondsUntilWeeklyReset` is intentionally omitted — 3.3.5 has
// no server-broadcast reset schedule and private-server realms vary,
// so any hardcoded weekday/hour would be wrong somewhere. The few
// addons that need it can compute their own from the daily reset.
//
// CalendarTime field conventions (per Blizzard's modern
// `TimeDocumentation.lua`):
//
//   year     full year (e.g. 2026)
//   month    1..12       (luaIndex)
//   monthDay 1..31       (luaIndex)
//   weekday  1..7        (luaIndex; 1 = Sunday)
//   hour     0..23
//   minute   0..59
//
// C's `std::tm` uses different bases for some of these: `tm_year` is
// years-since-1900, `tm_mon` is 0..11, `tm_wday` is 0..6 with
// Sunday=0. The pack/unpack helpers below translate.

#include "Game.h"
#include "Offsets.h"
#include "Server.h"

#include <cstdint>
#include <ctime>

namespace Time::DateAndTime {

namespace {

constexpr int SECONDS_PER_DAY = 86400;
constexpr int SECONDS_PER_MINUTE = 60;

// Pushes a fresh CalendarTime table at the top of the stack. Caller
// owns the stack — clear it before invoking if you need the new table
// at a specific index.
void PushCalendarTime(void *L, const std::tm &t) {
    Game::Lua::NewTable(L);
    Game::Lua::SetFieldNumber(L, "year",     static_cast<double>(t.tm_year + 1900));
    Game::Lua::SetFieldNumber(L, "month",    static_cast<double>(t.tm_mon + 1));
    Game::Lua::SetFieldNumber(L, "monthDay", static_cast<double>(t.tm_mday));
    Game::Lua::SetFieldNumber(L, "weekday",  static_cast<double>(t.tm_wday + 1));
    Game::Lua::SetFieldNumber(L, "hour",     static_cast<double>(t.tm_hour));
    Game::Lua::SetFieldNumber(L, "minute",   static_cast<double>(t.tm_min));
}

// Reads a numeric field off the table at `tableIdx`. Returns false
// if the field is missing or non-numeric, leaving `*out` untouched.
// Always restores the stack to its entry state.
bool ReadField(void *L, int tableIdx, const char *key, int *out) {
    Game::Lua::GetField(L, tableIdx, key);
    const bool ok = Game::Lua::IsNumber(L, -1);
    if (ok)
        *out = static_cast<int>(Game::Lua::ToNumber(L, -1));
    Game::Lua::SetTop(L, -2);
    return ok;
}

// Reads a CalendarTime table at `idx` into a `std::tm`. Doesn't set
// `tm_wday` / `tm_yday` — those are output-only for `_mkgmtime`.
// Returns false if the required fields are missing.
bool ReadCalendarTime(void *L, int idx, std::tm *out) {
    if (Game::Lua::Type(L, idx) != Game::Lua::TYPE_TABLE)
        return false;
    int year = 0, month = 0, monthDay = 0, hour = 0, minute = 0;
    if (!ReadField(L, idx, "year", &year))         return false;
    if (!ReadField(L, idx, "month", &month))       return false;
    if (!ReadField(L, idx, "monthDay", &monthDay)) return false;
    // hour/minute may legitimately be 0 — only require their presence.
    ReadField(L, idx, "hour", &hour);
    ReadField(L, idx, "minute", &minute);

    *out = std::tm{};
    out->tm_year = year - 1900;
    out->tm_mon = month - 1;
    out->tm_mday = monthDay;
    out->tm_hour = hour;
    out->tm_min = minute;
    out->tm_sec = 0;
    out->tm_isdst = -1;
    return true;
}

// Round-trip helper: turn an input CalendarTime into an epoch, add
// `deltaSec`, decompose back into a fresh CalendarTime, push it.
int AdjustAndPush(void *L, int deltaSec) {
    std::tm in{};
    if (!ReadCalendarTime(L, 1, &in))
        return 0;
    std::time_t epoch = _mkgmtime(&in);
    if (epoch < 0)
        return 0;
    epoch += deltaSec;
    std::tm out{};
    if (gmtime_s(&out, &epoch) != 0)
        return 0;
    Game::Lua::SetTop(L, 0);
    PushCalendarTime(L, out);
    return 1;
}

int __cdecl Script_AdjustTimeByDays(void *L) {
    if (!Game::Lua::IsNumber(L, 2))
        return 0;
    const int days = static_cast<int>(Game::Lua::ToNumber(L, 2));
    return AdjustAndPush(L, days * SECONDS_PER_DAY);
}

int __cdecl Script_AdjustTimeByMinutes(void *L) {
    if (!Game::Lua::IsNumber(L, 2))
        return 0;
    const int minutes = static_cast<int>(Game::Lua::ToNumber(L, 2));
    return AdjustAndPush(L, minutes * SECONDS_PER_MINUTE);
}

// `CompareCalendarTime(lhs, rhs)` — returns -1, 0, or 1. We compare
// via epoch conversion rather than field-by-field tuple compare so
// out-of-range inputs (e.g. month=13) normalize consistently.
int __cdecl Script_CompareCalendarTime(void *L) {
    std::tm lhs{}, rhs{};
    if (!ReadCalendarTime(L, 1, &lhs) || !ReadCalendarTime(L, 2, &rhs))
        return 0;
    const std::time_t e1 = _mkgmtime(&lhs);
    const std::time_t e2 = _mkgmtime(&rhs);
    int cmp = 0;
    if (e1 < e2)      cmp = -1;
    else if (e1 > e2) cmp = 1;
    Game::Lua::PushNumber(L, static_cast<double>(cmp));
    return 1;
}

int __cdecl Script_GetCalendarTimeFromEpoch(void *L) {
    if (!Game::Lua::IsNumber(L, 1))
        return 0;
    std::time_t epoch = static_cast<std::time_t>(Game::Lua::ToNumber(L, 1));
    std::tm t{};
    if (gmtime_s(&t, &epoch) != 0)
        return 0;
    Game::Lua::SetTop(L, 0);
    PushCalendarTime(L, t);
    return 1;
}

int __cdecl Script_GetCurrentCalendarTime(void *L) {
    const int64_t epoch = Time::Server::CurrentEpoch();
    if (epoch <= 0)
        return 0;
    std::time_t e = static_cast<std::time_t>(epoch);
    std::tm t{};
    if (gmtime_s(&t, &e) != 0)
        return 0;
    Game::Lua::SetTop(L, 0);
    PushCalendarTime(L, t);
    return 1;
}

// `GetSecondsUntilDailyReset()` — uses the engine's actual daily-
// reset clock at `VAR_NEXT_DAILY_RESET_EPOCH`, populated by the
// server-broadcast calendar packet. That timestamp respects whatever
// schedule the server uses (3am Pacific on retail, arbitrary on
// private servers) — much more accurate than guessing
// "midnight UTC" from the gametime struct.
//
// Same math as the engine's own `Script_GetQuestResetTime`
// (FUN_005E6DE0): `nextReset - time(NULL)`. Returns nil if the
// server hasn't broadcast a reset epoch yet, or if the stored value
// is somehow past (defensive — engine bumps it forward).
int __cdecl Script_GetSecondsUntilDailyReset(void *L) {
    const int32_t nextReset = *reinterpret_cast<const int32_t *>(
        static_cast<uintptr_t>(Offsets::VAR_NEXT_DAILY_RESET_EPOCH));
    if (nextReset == 0)
        return 0; // server hasn't sent the schedule yet
    const int64_t remaining =
        static_cast<int64_t>(nextReset) - static_cast<int64_t>(std::time(nullptr));
    if (remaining <= 0)
        return 0;
    Game::Lua::PushNumber(L, static_cast<double>(remaining));
    return 1;
}

// `GetServerTimeLocal()` — modern returns the server's wall clock
// re-interpreted as local time and packed as a Unix epoch. The trick:
// take the server's UTC-style components, feed them to `mktime`
// (which interprets them as local time), and the resulting epoch is
// what addons want for `date(format, GetServerTimeLocal())` to print
// server-clock strings.
int __cdecl Script_GetServerTimeLocal(void *L) {
    const int64_t epoch = Time::Server::CurrentEpoch();
    if (epoch <= 0)
        return 0;
    std::time_t e = static_cast<std::time_t>(epoch);
    std::tm t{};
    if (gmtime_s(&t, &e) != 0)
        return 0;
    t.tm_isdst = -1;
    const std::time_t local = std::mktime(&t);
    if (local == -1)
        return 0;
    Game::Lua::PushNumber(L, static_cast<double>(local));
    return 1;
}

void RegisterLuaFunctions() {
    Game::Lua::RegisterTableFunction("C_DateAndTime", "AdjustTimeByDays",
                                     &Script_AdjustTimeByDays);
    Game::Lua::RegisterTableFunction("C_DateAndTime", "AdjustTimeByMinutes",
                                     &Script_AdjustTimeByMinutes);
    Game::Lua::RegisterTableFunction("C_DateAndTime", "CompareCalendarTime",
                                     &Script_CompareCalendarTime);
    Game::Lua::RegisterTableFunction("C_DateAndTime", "GetCalendarTimeFromEpoch",
                                     &Script_GetCalendarTimeFromEpoch);
    Game::Lua::RegisterTableFunction("C_DateAndTime", "GetCurrentCalendarTime",
                                     &Script_GetCurrentCalendarTime);
    Game::Lua::RegisterTableFunction("C_DateAndTime", "GetSecondsUntilDailyReset",
                                     &Script_GetSecondsUntilDailyReset);
    Game::Lua::RegisterTableFunction("C_DateAndTime", "GetServerTimeLocal",
                                     &Script_GetServerTimeLocal);
}

const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace
} // namespace Time::DateAndTime
