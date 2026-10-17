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

// `C_AddOns` queries over the engine's addon registry:
//   * `IsAddOnLoaded(name|index)` → loadedOrLoading, loaded — the stock
//     IsAddOnLoaded (0x00511B30) with booleans instead of 1/nil.
//   * `GetAddOnOptionalDependencies(name|index)` → ...names — the
//     `## OptionalDeps` list, shaped like the stock GetAddOnDependencies
//     (0x005179B0) returns the required one.
//
// Both take the argument the way the stock functions do: an index goes through
// the AddOns-list array (GetAddOnByIndex, which leaves Blizzard addons out) and
// an out-of-range index raises the stock error; a name is looked up
// case-insensitively.

#include "Game.h"
#include "Offsets.h"

#include <cstdint>

namespace AddOns::Info {

namespace {

using GetNumAddOns_t = uint32_t(__cdecl *)();
using GetAddOnByIndex_t = const char *(__cdecl *)(uint32_t index);
using AddOnFlag_t = bool(__cdecl *)(const char *name);
using HashLookup_t = const uint8_t *(__thiscall *)(void *nameHash, const char *name);

// The engine's growable-array header — the layout of every UIADDON list field.
struct NameArray {
    uint32_t cap;
    uint32_t count;
    const char *const *data;
};

// The addon name the argument at stack 1 names, raising the stock errors for
// a bad index or a non-string.
const char *NameArg(void *L, const char *usage) {
    if (Game::Lua::IsNumber(L, 1)) {
        const auto index = static_cast<uint32_t>(static_cast<int>(Game::Lua::ToNumber(L, 1)) - 1);
        const char *name =
            reinterpret_cast<GetAddOnByIndex_t>(Offsets::FUN_GET_ADDON_BY_INDEX)(index);
        if (name == nullptr)
            Game::Lua::Error(
                L, "AddOn index must be in the range of 1 to %d",
                static_cast<int>(reinterpret_cast<GetNumAddOns_t>(Offsets::FUN_GET_NUM_ADDONS)()));
        return name;
    }
    if (!Game::Lua::IsString(L, 1))
        Game::Lua::Error(L, usage);
    return Game::Lua::ToString(L, 1);
}

int __cdecl Script_IsAddOnLoaded(void *L) {
    const char *name = NameArg(L, "Usage: C_AddOns.IsAddOnLoaded(name or index)");
    Game::Lua::PushBool(L, reinterpret_cast<AddOnFlag_t>(Offsets::FUN_IS_ADDON_LOADING)(name));
    Game::Lua::PushBool(L, reinterpret_cast<AddOnFlag_t>(Offsets::FUN_IS_ADDON_LOADED)(name));
    return 2;
}

int __cdecl Script_GetAddOnOptionalDependencies(void *L) {
    const char *name = NameArg(L, "Usage: C_AddOns.GetAddOnOptionalDependencies(name or index)");
    const uint8_t *entry = reinterpret_cast<HashLookup_t>(Offsets::FUN_ADDON_HASH_LOOKUP)(
        reinterpret_cast<void *>(static_cast<uintptr_t>(Offsets::VAR_ADDON_NAME_HASH)), name);
    if (entry == nullptr)
        return 0;
    const auto *deps = reinterpret_cast<const NameArray *>(entry + Offsets::OFF_ADDON_OPTIONAL_DEPS);
    const int count = static_cast<int>(deps->count);
    if (Game::Lua::CheckStack(L, count) == 0)
        return 0;
    for (int i = 0; i < count; ++i)
        Game::Lua::PushString(L, deps->data[i]);
    return count;
}

void RegisterLuaFunctions() {
    Game::Lua::RegisterTableFunction("C_AddOns", "IsAddOnLoaded", &Script_IsAddOnLoaded);
    Game::Lua::RegisterTableFunction("C_AddOns", "GetAddOnOptionalDependencies",
                                     &Script_GetAddOnOptionalDependencies);
}

const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace

} // namespace AddOns::Info
