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

// `C_UIColor.GetColors()` — modern Blizzard exposes the contents of
// `GlobalColor.dbc` to Lua through this engine function so that
// `Blizzard_SharedXMLBase/Color.lua` can wrap each row with
// `CreateColor(r,g,b,a)` and assign the result to globals like
// `ACHIEVEMENT_COLOR`. WotLK 3.3.5a has `GlobalColor.dbc` but no
// `C_UIColor` namespace (that came in 8.0). We embed a Blizzard-
// provided snapshot (BC Classic 2.5.5 build 67323) and register the
// function ourselves.
//
// Each returned row is `{ baseTag = "FOO_COLOR", color = {r,g,b,a} }`
// where the inner color is a plain Lua table (not a `ColorMixin`).
// ClassicAPI's same function tries to wrap via `CreateColor` if the
// addon-side has already defined it; we don't bother here because:
//   (a) consumers always re-wrap via `CreateColor(r,g,b,a)` anyway —
//       the modern `Color.lua` loop does this on every row regardless
//       of whether the inner value came in as a Mixin;
//   (b) skipping the `CreateColor` call removes the dependency on a
//       `lua_pcall` binding (would otherwise need a new offset).
// `/dump C_UIColor.GetColors()[1].color` shows `{r=, g=, b=, a=}`
// instead of a ColorMixin — a cosmetic difference only.

#include "ColorData.h"
#include "Game.h"

#include <cstdint>

namespace UI::Color {

namespace {

struct RGBA {
    double r, g, b, a;
};

RGBA Decode(int32_t argb) {
    const uint32_t v = static_cast<uint32_t>(argb);
    return {
        static_cast<double>((v >> 16) & 0xFF) / 255.0,
        static_cast<double>((v >> 8) & 0xFF) / 255.0,
        static_cast<double>(v & 0xFF) / 255.0,
        static_cast<double>((v >> 24) & 0xFF) / 255.0,
    };
}

// Pushes a plain `{r=, g=, b=, a=}` table on top of the stack.
void PushPlainColor(void *L, const RGBA &c) {
    Game::Lua::NewTable(L);
    Game::Lua::SetFieldNumber(L, "r", c.r);
    Game::Lua::SetFieldNumber(L, "g", c.g);
    Game::Lua::SetFieldNumber(L, "b", c.b);
    Game::Lua::SetFieldNumber(L, "a", c.a);
}

int __cdecl Script_GetColors(void *L) {
    Game::Lua::SetTop(L, 0);
    Game::Lua::NewTable(L); // outer array at stack index 1

    for (int i = 0; i < ColorData::kColorCount; ++i) {
        const auto &entry = ColorData::kColors[i];
        const RGBA c = Decode(entry.argb);

        // Build the inner row table on top of the outer.
        Game::Lua::NewTable(L); // stack: [outer, inner]

        Game::Lua::SetFieldString(L, "baseTag", entry.baseTag);

        PushPlainColor(L, c);             // stack: [outer, inner, color]
        Game::Lua::SetField(L, -2, "color"); // inner.color = color; pops color.

        // outer[i+1] = inner. lua_rawseti would be ideal but we don't
        // bind it; push the integer key + use RawSet, which pops both.
        Game::Lua::PushNumber(L, static_cast<double>(i + 1)); // stack: [outer, inner, key]
        Game::Lua::Insert(L, -2);                              // stack: [outer, key, inner]
        Game::Lua::RawSet(L, -3);                              // outer[key] = inner; pops key+inner.
    }

    return 1;
}

void RegisterLuaFunctions() {
    Game::Lua::RegisterTableFunction("C_UIColor", "GetColors", &Script_GetColors);
}

const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace
} // namespace UI::Color
