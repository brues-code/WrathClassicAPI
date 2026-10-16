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

// `GetShapeshiftFormID()` — the local player's current form as its
// SpellShapeshiftForm.dbc ID, or nil when not in a form. Ported from
// ClassicAPI's src/unit/ShapeshiftForm.cpp.
//
// The client has the bar-index API (GetShapeshiftForm, GetShapeshiftFormInfo)
// but no ID getter. Reads the form through CGUnit_C::GetShapeshiftForm, the
// accessor the stance bar's "is this form active" check uses, so a
// CancelShapeshiftForm shows up at once instead of after the server's reply.

#include "Game.h"
#include "Offsets.h"
#include "unit/Player.h"

#include <cstdint>

namespace Unit::ShapeshiftForm {

namespace {

// `uint32_t __thiscall(const CGUnit_C *)`, no stack args.
using GetShapeshiftForm_t = uint32_t(__fastcall *)(const void *unit);

int __cdecl Script_GetShapeshiftFormID(void *L) {
    const void *player = Unit::LocalPlayer();
    const uint32_t form =
        player != nullptr
            ? reinterpret_cast<GetShapeshiftForm_t>(Offsets::FUN_UNIT_GET_SHAPESHIFT_FORM)(player)
            : 0;
    if (form == 0)
        Game::Lua::PushNil(L);
    else
        Game::Lua::PushNumber(L, static_cast<double>(form));
    return 1;
}

void RegisterLuaFunctions() {
    Game::Lua::RegisterGlobalFunction("GetShapeshiftFormID", &Script_GetShapeshiftFormID);
}

const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace

} // namespace Unit::ShapeshiftForm
