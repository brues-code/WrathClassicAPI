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

// `frame:RegisterUnitEvent(event, ...units)` — register for an event but only
// receive it for the given unit tokens — plus the unit-aware
// `frame:IsEventRegistered(event)` (registered [, unit…]), which replaces the
// engine's (that one returns 1/nil and knows no units).
//
// The engine owns everything except the filter, so the filter is all this adds:
//
//   - Listener membership stays the engine's. RegisterUnitEvent registers
//     through FrameScript_Object::RegisterScriptEvent, the same call
//     Frame:RegisterEvent makes, so mid-dispatch registration is queued the
//     engine's way.
//   - The filter is stored here, keyed by (frame, event object), and dropped
//     where the engine drops the frame from the event: a co-hook on
//     FrameScript_Object::UnregisterScriptEvent, which both UnregisterEvent and
//     UnregisterAllEvents funnel through.
//   - The check is one gate on the per-listener OnEvent run. The dispatcher
//     calls FrameScript_Execute(ref, frame, argCount, taint, event) once per
//     listener with (event, arg1, …) on top of the Lua stack. When the frame
//     has a filter for that event and arg1 matches none of its units, we pop
//     the args (Execute's own contract) and skip the handler.
//
// Semantics mirror Blizzard's 5.4.8 implementation, where the filter lives on
// the listener node and the node's kind is fixed when it is created:
// RegisterEvent on a filtered registration leaves the filter; RegisterUnitEvent
// on a plain registration stays plain; RegisterUnitEvent on a filtered
// registration replaces the units; RegisterUnitEvent with no usable unit is a
// plain RegisterEvent. Switching kinds takes UnregisterEvent first.
//
// Every string argument past the event name is taken as a unit (5.4.8 reads
// two, retail documents up to four), compared case-insensitively.
//
// Filter rule: the filter applies only when the event's first argument is a
// string (every UNIT_* event fires with the unit token as arg1). Any other
// event fires as with RegisterEvent — its failure mode is an extra event
// rather than a lost one.

#include "Game.h"
#include "Offsets.h"
#include "frame/Methods.h"

#include <cstdint>
#include <cstring>
#include <iterator>
#include <string>
#include <unordered_map>
#include <vector>

