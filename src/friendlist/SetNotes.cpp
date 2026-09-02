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

// `C_FriendList.SetFriendNotes(name, notes)` / `SetFriendNotesByIndex(index,
// notes)` — set a friend's note. Both return `found`: true when the friend is on
// the list and the note was applied, false otherwise (a miss is silent — no
// error toast, unlike the stock SetFriendNotes).
//
// The note goes through the engine's own setter (`Offsets::
// FUN_SOCIAL_SET_FRIEND_NOTES`, the primitive behind the stock SetFriendNotes):
// it updates the entry's inline note, fires FRIENDLIST_UPDATE, and sends the
// note to the server. An empty (or nil) note clears it.

#include "friendlist/FriendList.h"

#include "Game.h"
#include "Guid.h"
#include "Offsets.h"

#include <cstdint>

namespace FriendList::SetNotes {

namespace {

using SetFriendNotes_t = void(__thiscall *)(void *social, uint32_t guidLo, uint32_t guidHi,
                                            const char *notes);

// Apply the note at Lua arg 2 to `entry`. False when there is no such friend.
bool Apply(void *L, const uint8_t *entry) {
    if (entry == nullptr)
        return false;
    const char *note = Game::Lua::IsString(L, 2) ? Game::Lua::ToString(L, 2) : "";
    const Guid::Pair guid = Guid::Split(FriendGuid(entry));
    reinterpret_cast<SetFriendNotes_t>(static_cast<uintptr_t>(
        Offsets::FUN_SOCIAL_SET_FRIEND_NOTES))(const_cast<uint8_t *>(Social()), guid.lo,
                                              guid.hi, note);
    return true;
}

int __cdecl Script_SetFriendNotes(void *L) {
    const char *name = Game::Lua::IsString(L, 1) ? Game::Lua::ToString(L, 1) : nullptr;
    Game::Lua::PushBool(L, Apply(L, FriendList::EntryByName(name)));
    return 1;
}

int __cdecl Script_SetFriendNotesByIndex(void *L) {
    const uint8_t *entry = nullptr;
    if (Game::Lua::IsNumber(L, 1))
        entry = FriendList::EntryByIndex(static_cast<int>(Game::Lua::ToNumber(L, 1)) - 1);
    Game::Lua::PushBool(L, Apply(L, entry));
    return 1;
}

void RegisterLuaFunctions() {
    Game::Lua::RegisterTableFunction("C_FriendList", "SetFriendNotes", &Script_SetFriendNotes);
    Game::Lua::RegisterTableFunction("C_FriendList", "SetFriendNotesByIndex",
                                     &Script_SetFriendNotesByIndex);
}

const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace

} // namespace FriendList::SetNotes
