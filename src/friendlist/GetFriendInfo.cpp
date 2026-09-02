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

// `C_FriendList.GetFriendInfo(name)` / `GetFriendInfoByIndex(index)` — the
// modern FriendInfo table, plus `C_FriendList.GetNumFriends()` and
// `GetNumOnlineFriends()`.
//
// Reads the friend entries directly (see `FriendList.h`). The class and zone
// names come from the same DBC lookups the engine's own GetFriendInfo uses:
// ChrClasses.dbc by the entry's class ID — gendered through the player name
// cache when the friend's record is there, like the engine — and AreaTable.dbc
// by the area ID, hopped once to the parent so a sub-area reports its zone.
//
// `notes` is the friend note the server stores (SetFriendNotes), nil when
// empty. `referAFriend` is the entry's Recruit-A-Friend flag. `mobile` is
// always false and `rafLinkType` 0 — there is no mobile app, and the flag does
// not say which side of the RAF link the friend is on.

#include "friendlist/FriendList.h"

#include "Game.h"
#include "Guid.h"
#include "dbc/Store.h"
#include "unit/NameCache.h"

#include <cstdint>

namespace FriendList::Info {

namespace {

const uint8_t *ClassRecord(int classID) {
    return DBC::Record(Offsets::VAR_CHRCLASSES_DBC_MIN_INDEX,
                       Offsets::VAR_CHRCLASSES_DBC_MAX_INDEX,
                       Offsets::VAR_CHRCLASSES_DBC_INDEX_TABLE, classID);
}

const uint8_t *AreaRecord(int areaID) {
    return DBC::Record(Offsets::VAR_AREATABLE_DBC_MIN_INDEX,
                       Offsets::VAR_AREATABLE_DBC_MAX_INDEX,
                       Offsets::VAR_AREATABLE_DBC_INDEX_TABLE, areaID);
}

const char *StringAt(const uint8_t *record, int offset) {
    return *reinterpret_cast<const char *const *>(record + offset);
}

// Localized class name for `classRec`, gendered the way the engine's own
// GetFriendInfo does it (FUN_007159E0): the friend's gender comes from their
// player-name-cache record; NameMale / NameFemale when present, else the
// neutral Name.
const char *ClassName(const uint8_t *classRec, Guid::Pair guid) {
    const uint8_t *nameRec = Unit::NameCache::Record(guid.lo, guid.hi);
    if (nameRec != nullptr) {
        const int gender =
            *reinterpret_cast<const int32_t *>(nameRec + Offsets::OFF_PLAYER_NAME_REC_GENDER);
        const char *male = StringAt(classRec, Offsets::OFF_CHRCLASSES_NAME_MALE);
        const char *female = StringAt(classRec, Offsets::OFF_CHRCLASSES_NAME_FEMALE);
        const char *first = (gender == 1) ? female : male;
        const char *second = (gender == 1) ? male : female;
        if (gender == 0 || gender == 1) {
            if (first != nullptr && *first != '\0')
                return first;
            if (second != nullptr && *second != '\0')
                return second;
        }
    }
    return StringAt(classRec, Offsets::OFF_CHRCLASSES_NAME);
}

// Localized zone name for `areaID`, resolved one hop to the parent area (as the
// engine does), or null when unknown.
const char *AreaName(int areaID) {
    const uint8_t *rec = AreaRecord(areaID);
    if (rec == nullptr)
        return nullptr;
    const int parentID =
        *reinterpret_cast<const int32_t *>(rec + Offsets::OFF_AREATABLE_PARENT_AREA_ID);
    if (parentID != 0)
        if (const uint8_t *parent = AreaRecord(parentID))
            rec = parent;
    return StringAt(rec, Offsets::OFF_AREATABLE_NAME);
}

// Push the modern FriendInfo table for `entry` on top of the Lua stack.
void PushFriendInfo(void *L, const uint8_t *entry) {
    const Guid::Pair guid = Guid::Split(FriendGuid(entry));
    const uint8_t flags = *(entry + OFF_FRIEND_FLAGS);
    const int level = *reinterpret_cast<const int32_t *>(entry + OFF_FRIEND_LEVEL);
    const int classID = *reinterpret_cast<const int32_t *>(entry + OFF_FRIEND_CLASS_ID);
    const int areaID = *reinterpret_cast<const int32_t *>(entry + OFF_FRIEND_AREA_ID);
    const char *note = reinterpret_cast<const char *>(entry + OFF_FRIEND_NOTE);

    Game::Lua::NewTable(L);
    if (const char *name = FriendName(entry))
        Game::Lua::SetFieldString(L, "name", name);
    Game::Lua::SetFieldBool(L, "connected", FriendConnected(entry));
    Game::Lua::SetFieldNumber(L, "level", static_cast<double>(level));
    // Optional strings stay nil (unset) when unknown rather than "" — "" is
    // truthy in Lua and would fool an `if info.area then` check.
    if (const uint8_t *classRec = ClassRecord(classID)) {
        if (const char *className = ClassName(classRec, guid))
            Game::Lua::SetFieldString(L, "className", className);
        if (const char *token = StringAt(classRec, Offsets::OFF_CHRCLASSES_FILENAME))
            Game::Lua::SetFieldString(L, "classFilename", token);
    }
    if (const char *area = AreaName(areaID))
        Game::Lua::SetFieldString(L, "area", area);
    if (guid.valid()) {
        char buf[Guid::STRING_SIZE];
        Game::Lua::SetFieldString(L, "guid", Guid::Format(guid, buf, sizeof buf));
    }
    if (*note != '\0')
        Game::Lua::SetFieldString(L, "notes", note);
    Game::Lua::SetFieldBool(L, "afk", (flags & FRIEND_FLAG_AFK) != 0);
    Game::Lua::SetFieldBool(L, "dnd", (flags & FRIEND_FLAG_DND) != 0);
    Game::Lua::SetFieldBool(L, "mobile", false);
    Game::Lua::SetFieldBool(L, "referAFriend", (flags & FRIEND_FLAG_REFER_A_FRIEND) != 0);
    Game::Lua::SetFieldNumber(L, "rafLinkType", 0);
}

int __cdecl Script_GetNumFriends(void *L) {
    Game::Lua::PushNumber(L, static_cast<double>(FriendList::Count()));
    return 1;
}

// `C_FriendList.GetNumOnlineFriends()` — friends whose `connected` byte is set,
// the same count the stock GetNumFriends returns as its second value.
int __cdecl Script_GetNumOnlineFriends(void *L) {
    int online = 0;
    const int count = FriendList::Count();
    for (int i = 0; i < count; ++i)
        if (FriendConnected(FriendList::EntryByIndex(i)))
            ++online;
    Game::Lua::PushNumber(L, static_cast<double>(online));
    return 1;
}

int __cdecl Script_GetFriendInfoByIndex(void *L) {
    if (!Game::Lua::IsNumber(L, 1)) {
        Game::Lua::PushNil(L);
        return 1;
    }
    const int index = static_cast<int>(Game::Lua::ToNumber(L, 1)); // 1-based
    const uint8_t *entry = FriendList::EntryByIndex(index - 1);
    if (entry == nullptr) {
        Game::Lua::PushNil(L);
        return 1;
    }
    PushFriendInfo(L, entry);
    return 1;
}

int __cdecl Script_GetFriendInfo(void *L) {
    const char *name = Game::Lua::IsString(L, 1) ? Game::Lua::ToString(L, 1) : nullptr;
    const uint8_t *entry = FriendList::EntryByName(name);
    if (entry == nullptr) {
        Game::Lua::PushNil(L);
        return 1;
    }
    PushFriendInfo(L, entry);
    return 1;
}

void RegisterLuaFunctions() {
    Game::Lua::RegisterTableFunction("C_FriendList", "GetNumFriends", &Script_GetNumFriends);
    Game::Lua::RegisterTableFunction("C_FriendList", "GetNumOnlineFriends",
                                     &Script_GetNumOnlineFriends);
    Game::Lua::RegisterTableFunction("C_FriendList", "GetFriendInfoByIndex",
                                     &Script_GetFriendInfoByIndex);
    Game::Lua::RegisterTableFunction("C_FriendList", "GetFriendInfo", &Script_GetFriendInfo);
}

const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace

} // namespace FriendList::Info
