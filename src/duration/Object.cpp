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

// `C_DurationUtil.CreateDuration()` and `Enum.DurationTimeModifier`
// (DurationUtilDocumentation.lua, LuaDurationObjectAPIDocumentation.lua), the
// port of ClassicAPI's src/duration/Object.cpp. The object is C in retail, so
// what follows is retail's behaviour as ClassicAPI probed it in a 12.1.5
// client:
//
//   - The object is a userdata, tostring "LuaDurationObject: <address>",
//     getmetatable false. A span is a start time, a total and a mod rate.
//   - Elapsed is now - start clamped to [0, total]; remaining is total -
//     elapsed, so before the start it is the whole total. The percents are
//     0 to 1; a zero span is 100% elapsed, 0% remaining.
//   - HasStarted is now >= start, HasExpired now >= end, IsActive between
//     them. A fresh or Reset object (start 0, total 0) has started and
//     expired.
//   - SetTimeSpan with the end before the start keeps the start and a zero
//     total. SetTimeFromEnd sets the start to end - duration.
//   - modRate leaves every real-time answer alone. In base time the total is
//     total / modRate from the same start: 1 s into a 4 s span at mod rate 2
//     has 1 s left of a 2 s total.
//   - Copy is independent of the original; Reset does not reach a copy.
//
// Times are GetTime() seconds on GetTime()'s clock (time/Clock), so a span
// straddling the 2^32 ms wrap of that clock jumps exactly as GetTime() does.
//
// The Format* methods hand the value to a Text::NumericFormatter. Not ported:
// the Evaluate* methods (retail curve objects) and SetClock /
// C_DurationUtil.CreateManualClock. There are no secret values, so
// HasSecretValues is always false.

#include "duration/Object.h"

#include "Game.h"
#include "text/NumericFormatter.h"
#include "time/Clock.h"

#include <cmath>
#include <cstdio>

