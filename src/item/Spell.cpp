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

#include <cstdint>

namespace Item::Spell {

namespace {

using GetOnUseSpellID_t = uint32_t(__cdecl *)(uint32_t itemID);
using DBCCopyRecord_t = int(__thiscall *)(void *desc, int id, void *outBuffer);

uint32_t ResolveOnUseSpellID(uint32_t itemID) {
    if (itemID == 0)
        return 0;
    auto fn = reinterpret_cast<GetOnUseSpellID_t>(
        static_cast<uintptr_t>(Offsets::FUN_ITEM_GET_ONUSE_SPELL));
    return fn(itemID);
}

// Copies the Spell.dbc record for `spellID` into `out`. Same
// shape Aura::Data uses — see `OFF_SPELL_NAME` comment in
// Offsets.h for the rationale (locale-normalisation via the
// engine helper).
bool CopySpellRecord(uint32_t spellID,
                     uint8_t out[Offsets::SPELL_DBC_RECORD_SIZE]) {
    if (spellID == 0)
        return false;
    auto fn = reinterpret_cast<DBCCopyRecord_t>(
        static_cast<uintptr_t>(Offsets::FUN_DBC_COPY_RECORD));
    auto *desc = reinterpret_cast<void *>(
        static_cast<uintptr_t>(Offsets::VAR_SPELL_DBC_DESC));
    return fn(desc, static_cast<int>(spellID), out) != 0;
}

const char *SpellNameByID(uint32_t spellID) {
    if (spellID == 0)
        return nullptr;
    uint8_t buf[Offsets::SPELL_DBC_RECORD_SIZE];
    if (!CopySpellRecord(spellID, buf))
        return nullptr;
    return *reinterpret_cast<const char *const *>(buf + Offsets::OFF_SPELL_NAME);
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
    const char *name = SpellNameByID(spellID);
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
