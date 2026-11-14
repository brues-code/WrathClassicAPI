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

// `bar:SetTimerDuration(duration [, interpolation [, direction]])`,
// `GetTimerDuration()`, `bar:SetValue(value [, interpolation])`,
// `SetMinMaxValues(min, max [, interpolation])`, `GetInterpolatedValue()`,
// `IsInterpolating()`, `SetToTargetValue()`, `Enum.StatusBarTimerDirection`
// and `Enum.StatusBarInterpolation` (SimpleStatusBarAPIDocumentation.lua; the
// port of ClassicAPI's src/statusbar/Display.cpp, whose retail probes set the
// behaviour and numbers below).
//
// The timer:
//   - The bar fills by the duration's elapsed share (ElapsedTime) or its
//     remaining share (RemainingTime), every update. After the end it stays
//     full or empty; the timer stays set.
//   - The bar keeps the duration object, not a copy: a change made to the
//     object afterwards shows at the next update. GetTimerDuration returns a
//     new Lua object for it each call.
//   - The value and range read 0 while the timer runs, written straight into
//     the bar, so no OnValueChanged or OnMinMaxChanged runs. SetValue leaves
//     the timer running (and clamps to 0..0); SetMinMaxValues ends it, after
//     which GetTimerDuration returns nil. A zero duration is a timer too.
//   - A hidden bar does not move; it is right again on its first update shown.
//
// ExponentialEaseOut:
//   - The ease works on the fill FRACTION: on each update the shown fraction
//     closes on the target by exp(-18 * elapsed) of the gap, whatever the
//     range.
//   - When the gap before an update's step is under 0.001 and the target held
//     still since the last update, that update snaps to the target and
//     IsInterpolating turns false.
//   - SetValue stores the target at once: GetValue returns it, OnValueChanged
//     runs once, with it. GetInterpolatedValue returns the shown value.
//   - A new eased SetValue goes on from the shown value; an Immediate one, or
//     SetToTargetValue, snaps and stops.
//   - SetMinMaxValues(min, max, ExponentialEaseOut) keeps the fraction shown
//     and eases to the new one.
//   - A hidden bar does not ease; once shown it steps by each update's elapsed.
//   - An eased timer chases the timer's share with the same ease. Only a
//     target that held still snaps, so the chase goes on until the timer ends.
//
// CSimpleStatusBar::OnLayerUpdate draws the fill from the GetAnimValue
// virtual, not the stored value; its hook answers the shown fraction for a
// bar with a timer or an ease, so the engine's layout and statusbar/Fill's
// both draw it.
//
// SetValue and SetMinMaxValues are the engine's script methods (0x00971540,
// 0x009713C0) reproduced natively, with the same checks and errors: the
// fraction shown is read, the engine's setter runs, then the ease starts or
// stops. Only ExponentialEaseOut eases; any other value in the interpolation
// argument is Immediate, never an error, since the stock methods ignore extra
// arguments and existing code can pass one by accident (a second return value,
// `bar:SetValue(UnitHealth("target"), x)`).

#include "statusbar/Display.h"

#include "Game.h"
#include "Offsets.h"
#include "duration/Object.h"
#include "statusbar/Lifetime.h"
#include "statusbar/Methods.h"
#include "ui/FrameObject.h"

#include <cmath>
#include <cstdint>
#include <unordered_map>

