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

// `bar:SetValue(value [, interpolation])`, `SetMinMaxValues(min, max [,
// interpolation])`, `GetInterpolatedValue()`, `IsInterpolating()`,
// `SetToTargetValue()` and `Enum.StatusBarInterpolation`
// (SimpleStatusBarAPIDocumentation.lua; the port of ClassicAPI's
// src/statusbar/Display.cpp, whose retail probes set the numbers below):
//
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
//
// CSimpleStatusBar::OnLayerUpdate draws the fill from the GetAnimValue
// virtual, not the stored value; its hook answers the shown fraction for an
// easing bar, so the engine's layout and statusbar/Fill's both draw it.
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

constexpr double kEaseRate = 18.0;     // per second, of the gap
constexpr float kEaseSnapGap = 0.001f; // of the fill fraction

// An easing bar. A bar not easing has no entry.
struct Ease {
    float shown = 0.0f;  // the fraction the fill draws
    float target = 0.0f; // the fraction headed for, as of the last update
};

std::unordered_map<const void *, Ease> g_eases;

void Forget(const void *bar) { g_eases.erase(bar); }

const StatusBar::Lifetime::OnDestroyed _destroyed{&Forget};

void MarkChanged(void *bar) { At<uint32_t>(bar, Offsets::OFF_STATUSBAR_BITS) |= Offsets::STATUSBAR_CHANGED; }

using GetAnimValue_t = float(__fastcall *)(void *bar, void *);
GetAnimValue_t g_getAnimValue = nullptr;

// The stored value's fraction: the engine's GetAnimValue.
float Target(void *bar) { return g_getAnimValue(bar, nullptr); }

float __fastcall GetAnimValue_h(void *bar, void *) {
    const auto it = g_eases.find(bar);
    return it != g_eases.end() ? it->second.shown : g_getAnimValue(bar, nullptr);
}

const Game::HookAutoRegister _getAnimValueHook{Offsets::FUN_STATUSBAR_GET_ANIM_VALUE,
                                               reinterpret_cast<void *>(&GetAnimValue_h),
                                               reinterpret_cast<void **>(&g_getAnimValue)};

// The fraction drawn now.
float Drawn(void *bar) {
    const auto it = g_eases.find(bar);
    return it != g_eases.end() ? it->second.shown : Target(bar);
}

// Starts an ease from `from`, the fraction drawn before the change, or ends
// one, the target shown at once.
void SetEase(void *bar, int interpolation, float from) {
    if (interpolation == INTERPOLATION_EXPONENTIAL_EASE_OUT)
        g_eases[bar] = Ease{from, Target(bar)};
    else
        g_eases.erase(bar);
    MarkChanged(bar);
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
    SetEase(bar, InterpolationArg(L, 4), from);
    return 0;
}

// The shown value: the shown fraction across the range while easing, else
// the stored value.
int __cdecl Script_GetInterpolatedValue(void *L) {
    void *bar = Self(L);
    if (bar == nullptr)
        return 0;
    const auto it = g_eases.find(bar);
    const float min = At<float>(bar, Offsets::OFF_STATUSBAR_MIN);
    const float max = At<float>(bar, Offsets::OFF_STATUSBAR_MAX);
    Lua::PushNumber(L, it != g_eases.end() ? min + it->second.shown * (max - min)
                                           : At<float>(bar, Offsets::OFF_STATUSBAR_VALUE));
    return 1;
}

int __cdecl Script_IsInterpolating(void *L) {
    void *bar = Self(L);
    if (bar == nullptr)
        return 0;
    Lua::PushBool(L, g_eases.find(bar) != g_eases.end());
    return 1;
}

int __cdecl Script_SetToTargetValue(void *L) {
    if (void *bar = Self(L))
        SetEase(bar, INTERPOLATION_IMMEDIATE, 0.0f);
    return 0;
}

// After the StatusBar's own, so SetValue and SetMinMaxValues replace the engine's.
const Frame::Methods::Entry kMethods[] = {
    {"SetValue", &Script_SetValue},
    {"SetMinMaxValues", &Script_SetMinMaxValues},
    {"GetInterpolatedValue", &Script_GetInterpolatedValue},
    {"IsInterpolating", &Script_IsInterpolating},
    {"SetToTargetValue", &Script_SetToTargetValue},
};

const StatusBar::Methods::AutoRegister _methods{kMethods,
                                                static_cast<int>(sizeof(kMethods) / sizeof(kMethods[0]))};

void RegisterLuaFunctions() {
    Lua::RegisterIntegerEnum("Enum", "StatusBarInterpolation", kInterpolations,
                             static_cast<int>(sizeof(kInterpolations) / sizeof(kInterpolations[0])));
}

const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace

void Advance(void *bar, float elapsedSec) {
    const auto it = g_eases.find(bar);
    if (it == g_eases.end())
        return;
    Ease &e = it->second;
    const float target = Target(bar);
    const bool held = target == e.target;
    e.target = target;
    const float gap = target - e.shown;
    if (held && std::fabs(gap) < kEaseSnapGap) {
        g_eases.erase(it);
        MarkChanged(bar);
        return;
    }
    const float shown = target - gap * static_cast<float>(std::exp(-kEaseRate * elapsedSec));
    if (shown != e.shown) {
        e.shown = shown;
        MarkChanged(bar);
    }
}

} // namespace StatusBar::Display