namespace Frame::UnitEvent {

namespace {

template <typename T> T At(const void *base, uintptr_t offset) {
    return *reinterpret_cast<const T *>(static_cast<const uint8_t *>(base) + offset);
}

// __thiscall entry points mirrored as __fastcall with a dummy EDX.
using FindEvent_t = void *(__fastcall *)(void *table, void *edx, const char *name);
using RegisterScriptEvent_t = int(__fastcall *)(void *object, void *edx, const char *name);
using UnregisterScriptEvent_t = void(__fastcall *)(void *object, void *edx, const char *name);
using IsA_t = bool(__fastcall *)(void *object, void *edx, int type);
using Execute_t = void(__cdecl *)(int luaRef, void *object, int argCount, const char *taint,
                                  void *event);

UnregisterScriptEvent_t g_unregisterOriginal = nullptr;
Execute_t g_executeOriginal = nullptr;

// One filtered registration. `units` is never empty — a filter with no unit is
// no filter.
struct Filter {
    const void *event;
    std::vector<std::string> units;
};

// Keyed by frame. Entries die with the frame's registration (the unregister
// co-hook) and on /reload (PrepareForReload).
std::unordered_map<const void *, std::vector<Filter>> g_filters;

// ---- engine state ------------------------------------------------------------

// The event object for `name` (case-insensitive), or null for an unknown event.
const void *FindEvent(const char *name) {
    auto *table = reinterpret_cast<void *>(static_cast<uintptr_t>(Offsets::VAR_EVENT_TABLE));
    return reinterpret_cast<FindEvent_t>(Offsets::FUN_EVENT_TABLE_FIND)(table, nullptr, name);
}

bool ListHas(const void *event, uintptr_t headOffset, const void *frame) {
    uintptr_t node = At<uintptr_t>(event, headOffset);
    while (node != 0 && (node & 1) == 0) {
        if (At<const void *>(reinterpret_cast<const void *>(node), Offsets::OFF_EVENT_NODE_OBJECT) == frame)
            return true;
        node = At<uintptr_t>(reinterpret_cast<const void *>(node), Offsets::OFF_EVENT_NODE_NEXT);
    }
    return false;
}

// Is `frame` registered for `event` once any in-flight dispatch settles: a
// listener not queued for removal, or queued to be added.
bool IsRegistered(const void *event, const void *frame) {
    return (ListHas(event, Offsets::OFF_EVENT_LISTENERS_HEAD, frame) &&
            !ListHas(event, Offsets::OFF_EVENT_UNREGISTERS_HEAD, frame)) ||
           ListHas(event, Offsets::OFF_EVENT_REGISTERS_HEAD, frame);
}

// The frame behind `self` (stack index 1), raising the engine's own errors for
// anything else — FrameScript_GetObjectThis(L, CSimpleFrame type).
void *FrameAt(void *L) {
    if (Game::Lua::Type(L, 1) != Game::Lua::TYPE_TABLE) {
        Game::Lua::Error(L, "Attempt to find 'this' in non-table object (used '.' instead of ':' ?)");
        return nullptr;
    }
    Game::Lua::RawGetI(L, 1, 0);
    void *object = Game::Lua::ToUserdata(L, -1);
    Game::Lua::SetTop(L, -2);
    if (object == nullptr) {
        Game::Lua::Error(L, "Attempt to find 'this' in non-framescript object");
        return nullptr;
    }
    auto &type = *reinterpret_cast<int *>(static_cast<uintptr_t>(Offsets::VAR_SIMPLEFRAME_OBJECT_TYPE));
    if (type == 0)
        type = ++*reinterpret_cast<int *>(static_cast<uintptr_t>(Offsets::VAR_SCRIPTOBJECT_TYPE_COUNTER));
    auto isA = At<IsA_t>(At<const void *>(object, 0), Offsets::VTBL_OFF_SCRIPTOBJECT_IS_A);
    if (!isA(object, nullptr, type)) {
        Game::Lua::Error(L, "Wrong object type for member function");
        return nullptr;
    }
    return object;
}

// ---- filter map --------------------------------------------------------------

Filter *FindFilter(const void *frame, const void *event) {
    auto it = g_filters.find(frame);
    if (it == g_filters.end())
        return nullptr;
    for (Filter &f : it->second)
        if (f.event == event)
            return &f;
    return nullptr;
}

void EraseFilter(const void *frame, const void *event) {
    auto it = g_filters.find(frame);
    if (it == g_filters.end())
        return;
    std::vector<Filter> &v = it->second;
    for (auto f = v.begin(); f != v.end(); ++f) {
        if (f->event == event) {
            v.erase(f);
            break;
        }
    }
    if (v.empty())
        g_filters.erase(it);
}

// ---- Lua methods --------------------------------------------------------------

// `frame:RegisterUnitEvent(event, ...units)` → registered.
int __cdecl Script_RegisterUnitEvent(void *L) {
    void *frame = FrameAt(L);
    if (!Game::Lua::IsString(L, 2)) {
        Game::Lua::Error(L, "Usage: frame:RegisterUnitEvent(\"event\" [, \"unit1\", ...])");
        return 0;
    }
    const char *eventName = Game::Lua::ToString(L, 2);
    const void *event = FindEvent(eventName);
    const bool wasRegistered = event != nullptr && IsRegistered(event, frame);
    const bool registered =
        reinterpret_cast<RegisterScriptEvent_t>(Offsets::FUN_SCRIPTOBJECT_REGISTER_EVENT)(
            frame, nullptr, eventName) != 0;

    // Like 5.4.8, only real strings count as units (a number is not coerced).
    std::vector<std::string> units;
    const int top = Game::Lua::GetTop(L);
    for (int idx = 3; idx <= top; ++idx) {
        if (Game::Lua::Type(L, idx) != Game::Lua::TYPE_STRING)
            continue;
        const char *s = Game::Lua::ToString(L, idx);
        if (*s != '\0')
            units.emplace_back(s);
    }

    // 5.4.8 node semantics (see the header). No usable unit → this was a plain
    // RegisterEvent and an existing filter is left alone.
    if (registered && !units.empty()) {
        if (Filter *f = FindFilter(frame, event))
            f->units = std::move(units);
        else if (!wasRegistered)
            g_filters[frame].push_back(Filter{event, std::move(units)});
    }

    Game::Lua::PushBool(L, registered);
    return 1;
}

// `frame:IsEventRegistered(event)` → registered [, unit…], units in
// registration order.
int __cdecl Script_IsEventRegistered(void *L) {
    void *frame = FrameAt(L);
    if (!Game::Lua::IsString(L, 2)) {
        Game::Lua::Error(L, "Usage: frame:IsEventRegistered(\"event\")");
        return 0;
    }
    const void *event = FindEvent(Game::Lua::ToString(L, 2));
    const bool registered = event != nullptr && IsRegistered(event, frame);
    Game::Lua::PushBool(L, registered);
    const Filter *f = registered ? FindFilter(frame, event) : nullptr;
    if (f == nullptr || Game::Lua::CheckStack(L, static_cast<int>(f->units.size())) == 0)
        return 1;
    for (const std::string &u : f->units)
        Game::Lua::PushString(L, u.c_str());
    return 1 + static_cast<int>(f->units.size());
}

// ---- the gate ------------------------------------------------------------------

// Whether `frame`'s filter for `event` rejects this dispatch. The top `argCount`
// stack values are (event name, arg1, …).
bool Rejects(const void *frame, const void *event, int argCount) {
    const Filter *f = FindFilter(frame, event);
    if (f == nullptr || argCount < 2)
        return false;
    void *L = Game::Lua::State();
    const int arg1 = -argCount + 1;
    if (Game::Lua::Type(L, arg1) != Game::Lua::TYPE_STRING)
        return false;
    const char *unit = Game::Lua::ToString(L, arg1);
    for (const std::string &u : f->units)
        if (_stricmp(unit, u.c_str()) == 0)
            return false;
    return true;
}

void __cdecl Execute_h(int luaRef, void *object, int argCount, const char *taint, void *event) {
    if (event != nullptr && !g_filters.empty() && Rejects(object, event, argCount)) {
        Game::Lua::SetTop(Game::Lua::State(), -argCount - 1);
        return;
    }
    g_executeOriginal(luaRef, object, argCount, taint, event);
}

// ---- unregister co-hook (the filter dies with the registration) -----------------

void __fastcall UnregisterScriptEvent_h(void *object, void *edx, const char *name) {
    if (!g_filters.empty()) {
        if (const void *event = FindEvent(name))
            EraseFilter(object, event);
    }
    g_unregisterOriginal(object, edx, name);
}

// ---- registration -----------------------------------------------------------------

const Frame::Methods::Entry kFrameMethods[] = {
    {"RegisterUnitEvent", &Script_RegisterUnitEvent},
    {"IsEventRegistered", &Script_IsEventRegistered},
};

// /reload destroys the event objects and every frame, which then re-register
// from scratch, so the filters start over too — and a recycled frame or event
// pointer can never inherit a stale one.
void PrepareForReload() { g_filters.clear(); }

const Frame::Methods::AutoRegister _methods{kFrameMethods,
                                            static_cast<int>(std::size(kFrameMethods))};
const Game::ReloadAutoRegister _reloadReg{&PrepareForReload};

const Game::HookAutoRegister _executeHook{
    Offsets::FUN_FRAMESCRIPT_EXECUTE, reinterpret_cast<void *>(&Execute_h),
    reinterpret_cast<void **>(&g_executeOriginal)};
const Game::HookAutoRegister _unregisterHook{
    Offsets::FUN_SCRIPTOBJECT_UNREGISTER_EVENT, reinterpret_cast<void *>(&UnregisterScriptEvent_h),
    reinterpret_cast<void **>(&g_unregisterOriginal)};

} // namespace

} // namespace Frame::UnitEvent
