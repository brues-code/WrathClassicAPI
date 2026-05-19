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

#include "Game.h"
#include "Offsets.h"

#include <cstdint>

namespace Event::Util {

namespace {

// Engine-side event-table lookup. The 3.3.5 event registry is a hash
// table (not 1.12's flat array), so rather than re-implement the
// bucket walk in C we call the engine's own resolver. Returns the
// entry struct pointer or NULL if `name` isn't a known event.
//
// __thiscall(ECX = table, name) — mirrored as __fastcall with a
// dummy EDX so MSVC can emit it from free-function code.
using FindEvent_t = void *(__fastcall *)(void *table, void *edx, const char *name);

bool IsKnownEventName(const char *name) {
    auto *table = reinterpret_cast<void *>(static_cast<uintptr_t>(Offsets::VAR_EVENT_TABLE));
    auto fn = reinterpret_cast<FindEvent_t>(Offsets::FUN_EVENT_TABLE_FIND);
    return fn(table, nullptr, name) != nullptr;
}

int __cdecl Script_IsEventValid(void *L) {
    if (Game::Lua::Type(L, 1) != Game::Lua::TYPE_STRING)
        return Game::Lua::Error(L, "Usage: C_EventUtils.IsEventValid(eventName)");
    const char *name = Game::Lua::ToString(L, 1);
    if (name == nullptr || name[0] == 0) {
        Game::Lua::PushBool(L, false);
        return 1;
    }
    Game::Lua::PushBool(L, IsKnownEventName(name));
    return 1;
}

void RegisterLuaFunctions() {
    Game::Lua::RegisterTableFunction("C_EventUtils", "IsEventValid", &Script_IsEventValid);
}

const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace
} // namespace Event::Util
