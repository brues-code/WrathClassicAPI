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

// `C_Item.GetItemSpell(item)` — namespaced version of the stock
// `GetItemSpell` global that returns the modern `(name, spellID)`
// shape rather than 3.3.5's `(name, rank)`. The stock global is
// left untouched.
//
// Used both by addons that want a modern-shaped call and
// internally by `C_Container.PlayerHasHearthstone` /
// `UseHearthstone`, which match items by spellID (hearthstone
// spellID 8690) rather than by hardcoded itemIDs — so any item
// the engine considers a hearthstone-equivalent (e.g. the
// Hearthstone Toy, custom-server reskinned hearthstones)
// counts.

#include "Game.h"
#include "Offsets.h"
#include "item/Arg.h"
#include "spell/Lookup.h"

#include <cstdint>

namespace Item::Spell {

namespace {

using GetOnUseSpellID_t = uint32_t(__cdecl *)(uint32_t itemID);

uint32_t ResolveOnUseSpellID(uint32_t itemID) {
    if (itemID == 0)
        return 0;
    auto fn = reinterpret_cast<GetOnUseSpellID_t>(
        static_cast<uintptr_t>(Offsets::FUN_ITEM_GET_ONUSE_SPELL));
    return fn(itemID);
}

int __cdecl Script_GetItemSpell(void *L) {
    const int itemID = Item::Arg::ResolveItemID(L, 1);
    if (itemID <= 0) {
        Game::Lua::PushNil(L);
        Game::Lua::PushNil(L);
        return 2;
    }
    const uint32_t spellID = ResolveOnUseSpellID(static_cast<uint32_t>(itemID));
    if (spellID == 0) {
        Game::Lua::PushNil(L);
        Game::Lua::PushNil(L);
        return 2;
    }
    const char *name = ::Spell::Lookup::NameForSpell(spellID);
    Game::Lua::PushString(L, name);
    Game::Lua::PushNumber(L, static_cast<double>(spellID));
    return 2;
}

void RegisterLuaFunctions() {
    Game::Lua::RegisterTableFunction("C_Item", "GetItemSpell",
                                     &Script_GetItemSpell);
}

const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace

// Public — exposed so Container::Hearthstone can do spellID
// matching without re-inventing the engine call.
uint32_t OnUseSpellIDForItem(uint32_t itemID) {
    return ResolveOnUseSpellID(itemID);
}

} // namespace Item::Spell
