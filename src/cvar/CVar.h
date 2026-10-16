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

#include <cstdint>

// The engine's console variables, as the C_CVar modules use them.
namespace CVar {

using Handle = uint8_t *;

// The cvar Lua can see by this name, or nullptr: the engine's
// LookupRegistered, minus hidden cvars, which every Lua getter treats as
// missing.
Handle Find(const char *name);

uint32_t Flags(Handle cvar);
const char *Value(Handle cvar);   // never null
const char *Default(Handle cvar); // never null
const char **ValueSlot(Handle cvar);

bool IsReadOnly(Handle cvar);
// A protected cvar while protected actions aren't allowed: SetCVar refuses.
bool IsBlocked(Handle cvar);

// Sets the value through the engine (change callback included). `persist`
// marks the config for writing, as SetCVar does.
void Set(Handle cvar, const char *value, bool persist);

} // namespace CVar
