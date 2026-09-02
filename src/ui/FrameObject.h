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

namespace UI::FrameObject {

// Pushes a C++ `CFrameScriptObject`-derived frame onto the Lua stack as
// its canonical wrapper table — the same object every other engine path
// resolves via `lua_rawgeti(REGISTRY, frame + 0x08)`, so addon-set fields
// on the wrapper survive roundtrips through our API and method dispatch
// (`:GetRegions()`, `:GetName()`, ...) works normally.
//
// For frames the engine created purely in C++ without ever going through
// Lua `CreateFrame` (default nameplates, chat bubbles), the registry ref
// at `frame + 0x08` starts at the -2 "unregistered" sentinel; the first
// push delegates to the engine's own `FrameScript_Object::ScriptRegister`
// to lazily build the wrapper + registry slot. Subsequent pushes just
// `lua_rawgeti` the cached slot.
//
// Leaves exactly one value (the wrapper table, or the re-registered
// fallback) on the stack. `frame` must be non-null. `L` must be the
// in-game Lua state (the same global state ScriptRegister builds into).
void Push(void *L, void *frame);

} // namespace UI::FrameObject
