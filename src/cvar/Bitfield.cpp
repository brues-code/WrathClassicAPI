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

// `C_CVar.GetCVarBitfield(name, index)` / `C_CVar.SetCVarBitfield(name,
// index, value)` — one bit of a cvar, so one cvar carries many booleans.
// Ported from ClassicAPI's src/cvar/Bitfield.cpp.
//
// The bits live in the client's own bit-array format (CVarArrayWrapper; see
// Offsets.h), the one its Dungeon Finder cvars use, through the client's own
// reader and writer. So `lfdSelectedDungeons` reads by dungeon ID, and a
// write fires the change callback and marks the config for saving, exactly as
// SetCVar does.
//
// The index is 1-based. The writer refuses a bit whose character wouldn't
// fit in a cvar's 255 (bit i sits in character i / 6 + 1, which needs a
// terminator after it), which caps it at kMaxIndex.

#include "Game.h"
#include "Offsets.h"
#include "cvar/CVar.h"

#include <cstdint>

namespace CVar::Bitfield {

namespace {

constexpr int kMaxIndex = 6 * 254 - 1;

struct ArrayWrapper {
    CVar::Handle cvar;
    uint8_t defaultValue;
};

using ArrayGet_t = bool(__thiscall *)(const ArrayWrapper *self, uint32_t index);
using ArraySet_t = bool(__thiscall *)(ArrayWrapper *self, uint32_t index, bool value);
using ArrayUpgrade_t = void(__thiscall *)(ArrayWrapper *self, int version);

// The wrapper's inlined constructor: upgrade an older encoding in place.
ArrayWrapper Wrap(CVar::Handle cvar) {
    ArrayWrapper wrapper{cvar, 0};
    const auto version = static_cast<uint8_t>(CVar::Value(cvar)[0]);
    if (version != Offsets::CVAR_ARRAY_VERSION)
        reinterpret_cast<ArrayUpgrade_t>(Offsets::FUN_CVAR_ARRAY_UPGRADE)(&wrapper, version);
    return wrapper;
}

// The cvar and a valid index from args 1 and 2, or nullptr.
CVar::Handle Resolve(void *L, const char *usage, int *index) {
    if (!Game::Lua::IsString(L, 1) || Game::Lua::Type(L, 2) != Game::Lua::TYPE_NUMBER) {
        Game::Lua::Error(L, usage);
        return nullptr;
    }
    *index = static_cast<int>(Game::Lua::ToNumber(L, 2));
    if (*index < 1 || *index > kMaxIndex)
        return nullptr;
    return CVar::Find(Game::Lua::ToString(L, 1));
}

// C_CVar.GetCVarBitfield(name, index) — the bit, or nil for a name that
// isn't a cvar or an index out of range.
int __cdecl Script_GetCVarBitfield(void *L) {
    int index = 0;
    CVar::Handle cvar = Resolve(L, "Usage: C_CVar.GetCVarBitfield(\"cvar\", index)", &index);
    if (cvar == nullptr) {
        Game::Lua::PushNil(L);
        return 1;
    }
    const ArrayWrapper wrapper = Wrap(cvar);
    const bool bit = reinterpret_cast<ArrayGet_t>(Offsets::FUN_CVAR_ARRAY_GET)(
        &wrapper, static_cast<uint32_t>(index));
    Game::Lua::PushBool(L, bit);
    return 1;
}

// C_CVar.SetCVarBitfield(name, index, value) — whether the bit was written.
// False for an unknown cvar, an index out of range, and a cvar SetCVar
// would refuse.
int __cdecl Script_SetCVarBitfield(void *L) {
    int index = 0;
    CVar::Handle cvar =
        Resolve(L, "Usage: C_CVar.SetCVarBitfield(\"cvar\", index, value)", &index);
    if (cvar == nullptr || CVar::IsReadOnly(cvar) || CVar::IsBlocked(cvar)) {
        Game::Lua::PushBool(L, false);
        return 1;
    }
    const bool value = Game::Lua::ToBoolean(L, 3) != 0;
    ArrayWrapper wrapper = Wrap(cvar);
    const bool ok = reinterpret_cast<ArraySet_t>(Offsets::FUN_CVAR_ARRAY_SET)(
        &wrapper, static_cast<uint32_t>(index), value);
    Game::Lua::PushBool(L, ok);
    return 1;
}

void RegisterLuaFunctions() {
    Game::Lua::RegisterTableFunction("C_CVar", "GetCVarBitfield", &Script_GetCVarBitfield);
    Game::Lua::RegisterTableFunction("C_CVar", "SetCVarBitfield", &Script_SetCVarBitfield);
}

const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};
const Game::GlueModuleAutoRegister _glueAutoreg{&RegisterLuaFunctions};

} // namespace

} // namespace CVar::Bitfield