namespace Duration::Object {

namespace Lua = Game::Lua;

struct Span {
    int refs = 0;
    double start = 0.0;
    double total = 0.0;
    double modRate = 1.0;
};

void Retain(Span *s) { ++s->refs; }

void Release(Span *s) {
    if (s != nullptr && --s->refs == 0)
        delete s;
}

namespace {

// Enum.DurationTimeModifier.
enum Modifier : int {
    MODIFIER_REAL_TIME = 0,
    MODIFIER_BASE_TIME = 1,
};

constexpr const char *kMetatableKey = "WrathClassicAPI.LuaDurationObject";

void PushMetatable(void *L) {
    Lua::PushString(L, kMetatableKey);
    Lua::RawGet(L, Lua::REGISTRY_INDEX);
}

int AbsIndex(void *L, int idx) { return idx > 0 ? idx : Lua::GetTop(L) + idx + 1; }

double Total(const Span &s, int m) { return m == MODIFIER_BASE_TIME ? s.total / s.modRate : s.total; }

double Elapsed(const Span &s, int m, double now) {
    const double e = now - s.start;
    const double total = Total(s, m);
    return e < 0.0 ? 0.0 : (e > total ? total : e);
}

double ElapsedPct(const Span &s, int m, double now) {
    const double total = Total(s, m);
    return total > 0.0 ? Elapsed(s, m, now) / total : 1.0;
}

double RemainingPct(const Span &s, int m, double now) {
    const double total = Total(s, m);
    return total > 0.0 ? (total - Elapsed(s, m, now)) / total : 0.0;
}

void Set(Span &s, double start, double total, double modRate) {
    s.start = start;
    s.total = total > 0.0 ? total : 0.0;
    s.modRate = modRate;
}

// ---- arguments ------------------------------------------------------------------

// luaL_error does not unwind C++ frames, so every caller returns right after.
void Usage(void *L, int arg, const char *usage) { Lua::Error(L, "bad argument #%d to '?' (Usage: %s)", arg, usage); }

Span *Self(void *L, const char *usage) {
    Span *s = ToSpan(L, 1);
    if (s == nullptr)
        Usage(L, 1, usage);
    return s;
}

// A finite number at `idx`; raises the usage error otherwise.
bool Number(void *L, int idx, const char *usage, double &out) {
    out = Lua::Type(L, idx) == Lua::TYPE_NUMBER ? Lua::ToNumber(L, idx) : NAN;
    if (std::isfinite(out))
        return true;
    Usage(L, idx, usage);
    return false;
}

// The optional mod rate at `idx`: 1 when nil, else a positive number.
bool ModRate(void *L, int idx, const char *usage, double &out) {
    out = 1.0;
    if (Lua::Type(L, idx) <= Lua::TYPE_NIL)
        return true;
    if (Number(L, idx, usage, out) && out > 0.0)
        return true;
    Usage(L, idx, usage);
    return false;
}

// The optional DurationTimeModifier at `idx`: RealTime when nil.
bool ModifierArg(void *L, int idx, const char *usage, int &out) {
    out = MODIFIER_REAL_TIME;
    if (Lua::Type(L, idx) <= Lua::TYPE_NIL)
        return true;
    const double n = Lua::Type(L, idx) == Lua::TYPE_NUMBER ? Lua::ToNumber(L, idx) : NAN;
    if (n == MODIFIER_REAL_TIME || n == MODIFIER_BASE_TIME) {
        out = static_cast<int>(n);
        return true;
    }
    Usage(L, idx, usage);
    return false;
}

// ---- the methods ----------------------------------------------------------------

// The getters that take a modifier: `self:Get...([modifier])`.
template <typename Answer> int Query(void *L, const char *usage, Answer answer) {
    Span *s = Self(L, usage);
    int m;
    if (s == nullptr || !ModifierArg(L, 2, usage, m))
        return 0;
    answer(*s, m, Time::Clock::NowSeconds());
    return 1;
}

int __cdecl Script_GetStartTime(void *L) {
    return Query(L, "local startTime = self:GetStartTime([modifier])",
                 [L](const Span &s, int, double) { Lua::PushNumber(L, s.start); });
}

int __cdecl Script_GetEndTime(void *L) {
    return Query(L, "local endTime = self:GetEndTime([modifier])",
                 [L](const Span &s, int m, double) { Lua::PushNumber(L, s.start + Total(s, m)); });
}

int __cdecl Script_GetTotalDuration(void *L) {
    return Query(L, "local totalDuration = self:GetTotalDuration([modifier])",
                 [L](const Span &s, int m, double) { Lua::PushNumber(L, Total(s, m)); });
}

int __cdecl Script_GetElapsedDuration(void *L) {
    return Query(L, "local elapsedDuration = self:GetElapsedDuration([modifier])",
                 [L](const Span &s, int m, double now) { Lua::PushNumber(L, Elapsed(s, m, now)); });
}

int __cdecl Script_GetRemainingDuration(void *L) {
    return Query(L, "local remainingDuration = self:GetRemainingDuration([modifier])",
                 [L](const Span &s, int m, double now) { Lua::PushNumber(L, Total(s, m) - Elapsed(s, m, now)); });
}

int __cdecl Script_GetElapsedPercent(void *L) {
    return Query(L, "local elapsedPercent = self:GetElapsedPercent([modifier])",
                 [L](const Span &s, int m, double now) { Lua::PushNumber(L, ElapsedPct(s, m, now)); });
}

int __cdecl Script_GetRemainingPercent(void *L) {
    return Query(L, "local remainingPercent = self:GetRemainingPercent([modifier])",
                 [L](const Span &s, int m, double now) { Lua::PushNumber(L, RemainingPct(s, m, now)); });
}

int __cdecl Script_HasStarted(void *L) {
    return Query(L, "local hasStarted = self:HasStarted([modifier])",
                 [L](const Span &s, int, double now) { Lua::PushBool(L, now >= s.start); });
}

int __cdecl Script_HasExpired(void *L) {
    return Query(L, "local hasExpired = self:HasExpired([modifier])",
                 [L](const Span &s, int m, double now) { Lua::PushBool(L, now >= s.start + Total(s, m)); });
}

int __cdecl Script_IsActive(void *L) {
    return Query(L, "local isActive = self:IsActive([modifier])", [L](const Span &s, int m, double now) {
        Lua::PushBool(L, now >= s.start && now < s.start + Total(s, m));
    });
}

enum Property {
    PROPERTY_ELAPSED,
    PROPERTY_REMAINING,
    PROPERTY_TOTAL,
};

// `self:Format...Duration(formatter [, modifier])`: the formatter's text for
// the value, or its format error, raised as FormatNumber raises it. Both
// objects stay alive through the call: the arguments hold them.
int Format(void *L, const char *usage, const char *method, Property property) {
    Span *s = Self(L, usage);
    if (s == nullptr)
        return 0;
    Text::NumericFormatter::Formatter *f = Text::NumericFormatter::ToFormatter(L, 2);
    if (f == nullptr) {
        Usage(L, 2, usage);
        return 0;
    }
    int m;
    if (!ModifierArg(L, 3, usage, m))
        return 0;
    const double now = Time::Clock::NowSeconds();
    const double value = property == PROPERTY_ELAPSED   ? Elapsed(*s, m, now)
                         : property == PROPERTY_REMAINING ? Total(*s, m) - Elapsed(*s, m, now)
                                                          : Total(*s, m);
    if (Text::NumericFormatter::Format(L, f, value) == nullptr) {
        const char *msg = Lua::ToString(L, -1);
        Lua::Error(L, "LuaDurationObject:%s(): %s", method, msg != nullptr ? msg : "?");
    }
    return 1;
}

int __cdecl Script_FormatElapsedDuration(void *L) {
    return Format(L, "local formatted = self:FormatElapsedDuration(formatter [, modifier])",
                  "FormatElapsedDuration", PROPERTY_ELAPSED);
}

int __cdecl Script_FormatRemainingDuration(void *L) {
    return Format(L, "local formatted = self:FormatRemainingDuration(formatter [, modifier])",
                  "FormatRemainingDuration", PROPERTY_REMAINING);
}

int __cdecl Script_FormatTotalDuration(void *L) {
    return Format(L, "local formatted = self:FormatTotalDuration(formatter [, modifier])", "FormatTotalDuration",
                  PROPERTY_TOTAL);
}

int __cdecl Script_IsZero(void *L) {
    Span *s = Self(L, "local isZero = self:IsZero()");
    if (s == nullptr)
        return 0;
    Lua::PushBool(L, s->total == 0.0);
    return 1;
}

int __cdecl Script_GetModRate(void *L) {
    Span *s = Self(L, "local modRate = self:GetModRate()");
    if (s == nullptr)
        return 0;
    Lua::PushNumber(L, s->modRate);
    return 1;
}

// Nil: the default clock, GetTime().
int __cdecl Script_GetClock(void *L) {
    if (Self(L, "local clock = self:GetClock()") == nullptr)
        return 0;
    Lua::PushNil(L);
    return 1;
}

int __cdecl Script_GetClockTime(void *L) {
    if (Self(L, "local clockTime = self:GetClockTime()") == nullptr)
        return 0;
    Lua::PushNumber(L, Time::Clock::NowSeconds());
    return 1;
}

int __cdecl Script_HasSecretValues(void *L) {
    if (Self(L, "local hasSecretValues = self:HasSecretValues()") == nullptr)
        return 0;
    Lua::PushBool(L, false);
    return 1;
}

int __cdecl Script_SetTimeFromStart(void *L) {
    constexpr const char *kUsage = "self:SetTimeFromStart(startTime, duration [, modRate])";
    Span *s = Self(L, kUsage);
    double start, duration, modRate;
    if (s == nullptr || !Number(L, 2, kUsage, start) || !Number(L, 3, kUsage, duration) ||
        !ModRate(L, 4, kUsage, modRate))
        return 0;
    Set(*s, start, duration, modRate);
    return 0;
}

int __cdecl Script_SetTimeFromEnd(void *L) {
    constexpr const char *kUsage = "self:SetTimeFromEnd(endTime, duration [, modRate])";
    Span *s = Self(L, kUsage);
    double end, duration, modRate;
    if (s == nullptr || !Number(L, 2, kUsage, end) || !Number(L, 3, kUsage, duration) ||
        !ModRate(L, 4, kUsage, modRate))
        return 0;
    if (duration < 0.0)
        duration = 0.0;
    Set(*s, end - duration, duration, modRate);
    return 0;
}

// Takes no mod rate, so the span has none.
int __cdecl Script_SetTimeSpan(void *L) {
    constexpr const char *kUsage = "self:SetTimeSpan(startTime, endTime)";
    Span *s = Self(L, kUsage);
    double start, end;
    if (s == nullptr || !Number(L, 2, kUsage, start) || !Number(L, 3, kUsage, end))
        return 0;
    Set(*s, start, end - start, 1.0);
    return 0;
}

int __cdecl Script_Reset(void *L) {
    if (Span *s = Self(L, "self:Reset()"))
        Set(*s, 0.0, 0.0, 1.0);
    return 0;
}

// Reset plus retail's secret flag and clock, neither of which exists here.
int __cdecl Script_SetToDefaults(void *L) {
    if (Span *s = Self(L, "self:SetToDefaults()"))
        Set(*s, 0.0, 0.0, 1.0);
    return 0;
}

int __cdecl Script_Assign(void *L) {
    constexpr const char *kUsage = "self:Assign(other)";
    Span *s = Self(L, kUsage);
    if (s == nullptr)
        return 0;
    const Span *other = ToSpan(L, 2);
    if (other == nullptr) {
        Usage(L, 2, kUsage);
        return 0;
    }
    Set(*s, other->start, other->total, other->modRate);
    return 0;
}

int __cdecl Script_Copy(void *L) {
    Span *s = Self(L, "local copy = self:Copy()");
    if (s == nullptr)
        return 0;
    Span *copy = new Span;
    Set(*copy, s->start, s->total, s->modRate);
    Push(L, copy);
    return 1;
}

int __cdecl Meta_gc(void *L) {
    auto **block = static_cast<Span **>(Lua::ToUserdata(L, 1));
    if (block != nullptr) {
        Release(*block);
        *block = nullptr;
    }
    return 0;
}

int __cdecl Meta_tostring(void *L) {
    char text[48];
    std::snprintf(text, sizeof(text), "LuaDurationObject: %p", static_cast<void *>(ToSpan(L, 1)));
    Lua::PushString(L, text);
    return 1;
}

int __cdecl Script_CreateDuration(void *L) {
    Push(L, new Span);
    return 1;
}

struct Method {
    const char *name;
    Lua::CFunction fn;
};
const Method kMethods[] = {
    {"Assign", &Script_Assign},
    {"Copy", &Script_Copy},
    {"FormatElapsedDuration", &Script_FormatElapsedDuration},
    {"FormatRemainingDuration", &Script_FormatRemainingDuration},
    {"FormatTotalDuration", &Script_FormatTotalDuration},
    {"GetClock", &Script_GetClock},
    {"GetClockTime", &Script_GetClockTime},
    {"GetElapsedDuration", &Script_GetElapsedDuration},
    {"GetElapsedPercent", &Script_GetElapsedPercent},
    {"GetEndTime", &Script_GetEndTime},
    {"GetModRate", &Script_GetModRate},
    {"GetRemainingDuration", &Script_GetRemainingDuration},
    {"GetRemainingPercent", &Script_GetRemainingPercent},
    {"GetStartTime", &Script_GetStartTime},
    {"GetTotalDuration", &Script_GetTotalDuration},
    {"HasExpired", &Script_HasExpired},
    {"HasSecretValues", &Script_HasSecretValues},
    {"HasStarted", &Script_HasStarted},
    {"IsActive", &Script_IsActive},
    {"IsZero", &Script_IsZero},
    {"Reset", &Script_Reset},
    {"SetTimeFromEnd", &Script_SetTimeFromEnd},
    {"SetTimeFromStart", &Script_SetTimeFromStart},
    {"SetTimeSpan", &Script_SetTimeSpan},
    {"SetToDefaults", &Script_SetToDefaults},
};

void SetFunction(void *L, const char *key, Lua::CFunction fn) {
    Lua::PushString(L, key);
    Lua::PushCClosure(L, fn, 0);
    Lua::RawSet(L, -3);
}

// registry[kMetatableKey] = {__index = methods, __gc, __tostring, __metatable = false}
void CreateMetatable(void *L) {
    PushMetatable(L);
    const bool exists = Lua::Type(L, -1) == Lua::TYPE_TABLE;
    Lua::SetTop(L, -2);
    if (exists)
        return;
    Lua::PushString(L, kMetatableKey);
    Lua::NewTable(L);
    Lua::PushString(L, "__index");
    Lua::NewTable(L);
    for (const Method &m : kMethods)
        SetFunction(L, m.name, m.fn);
    Lua::RawSet(L, -3);
    SetFunction(L, "__gc", &Meta_gc);
    SetFunction(L, "__tostring", &Meta_tostring);
    Lua::PushString(L, "__metatable");
    Lua::PushBool(L, false);
    Lua::RawSet(L, -3);
    Lua::RawSet(L, Lua::REGISTRY_INDEX);
}

const Lua::EnumIntegerEntry kModifiers[] = {
    {"RealTime", MODIFIER_REAL_TIME},
    {"BaseTime", MODIFIER_BASE_TIME},
};

void RegisterLuaFunctions() {
    CreateMetatable(Lua::State());
    Lua::RegisterTableFunction("C_DurationUtil", "CreateDuration", &Script_CreateDuration);
    Lua::RegisterIntegerEnum("Enum", "DurationTimeModifier", kModifiers,
                             static_cast<int>(sizeof(kModifiers) / sizeof(kModifiers[0])));
}

// Pure Lua objects: both states have them.
const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};
const Game::GlueModuleAutoRegister _autoregGlue{&RegisterLuaFunctions};

} // namespace

