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

// `C_Timer.After` / `C_Timer.NewTimer` / `C_Timer.NewTicker` — modern
// callback-scheduling API. 3.3.5 ships neither the namespace nor any
// underlying engine primitive for "fire a Lua callback after N seconds",
// so we drive the dispatch ourselves: hook `FrameScript_FireOnUpdate`
// (called once per render frame), and walk a min-heap of pending
// entries on each tick.
//
// Per Blizzard's design note on the original C_Timer implementation:
//   > the new C_Timer system uses a standard heap implementation.
//   > It's only doing work when the timer is created or destroyed
//   > (and even then, that work is fairly minimal).
// So we use `std::priority_queue` keyed by `fireAt`. The frame-cost
// when nothing is due is one comparison against the heap top.
//
// **Cancel semantics** — lazy deletion. The returned timer table
// stores its `_ref` (callback registry key) and a `_cancelled`
// boolean. `:Cancel()` clears `refs[_ref] = nil` and flips the flag;
// the heap entry stays in place. On pop, if the callback ref no
// longer resolves to a function, we skip and drop.
//
// **Tick source** — `GetTickCount()` (ms since boot) divided by
// 1000.0. Monotonic, wraps every ~49 days, fine for timer math
// (we always compare `now >= fireAt`, both in the same clock).
// User-visible `GetTime()` advances at the same rate, so a 1.0s
// timer fires ~1.0s after the call in either clock.

#include "Common.h"
#include "Game.h"
#include "Offsets.h"

#include <cstdint>
#include <queue>
#include <vector>

#include <windows.h>

