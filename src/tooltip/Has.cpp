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

// `GameTooltip:HasSpell()` / `:HasItem()` / `:HasUnit()` — boolean
// predicates mirroring the modern WoW API.
//
// 3.3.5 already has `:GetSpell()` / `:GetItem()` / `:GetUnit()`, which
// return nil when the tooltip isn't displaying that content type, so
// the `Has*` predicates are conceptually `(self:GetX()) ~= nil`. Doing
// it natively (rather than via an addon-side Lua shim) is the project's
// stated goal — these become real frame methods registered into the
// engine's `GameTooltip` method table the same way `Set*` / `Get*` are.
//
// Implementation strategy:
//   * The engine registers tooltip methods by walking a static table at
//     `Offsets::FUN_TOOLTIP_METHODS_WALKER`. Body of that walker is just
//     `parent_methods(L); register_frame_methods(L, methodTable, count)`.
//   * We MinHook the walker; our hook calls the original (which registers
//     all 69 stock entries), then calls `register_frame_methods` again
//     with our own 3-entry extension table.
//   * The registrar is the simple `for (i; i<n) lua_settable` loop at
//     `Offsets::FUN_REGISTER_FRAME_METHODS`, so multiple invocations
//     accumulate into the same method table on the Lua stack.
//
// Reading the content state:
//   * Lua method calls pass `self` as arg 1 (a Lua table representing
//     the frame). The C++ frame pointer is stored at `self[0]` as a
//     `lightuserdata`. Pattern is verified against the engine's own
//     `Get*` methods, which do the equivalent via the `FUN_004A81B0`
//     helper. We extract directly via `lua_rawgeti` + `lua_touserdata`
//     to avoid the engine's type-check error (we'd rather degrade to
//     `false` than raise if someone misuses the API).
//   * Spell / item / unit content offsets all live on the tooltip
//     frame itself — see comments on `OFF_TOOLTIP_*` in `Offsets.h`.

#include "Common.h"
#include "Game.h"
#include "MinHook.h"
#include "Offsets.h"

#include <cstdint>

namespace Tooltip::Has {

namespace {

using TooltipMethodWalker_t = void(__cdecl *)(void *L);
using RegisterFrameMethods_t = void(__cdecl *)(void *L, const void *table, int count);

TooltipMethodWalker_t Walker_o = nullptr;

// Returns the C++ frame pointer for the Lua `self` table at stack
// index 1. `self[0]` (rawget by integer key) is the lightuserdata the
// engine stashes when binding a frame to a Lua table. Returns nullptr
// for any unexpected shape; caller pushes `false` and returns.
//
// Leaves the Lua stack as it found it (pushes the userdata, reads it,
// pops it).
void *FrameFromSelf(void *L) {
    if (Game::Lua::Type(L, 1) != Game::Lua::TYPE_TABLE)
        return nullptr;
    Game::Lua::RawGetI(L, 1, 0);
    void *frame = Game::Lua::ToUserdata(L, -1);
    Game::Lua::SetTop(L, -2);
    return frame;
}

int __cdecl Script_HasSpell(void *L) {
    auto *frame = static_cast<const uint8_t *>(FrameFromSelf(L));
    if (frame == nullptr) {
        Game::Lua::PushBool(L, false);
        return 1;
    }
    const int32_t spellID = *reinterpret_cast<const int32_t *>(
        frame + Offsets::OFF_TOOLTIP_SPELL_ID);
    Game::Lua::PushBool(L, spellID != 0);
    return 1;
}

int __cdecl Script_HasItem(void *L) {
    auto *frame = static_cast<const uint8_t *>(FrameFromSelf(L));
    if (frame == nullptr) {
        Game::Lua::PushBool(L, false);
        return 1;
    }
    const int32_t itemID = *reinterpret_cast<const int32_t *>(
        frame + Offsets::OFF_TOOLTIP_ITEM_ID);
    Game::Lua::PushBool(L, itemID != 0);
    return 1;
}

int __cdecl Script_HasUnit(void *L) {
    auto *frame = static_cast<const uint8_t *>(FrameFromSelf(L));
    if (frame == nullptr) {
        Game::Lua::PushBool(L, false);
        return 1;
    }
    const uint32_t guidLo = *reinterpret_cast<const uint32_t *>(
        frame + Offsets::OFF_TOOLTIP_UNIT_GUID_LO);
    const uint32_t guidHi = *reinterpret_cast<const uint32_t *>(
        frame + Offsets::OFF_TOOLTIP_UNIT_GUID_HI);
    Game::Lua::PushBool(L, (guidLo | guidHi) != 0);
    return 1;
}

// (name, func) entries appended to the GameTooltip method table.
// Layout must match the engine's expectation: 8 bytes per entry, name
// pointer first, function pointer second — same shape as the static
// table at 0x00AD2AE0 that the registrar already walks.
struct MethodEntry {
    const char *name;
    Game::Lua::CFunction func;
};

const MethodEntry kExtraMethods[] = {
    { "HasSpell", &Script_HasSpell },
    { "HasItem",  &Script_HasItem  },
    { "HasUnit",  &Script_HasUnit  },
};
constexpr int kExtraMethodCount = sizeof(kExtraMethods) / sizeof(kExtraMethods[0]);

void __cdecl Walker_h(void *L) {
    Walker_o(L);  // stock 69 entries
    auto registerFn = reinterpret_cast<RegisterFrameMethods_t>(
        Offsets::FUN_REGISTER_FRAME_METHODS);
    registerFn(L, kExtraMethods, kExtraMethodCount);
}

const Game::HookAutoRegister _hookreg{
    Offsets::FUN_TOOLTIP_METHODS_WALKER,
    reinterpret_cast<void *>(&Walker_h),
    reinterpret_cast<void **>(&Walker_o)};

} // namespace
} // namespace Tooltip::Has