Span *ToSpan(void *L, int idx) {
    if (Lua::Type(L, idx) != Lua::TYPE_USERDATA)
        return nullptr;
    idx = AbsIndex(L, idx);
    if (!Lua::GetMetatable(L, idx))
        return nullptr;
    PushMetatable(L);
    const bool ours = Lua::ToPointer(L, -1) == Lua::ToPointer(L, -2);
    Lua::SetTop(L, -3);
    return ours ? *static_cast<Span **>(Lua::ToUserdata(L, idx)) : nullptr;
}

// The reference is taken before the allocation, which can run a collection.
void Push(void *L, Span *s) {
    Retain(s);
    auto **block = static_cast<Span **>(Lua::NewUserdata(L, sizeof(Span *)));
    *block = s;
    PushMetatable(L);
    Lua::SetMetatable(L, -2);
}

void PushFromMs(void *L, uint32_t startMs, uint32_t endMs) {
    Span *s = new Span;
    Set(*s, startMs * 0.001, static_cast<int32_t>(endMs - startMs) * 0.001, 1.0);
    Push(L, s);
}

double ElapsedPercent(const Span *s) { return ElapsedPct(*s, MODIFIER_REAL_TIME, Time::Clock::NowSeconds()); }

double RemainingPercent(const Span *s) { return RemainingPct(*s, MODIFIER_REAL_TIME, Time::Clock::NowSeconds()); }

} // namespace Duration::Object
