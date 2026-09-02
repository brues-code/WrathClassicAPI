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

// `CombatLogGetCurrentEventInfo()` — returns the fields of the combat-log event
// currently being processed as multiple return values: timestamp, subEvent,
// sourceGUID, sourceName, sourceFlags, destGUID, destName, destFlags, followed by
// the sub-event-specific values (spell id/name/school, amounts, and so on). These
// are the same values COMBAT_LOG_EVENT_UNFILTERED delivers, so combat-log code can
// read them from this accessor instead of from the event's arguments.
//
// The engine builds the payload straight onto the Lua stack for each combat-log
// entry (FUN_CLEU_BUILD_ARGS) and keeps no record to read afterward, so we
// post-hook the builder and copy the values it just pushed into one persistent
// table in the Lua registry; the accessor replays that table. Capture reuses the
// same table every entry (no per-event allocation) and leaves the engine's stack
// exactly as the builder left it, so the values are always current when a
// COMBAT_LOG_EVENT_UNFILTERED handler calls the accessor.

#include "Game.h"
#include "Offsets.h"

namespace CombatLog::CurrentEventInfo {

namespace {

// Registry slot holding the most recently captured payload (a single table,
// reused each capture).
constexpr const char *kRegistryKey = "WrathClassicAPI::CombatLogCurrentEvent";

// Number of values in the last captured payload.
int g_argCount = 0;

// The engine's combat-log argument builder, `int __thiscall(entry, L)`, hooked
// via __fastcall with the customary unused-EDX slot (see Offsets.h).
using BuildArgs_t = int(__fastcall *)(void *entry, void *edx, void *L);
BuildArgs_t BuildArgs_o = nullptr;

// Copy the top `count` values on `L` — the payload the builder just pushed — into
// the persistent registry table, without disturbing the stack the engine still
// needs in order to dispatch them.
void Capture(void *L, int count) {
    if (count <= 0) {
        g_argCount = 0;
        return;
    }
    const int top = Game::Lua::GetTop(L);
    if (top < count) { // defensive: the builder always leaves >= count values
        g_argCount = 0;
        return;
    }

    Game::Lua::GetField(L, Game::Lua::REGISTRY_INDEX, kRegistryKey); // push table | nil
    if (Game::Lua::Type(L, -1) != Game::Lua::TYPE_TABLE) {
        Game::Lua::SetTop(L, top); // drop the nil probe
        Game::Lua::CreateTable(L, count, 0);
        Game::Lua::PushValue(L, -1);
        Game::Lua::SetField(L, Game::Lua::REGISTRY_INDEX, kRegistryKey); // registry[key] = table
    }
    const int tableIdx = Game::Lua::GetTop(L); // the table, now on top
    for (int i = 1; i <= count; ++i) {
        Game::Lua::PushValue(L, top - count + i); // dup payload value i
        Game::Lua::RawSetI(L, tableIdx, i);       // table[i] = value (pops it)
    }
    Game::Lua::SetTop(L, top); // pop the table; restore the payload for the engine
    g_argCount = count;
}

int __fastcall BuildArgs_h(void *entry, void *edx, void *L) {
    const int count = BuildArgs_o(entry, edx, L);
    Capture(L, count);
    return count;
}

int __cdecl Script_CombatLogGetCurrentEventInfo(void *L) {
    const int count = g_argCount;
    if (count <= 0)
        return 0;
    Game::Lua::GetField(L, Game::Lua::REGISTRY_INDEX, kRegistryKey); // push table | nil
    if (Game::Lua::Type(L, -1) != Game::Lua::TYPE_TABLE) {
        Game::Lua::SetTop(L, -2); // pop the non-table
        return 0;
    }
    const int tableIdx = Game::Lua::GetTop(L);
    for (int i = 1; i <= count; ++i)
        Game::Lua::RawGetI(L, tableIdx, i); // push table[i]
    Game::Lua::Remove(L, tableIdx);         // drop the table from beneath the results
    return count;
}

void RegisterLuaFunctions() {
    Game::Lua::RegisterGlobalFunction("CombatLogGetCurrentEventInfo",
                                      &Script_CombatLogGetCurrentEventInfo);
}

const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};
const Game::HookAutoRegister _hook{
    Offsets::FUN_CLEU_BUILD_ARGS, reinterpret_cast<void *>(&BuildArgs_h),
    reinterpret_cast<void **>(&BuildArgs_o)};

} // namespace

} // namespace CombatLog::CurrentEventInfo