namespace Timer {

namespace {

struct Entry {
    double fireAt;          // GetTickCount() seconds
    double period;          // 0 for one-shot, > 0 for ticker
    int    iterationsLeft;  // -1 = infinite, > 0 = remaining, 0 = done
    int    refID;           // key into the refs table
};

struct EntryGreater {
    bool operator()(const Entry &a, const Entry &b) const {
        return a.fireAt > b.fireAt;
    }
};

std::priority_queue<Entry, std::vector<Entry>, EntryGreater> g_heap;
int g_nextRef = 1;

// Private key into the Lua registry. The refs table maps integer
// `refID` → user callback function. Registry-anchored so we don't
// have to expose anything to user-visible globals.
constexpr const char *kRefsRegistryKey = "WrathClassicAPI.C_Timer.Refs";

double NowSec() {
    return static_cast<double>(GetTickCount()) * 0.001;
}

// Pushes the refs table onto the Lua stack, lazily creating it on
// first use. Leaves it at the top.
void PushRefsTable(void *L) {
    Game::Lua::GetField(L, Game::Lua::REGISTRY_INDEX, kRefsRegistryKey);
    if (Game::Lua::Type(L, -1) != Game::Lua::TYPE_TABLE) {
        Game::Lua::SetTop(L, -2);                                // pop the nil
        Game::Lua::NewTable(L);                                  // create
        Game::Lua::PushValue(L, -1);                             // dup for SetField pop
        Game::Lua::SetField(L, Game::Lua::REGISTRY_INDEX,
                            kRefsRegistryKey);                   // registry[key] = table
        // Original copy stays on top.
    }
}

// Stashes the value at `callbackStackIdx` in the refs table under a
// fresh integer key. Returns the key. Does not modify the rest of
// the stack.
int RefCallback(void *L, int callbackStackIdx) {
    const int ref = g_nextRef++;
    PushRefsTable(L);                                  // [..., refs]
    Game::Lua::PushValue(L, callbackStackIdx);         // [..., refs, callback]
    Game::Lua::RawSetI(L, -2, ref);                    // refs[ref] = callback; pops
    Game::Lua::SetTop(L, -2);                          // pop refs
    return ref;
}

void ClearRef(void *L, int refID) {
    PushRefsTable(L);
    Game::Lua::PushNil(L);
    Game::Lua::RawSetI(L, -2, refID);
    Game::Lua::SetTop(L, -2);
}

// Walks heap, firing every entry whose fireAt has passed. Re-pushes
// ticker entries with bumped fireAt. Drops the callback ref for
// one-shots and final ticker iterations.
void Tick() {
    void *L = Game::Lua::State();
    if (L == nullptr || g_heap.empty())
        return;
    const double now = NowSec();
    if (g_heap.top().fireAt > now)
        return; // fast path

    PushRefsTable(L);
    const int refsIdx = Game::Lua::GetTop(L);

    while (!g_heap.empty() && g_heap.top().fireAt <= now) {
        Entry e = g_heap.top();
        g_heap.pop();

        Game::Lua::PushNumber(L, static_cast<double>(e.refID));
        Game::Lua::RawGet(L, refsIdx);
        if (Game::Lua::Type(L, -1) != Game::Lua::TYPE_FUNCTION) {
            Game::Lua::SetTop(L, -2); // pop the not-function
            continue;                 // cancelled / already-fired
        }

        // pcall pops the function + nargs and pushes nresults or
        // a single error message on failure. We want neither.
        if (Game::Lua::PCall(L, /*nargs=*/0, /*nresults=*/0,
                             /*errfunc=*/0) != 0) {
            Game::Lua::SetTop(L, -2); // discard error message
        }

        // Re-schedule tickers.
        if (e.period > 0.0) {
            if (e.iterationsLeft > 0)
                --e.iterationsLeft;
            if (e.iterationsLeft != 0) {
                e.fireAt = now + e.period;
                g_heap.push(e);
                continue;
            }
        }

        // One-shot or final ticker iteration. Drop the ref so the
        // callback is GC-eligible.
        Game::Lua::PushNil(L);
        Game::Lua::RawSetI(L, refsIdx, e.refID);
    }

    Game::Lua::SetTop(L, refsIdx - 1); // pop refs table
}

using FireOnUpdate_t = void(__cdecl *)(int a1, int a2, int a3, int a4);
FireOnUpdate_t FireOnUpdate_o = nullptr;

void __cdecl FireOnUpdate_h(int a1, int a2, int a3, int a4) {
    // Run the engine's per-frame dispatch first so user OnUpdate
    // handlers see "the world has progressed" before our timer
    // callbacks fire — matches the expectations of modern WoW where
    // C_Timer is logically a downstream consumer of the same tick.
    FireOnUpdate_o(a1, a2, a3, a4);
    Tick();
}

const Game::HookAutoRegister _hookreg{
    Offsets::FUN_FRAMESCRIPT_FIRE_ON_UPDATE,
    reinterpret_cast<void *>(&FireOnUpdate_h),
    reinterpret_cast<void **>(&FireOnUpdate_o)};

// `timer:Cancel()` — pops self off the stack, marks `_cancelled =
// true`, clears the callback ref so the heap entry becomes a no-op
// when it eventually reaches the top.
int __cdecl Script_Timer_Cancel(void *L) {
    if (Game::Lua::Type(L, 1) != Game::Lua::TYPE_TABLE)
        return 0;
    Game::Lua::GetField(L, 1, "_ref");
    if (Game::Lua::Type(L, -1) == Game::Lua::TYPE_NUMBER) {
        const int ref = static_cast<int>(Game::Lua::ToNumber(L, -1));
        ClearRef(L, ref);
    }
    Game::Lua::SetTop(L, -2);
    Game::Lua::PushBool(L, true);
    Game::Lua::SetField(L, 1, "_cancelled");
    return 0;
}

int __cdecl Script_Timer_IsCancelled(void *L) {
    if (Game::Lua::Type(L, 1) != Game::Lua::TYPE_TABLE) {
        Game::Lua::PushBool(L, false);
        return 1;
    }
    Game::Lua::GetField(L, 1, "_cancelled");
    return 1;
}

// Builds the returned `{Cancel, IsCancelled, _cancelled, _ref}`
// table for NewTimer / NewTicker. Net stack effect: +1 (the table).
void BuildTimerTable(void *L, int refID) {
    Game::Lua::NewTable(L);
    Game::Lua::SetFieldBool(L, "_cancelled", false);
    Game::Lua::SetFieldNumber(L, "_ref", static_cast<double>(refID));
    Game::Lua::PushCClosure(L, &Script_Timer_Cancel, 0);
    Game::Lua::SetField(L, -2, "Cancel");
    Game::Lua::PushCClosure(L, &Script_Timer_IsCancelled, 0);
    Game::Lua::SetField(L, -2, "IsCancelled");
}

// Args: (seconds, callback). Returns nothing.
int __cdecl Script_C_Timer_After(void *L) {
    if (!Game::Lua::IsNumber(L, 1) ||
        Game::Lua::Type(L, 2) != Game::Lua::TYPE_FUNCTION)
        return Game::Lua::Error(L, "Usage: C_Timer.After(seconds, callback)");

    const double sec = Game::Lua::ToNumber(L, 1);
    const int refID = RefCallback(L, 2);

    Entry e{};
    e.fireAt = NowSec() + sec;
    e.period = 0.0;
    e.iterationsLeft = 1;
    e.refID = refID;
    g_heap.push(e);
    return 0;
}

// Args: (seconds, callback). Returns a timer object with
// `Cancel()` / `IsCancelled()` methods.
int __cdecl Script_C_Timer_NewTimer(void *L) {
    if (!Game::Lua::IsNumber(L, 1) ||
        Game::Lua::Type(L, 2) != Game::Lua::TYPE_FUNCTION)
        return Game::Lua::Error(L,
            "Usage: C_Timer.NewTimer(seconds, callback)");

    const double sec = Game::Lua::ToNumber(L, 1);
    const int refID = RefCallback(L, 2);

    Entry e{};
    e.fireAt = NowSec() + sec;
    e.period = 0.0;
    e.iterationsLeft = 1;
    e.refID = refID;
    g_heap.push(e);

    BuildTimerTable(L, refID);
    return 1;
}

// Args: (seconds, callback[, iterations]). Returns a ticker object
// with `Cancel()` / `IsCancelled()` methods. Omitted/non-positive
// `iterations` means infinite — matches modern semantics.
int __cdecl Script_C_Timer_NewTicker(void *L) {
    if (!Game::Lua::IsNumber(L, 1) ||
        Game::Lua::Type(L, 2) != Game::Lua::TYPE_FUNCTION)
        return Game::Lua::Error(L,
            "Usage: C_Timer.NewTicker(seconds, callback [, iterations])");

    const double sec = Game::Lua::ToNumber(L, 1);
    const int iterations = Game::Lua::IsNumber(L, 3)
        ? static_cast<int>(Game::Lua::ToNumber(L, 3))
        : -1;
    const int refID = RefCallback(L, 2);

    Entry e{};
    e.fireAt = NowSec() + sec;
    e.period = sec;
    e.iterationsLeft = (iterations > 0) ? iterations : -1;
    e.refID = refID;
    g_heap.push(e);

    BuildTimerTable(L, refID);
    return 1;
}

void RegisterLuaFunctions() {
    Game::Lua::RegisterTableFunction("C_Timer", "After",
                                     &Script_C_Timer_After);
    Game::Lua::RegisterTableFunction("C_Timer", "NewTimer",
                                     &Script_C_Timer_NewTimer);
    Game::Lua::RegisterTableFunction("C_Timer", "NewTicker",
                                     &Script_C_Timer_NewTicker);
}

const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace
} // namespace Timer
