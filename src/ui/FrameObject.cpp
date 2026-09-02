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

#include "ui/FrameObject.h"

#include "Game.h"
#include "Offsets.h"

#include <cstdint>

namespace UI::FrameObject {

namespace {

// `FrameScript_Object::ScriptRegister` — `__thiscall(this, name)`,
// expressed for a raw pointer call as `__fastcall(this /*ecx*/,
// edx_unused, name)`. It uses the global in-game lua_State internally
// (VAR_LUA_STATE), so it takes no `L` argument. `name` is null for
// programmatic registration.
using ScriptRegister_t = void(__fastcall *)(void *this_, void *edx_unused,
                                            const char *name);

} // namespace

// Delegate wrapper construction to the engine's own
// `FrameScript_Object::ScriptRegister`. First call for a never-seen frame
// builds a `{[0] = lightuserdata(this)}` table with the framescript
// metatable, `luaL_ref`s it into the registry, and writes the refkey to
// `frame + OFF_COBJECT_LUA_REGISTRY_REF`. Subsequent calls find a
// populated refkey and just `lua_rawgeti`.
//
// Why delegate instead of building our own wrapper: single source of
// truth. Every code path that pushes this frame — ours, addons'
// `CreateFrame(..., parent)` extracting it, the engine's own pushers —
// funnels through `lua_rawgeti(REGISTRY, frame + 0x08)` and gets the same
// Lua table, so addon fields set on the wrapper aren't orphaned onto a
// divergent copy.
//
// `ScriptRegister` unconditionally bumps the refcount at
// `OFF_COBJECT_LUA_REFCOUNT`, pinning the frame against GC. That's benign
// for the pooled/permanent frames we push here (chat bubbles) — we call
// it at most once per frame pointer (gated by `refKey <= 0`), so the
// refcount tops out at one increment per pool slot.
//
// Defensive fallback: if the registry slot exists but isn't a table any
// more (freed elsewhere), re-register to rebuild a fresh wrapper. This
// shouldn't happen in practice — the engine only unrefs on frame
// destruction.
void Push(void *L, void *frame) {
    auto scriptRegister = reinterpret_cast<ScriptRegister_t>(
        static_cast<uintptr_t>(Offsets::FUN_FRAMESCRIPT_OBJECT_SCRIPT_REGISTER));

    auto *cobj = static_cast<uint8_t *>(frame);
    int refKey =
        *reinterpret_cast<const int *>(cobj + Offsets::OFF_COBJECT_LUA_REGISTRY_REF);
    if (refKey <= 0) { // 0, or the -2 "unregistered" sentinel
        scriptRegister(frame, nullptr, nullptr);
        refKey =
            *reinterpret_cast<const int *>(cobj + Offsets::OFF_COBJECT_LUA_REGISTRY_REF);
    }

    Game::Lua::RawGetI(L, Game::Lua::REGISTRY_INDEX, refKey);
    if (Game::Lua::Type(L, -1) == Game::Lua::TYPE_TABLE)
        return;
    Game::Lua::SetTop(L, -2);

    // Slot got freed somewhere — re-register to rebuild a fresh wrapper
    // and slot. Zero the refcount so `ScriptRegister` re-enters its build
    // branch.
    *reinterpret_cast<int *>(cobj + Offsets::OFF_COBJECT_LUA_REFCOUNT) = 0;
    scriptRegister(frame, nullptr, nullptr);
    refKey =
        *reinterpret_cast<const int *>(cobj + Offsets::OFF_COBJECT_LUA_REGISTRY_REF);
    Game::Lua::RawGetI(L, Game::Lua::REGISTRY_INDEX, refKey);
}

} // namespace UI::FrameObject