namespace StatusBar::Display {

namespace {

namespace Lua = Game::Lua;

template <typename T> T &At(void *base, uintptr_t offset) {
    return *reinterpret_cast<T *>(static_cast<uint8_t *>(base) + offset);
}

enum Interpolation : int {
    INTERPOLATION_IMMEDIATE = 0,
    INTERPOLATION_EXPONENTIAL_EASE_OUT = 1,
};

const Lua::EnumIntegerEntry kInterpolations[] = {
    {"Immediate", INTERPOLATION_IMMEDIATE},
    {"ExponentialEaseOut", INTERPOLATION_EXPONENTIAL_EASE_OUT},
};

enum Direction : int {
    DIRECTION_ELAPSED_TIME = 0,
    DIRECTION_REMAINING_TIME = 1,
};

const Lua::EnumIntegerEntry kDirections[] = {
    {"ElapsedTime", DIRECTION_ELAPSED_TIME},
    {"RemainingTime", DIRECTION_REMAINING_TIME},
};

constexpr double kEaseRate = 18.0;     // per second, of the gap
constexpr float kEaseSnapGap = 0.001f; // of the fill fraction

// A bar with a timer, an ease, or both. A bar with neither has no entry.
struct State {
    Duration::Object::Span *timer = nullptr; // one reference
    int direction = DIRECTION_ELAPSED_TIME;
    bool easing = false;
    float shown = 0.0f;  // the fraction the fill draws
    float target = 0.0f; // the fraction headed for, as of the last update
};

using Bars = std::unordered_map<const void *, State>;
Bars g_bars;

void Forget(const void *bar) {
    const auto it = g_bars.find(bar);
    if (it == g_bars.end())
        return;
    Duration::Object::Release(it->second.timer);
    g_bars.erase(it);
}

const StatusBar::Lifetime::OnDestroyed _destroyed{&Forget};

void MarkChanged(void *bar) { At<uint32_t>(bar, Offsets::OFF_STATUSBAR_BITS) |= Offsets::STATUSBAR_CHANGED; }

using GetAnimValue_t = float(__fastcall *)(void *bar, void *);
GetAnimValue_t g_getAnimValue = nullptr;

float TimerShare(const State &s) {
    return static_cast<float>(s.direction == DIRECTION_REMAINING_TIME ? Duration::Object::RemainingPercent(s.timer)
                                                                      : Duration::Object::ElapsedPercent(s.timer));
}

// The fraction the bar heads for: its timer's share, or its stored value's
// (the engine's GetAnimValue).
float Target(void *bar, const State &s) {
    return s.timer != nullptr ? TimerShare(s) : g_getAnimValue(bar, nullptr);
}

float __fastcall GetAnimValue_h(void *bar, void *) {
    const auto it = g_bars.find(bar);
    return it != g_bars.end() ? it->second.shown : g_getAnimValue(bar, nullptr);
}

const Game::HookAutoRegister _getAnimValueHook{Offsets::FUN_STATUSBAR_GET_ANIM_VALUE,
                                               reinterpret_cast<void *>(&GetAnimValue_h),
                                               reinterpret_cast<void **>(&g_getAnimValue)};

// The fraction drawn now.
float Drawn(void *bar) {
    const auto it = g_bars.find(bar);
    return it != g_bars.end() ? it->second.shown : g_getAnimValue(bar, nullptr);
}

void EraseIfIdle(Bars::iterator it) {
    if (it->second.timer == nullptr && !it->second.easing)
        g_bars.erase(it);
}

// Starts an ease from `from`, the fraction drawn before the change, or ends
// one, the target shown at once.
void SetEase(void *bar, int interpolation, float from) {
    if (interpolation == INTERPOLATION_EXPONENTIAL_EASE_OUT) {
        State &s = g_bars[bar];
        s.easing = true;
        s.shown = from;
        s.target = Target(bar, s);
    } else if (const auto it = g_bars.find(bar); it != g_bars.end()) {
        it->second.easing = false;
        it->second.shown = Target(bar, it->second);
        EraseIfIdle(it);
    }
    MarkChanged(bar);
}

void ClearTimer(void *bar) {
    const auto it = g_bars.find(bar);
    if (it == g_bars.end() || it->second.timer == nullptr)
        return;
    Duration::Object::Release(it->second.timer);
    it->second.timer = nullptr;
    EraseIfIdle(it);
}

// ---- the methods --------------------------------------------------------------------

int TypeId() {
    int &id = At<int>(reinterpret_cast<void *>(Offsets::VAR_STATUSBAR_OBJECT_TYPE), 0);
    if (id == 0)
        id = ++At<int>(reinterpret_cast<void *>(Offsets::VAR_SCRIPTOBJECT_TYPE_COUNTER), 0);
    return id;
}

void *Self(void *L) { return UI::FrameObject::ResolveSelf(L, TypeId()); }

const char *FrameName(void *frame) {
    const char *name = reinterpret_cast<const char *(__fastcall *)(void *, void *)>(
        At<const void *const *>(frame, 0)[Offsets::VTBL_FRAME_GET_NAME / 4])(frame, nullptr);
    return name != nullptr && *name != '\0' ? name : "<unnamed>";
}

// An optional enum argument: `dflt` when nil, else an integer in [0, max].
bool EnumArg(void *L, int idx, int max, int dflt, int &out) {
    out = dflt;
    if (Lua::Type(L, idx) <= Lua::TYPE_NIL)
        return true;
    if (Lua::Type(L, idx) != Lua::TYPE_NUMBER)
        return false;
    const double n = Lua::ToNumber(L, idx);
    if (!(n >= 0 && n <= max) || n != static_cast<int>(n))
        return false;
    out = static_cast<int>(n);
    return true;
}

int InterpolationArg(void *L, int index) {
    return Lua::Type(L, index) == Lua::TYPE_NUMBER && Lua::ToNumber(L, index) == INTERPOLATION_EXPONENTIAL_EASE_OUT
               ? INTERPOLATION_EXPONENTIAL_EASE_OUT
               : INTERPOLATION_IMMEDIATE;
}

// Script_SetValue (0x00971540): the bar's SetValue virtual, which the health
// bar overrides.
int __cdecl Script_SetValue(void *L) {
    void *bar = Self(L);
    if (bar == nullptr)
        return 0;
    if (!Lua::IsNumber(L, 2)) {
        Lua::Error(L, "Usage: %s:SetValue(value)", FrameName(bar));
        return 0;
    }
    const float from = Drawn(bar);
    using SetValue_t = void(__fastcall *)(void *bar, void *, float value);
    reinterpret_cast<SetValue_t>(At<const void *const *>(bar, 0)[Offsets::VTBL_STATUSBAR_SET_VALUE / 4])(
        bar, nullptr, static_cast<float>(Lua::ToNumber(L, 2)));
    SetEase(bar, InterpolationArg(L, 3), from);
    return 0;
}

// Script_SetMinMaxValues (0x009713C0).
int __cdecl Script_SetMinMaxValues(void *L) {
    void *bar = Self(L);
    if (bar == nullptr)
        return 0;
    if (!Lua::IsNumber(L, 2) || !Lua::IsNumber(L, 3)) {
        Lua::Error(L, "Usage: %s:SetMinMaxValues(min, max)", FrameName(bar));
        return 0;
    }
    const float min = static_cast<float>(Lua::ToNumber(L, 2));
    const float max = static_cast<float>(Lua::ToNumber(L, 3));
    if (min < -1.0e12 || min > 1.0e12 || max < -1.0e12 || max > 1.0e12) {
        Lua::Error(L, "Min or Max out of range");
        return 0;
    }
    if (max - min > 1.0e12) {
        Lua::Error(L, "Min and Max too far apart");
        return 0;
    }
    const float from = Drawn(bar);
    reinterpret_cast<void(__fastcall *)(void *, void *, float, float)>(Offsets::FUN_STATUSBAR_SET_MIN_MAX_VALUES)(
        bar, nullptr, min, max);
    ClearTimer(bar);
    SetEase(bar, InterpolationArg(L, 4), from);
    return 0;
}

// The shown value: the shown fraction across the range while easing, else
// the stored value.
int __cdecl Script_GetInterpolatedValue(void *L) {
    void *bar = Self(L);
    if (bar == nullptr)
        return 0;
    const auto it = g_bars.find(bar);
    const bool easing = it != g_bars.end() && it->second.easing;
    const float min = At<float>(bar, Offsets::OFF_STATUSBAR_MIN);
    const float max = At<float>(bar, Offsets::OFF_STATUSBAR_MAX);
    Lua::PushNumber(L, easing ? min + it->second.shown * (max - min) : At<float>(bar, Offsets::OFF_STATUSBAR_VALUE));
    return 1;
}

int __cdecl Script_IsInterpolating(void *L) {
    void *bar = Self(L);
    if (bar == nullptr)
        return 0;
    const auto it = g_bars.find(bar);
    Lua::PushBool(L, it != g_bars.end() && it->second.easing);
    return 1;
}

int __cdecl Script_SetToTargetValue(void *L) {
    if (void *bar = Self(L))
        SetEase(bar, INTERPOLATION_IMMEDIATE, 0.0f);
    return 0;
}

int __cdecl Script_SetTimerDuration(void *L) {
    void *bar = Self(L);
    if (bar == nullptr)
        return 0;
    Duration::Object::Span *span = Duration::Object::ToSpan(L, 2);
    int interpolation, direction;
    if (span == nullptr ||
        !EnumArg(L, 3, INTERPOLATION_EXPONENTIAL_EASE_OUT, INTERPOLATION_IMMEDIATE, interpolation) ||
        !EnumArg(L, 4, DIRECTION_REMAINING_TIME, DIRECTION_ELAPSED_TIME, direction)) {
        Lua::Error(L, "Usage: %s:SetTimerDuration(duration [, interpolation [, direction]])", FrameName(bar));
        return 0;
    }
    const float from = Drawn(bar);
    State &state = g_bars[bar];
    Duration::Object::Retain(span);
    Duration::Object::Release(state.timer);
    state.timer = span;
    state.direction = direction;
    state.shown = TimerShare(state);

    At<float>(bar, Offsets::OFF_STATUSBAR_MIN) = 0.0f;
    At<float>(bar, Offsets::OFF_STATUSBAR_MAX) = 0.0f;
    At<float>(bar, Offsets::OFF_STATUSBAR_VALUE) = 0.0f;
    At<uint32_t>(bar, Offsets::OFF_STATUSBAR_BITS) |= Offsets::STATUSBAR_RANGE_SET | Offsets::STATUSBAR_VALUE_SET;
    SetEase(bar, interpolation, from);
    return 0;
}

int __cdecl Script_GetTimerDuration(void *L) {
    void *bar = Self(L);
    if (bar == nullptr)
        return 0;
    const auto it = g_bars.find(bar);
    if (it != g_bars.end() && it->second.timer != nullptr)
        Duration::Object::Push(L, it->second.timer);
    else
        Lua::PushNil(L);
    return 1;
}

// After the StatusBar's own, so SetValue and SetMinMaxValues replace the engine's.
const Frame::Methods::Entry kMethods[] = {
    {"SetValue", &Script_SetValue},
    {"SetMinMaxValues", &Script_SetMinMaxValues},
    {"GetInterpolatedValue", &Script_GetInterpolatedValue},
    {"IsInterpolating", &Script_IsInterpolating},
    {"SetToTargetValue", &Script_SetToTargetValue},
    {"SetTimerDuration", &Script_SetTimerDuration},
    {"GetTimerDuration", &Script_GetTimerDuration},
};

const StatusBar::Methods::AutoRegister _methods{kMethods,
                                                static_cast<int>(sizeof(kMethods) / sizeof(kMethods[0]))};

void RegisterLuaFunctions() {
    Lua::RegisterIntegerEnum("Enum", "StatusBarInterpolation", kInterpolations,
                             static_cast<int>(sizeof(kInterpolations) / sizeof(kInterpolations[0])));
    Lua::RegisterIntegerEnum("Enum", "StatusBarTimerDirection", kDirections,
                             static_cast<int>(sizeof(kDirections) / sizeof(kDirections[0])));
}

const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace

void Advance(void *bar, float elapsedSec) {
    const auto it = g_bars.find(bar);
    if (it == g_bars.end())
        return;
    State &s = it->second;
    const float target = Target(bar, s);
    const bool held = target == s.target;
    s.target = target;
    float shown = target;
    if (s.easing) {
        // Only a target that held still may snap: a timer's share moves on
        // every update, and retail chases it from a near-zero start until the
        // timer ends.
        const float gap = target - s.shown;
        if (held && std::fabs(gap) < kEaseSnapGap)
            s.easing = false;
        else
            shown = target - gap * static_cast<float>(std::exp(-kEaseRate * elapsedSec));
    }
    if (shown != s.shown) {
        s.shown = shown;
        MarkChanged(bar);
    }
    EraseIfIdle(it);
}

} // namespace StatusBar::Display
