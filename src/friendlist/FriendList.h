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

#pragma once

#include "Offsets.h"

#include <cstdint>
#include <cstring>

// Shared access to the client's contact list — the social singleton at
// `Offsets::VAR_SOCIAL_SYSTEM`. Friends are inline entries from offset 0,
// stride 0x220, up to 100; the ignore list is a GUID array at +0xD490, stride
// 0x10, up to 50. Both lists are contiguous and end at the first zero GUID —
// the same terminate-on-zero contract the engine's own GetNumFriends /
// GetNumIgnores rely on. Field offsets verified in Script_GetFriendInfo
// (FUN_006B4130) and Script_GetIgnoreName (FUN_006B4620).

namespace FriendList {

constexpr int FRIEND_ENTRY_STRIDE = 0x220;
constexpr int MAX_FRIENDS = 100;

constexpr int OFF_FRIEND_CONNECTED = 0x000; // u8  — 0 = offline
constexpr int OFF_FRIEND_FLAGS = 0x001;     // u8  — FRIEND_FLAG_*
constexpr int OFF_FRIEND_NAME = 0x004;      // char*
constexpr int OFF_FRIEND_NOTE = 0x008;      // char[0x200] inline; "" = no note
constexpr int OFF_FRIEND_GUID = 0x208;      // u64
constexpr int OFF_FRIEND_LEVEL = 0x210;     // i32 — 0 while offline
constexpr int OFF_FRIEND_CLASS_ID = 0x214;  // i32 → ChrClasses.dbc; 0 while offline
constexpr int OFF_FRIEND_AREA_ID = 0x218;   // i32 → AreaTable.dbc; 0 while offline

constexpr uint8_t FRIEND_FLAG_AFK = 0x02;
constexpr uint8_t FRIEND_FLAG_DND = 0x04;
constexpr uint8_t FRIEND_FLAG_REFER_A_FRIEND = 0x08;

constexpr int OFF_IGNORE_LIST = 0xD490;
constexpr int IGNORE_ENTRY_STRIDE = 0x10; // u64 GUID + 8 unused bytes
constexpr int MAX_IGNORES = 50;

// The social singleton, or null before login.
inline const uint8_t *Social() {
    return *reinterpret_cast<const uint8_t *const *>(
        static_cast<uintptr_t>(Offsets::VAR_SOCIAL_SYSTEM));
}

inline uint64_t FriendGuid(const uint8_t *entry) {
    return *reinterpret_cast<const uint64_t *>(entry + OFF_FRIEND_GUID);
}

inline const char *FriendName(const uint8_t *entry) {
    return *reinterpret_cast<const char *const *>(entry + OFF_FRIEND_NAME);
}

inline bool FriendConnected(const uint8_t *entry) {
    return *(entry + OFF_FRIEND_CONNECTED) != 0;
}

// Number of populated friend slots — the leading entries with a nonzero GUID,
// capped at MAX_FRIENDS. What GetNumFriends returns (mirrors FUN_006B34A0).
inline int Count() {
    const uint8_t *social = Social();
    if (social == nullptr)
        return 0;
    int n = 0;
    while (n < MAX_FRIENDS && FriendGuid(social + n * FRIEND_ENTRY_STRIDE) != 0)
        ++n;
    return n;
}

// Friend entry at 0-based `index`, or null when out of the populated range.
inline const uint8_t *EntryByIndex(int index) {
    if (index < 0 || index >= Count())
        return nullptr;
    return Social() + index * FRIEND_ENTRY_STRIDE;
}

// The friend entry whose name matches `name` case-insensitively, or null.
// Mirrors the engine's by-name lookup (FUN_006B3570).
inline const uint8_t *EntryByName(const char *name) {
    if (name == nullptr || *name == '\0')
        return nullptr;
    const int count = Count();
    for (int i = 0; i < count; ++i) {
        const uint8_t *entry = Social() + i * FRIEND_ENTRY_STRIDE;
        const char *fname = FriendName(entry);
        if (fname != nullptr && _stricmp(fname, name) == 0)
            return entry;
    }
    return nullptr;
}

// The friend entry with GUID `guid`, or null. The list keeps the GUID for
// online AND offline friends.
inline const uint8_t *EntryByGuid(uint64_t guid) {
    if (guid == 0)
        return nullptr;
    const int count = Count();
    for (int i = 0; i < count; ++i) {
        const uint8_t *entry = Social() + i * FRIEND_ENTRY_STRIDE;
        if (FriendGuid(entry) == guid)
            return entry;
    }
    return nullptr;
}

// True if `guid` is on the ignore list. Walks the GUID array, stopping at the
// first zero entry (mirrors the engine's own by-GUID check, FUN_006B52E0).
inline bool IsGuidIgnored(uint64_t guid) {
    const uint8_t *social = Social();
    if (social == nullptr || guid == 0)
        return false;
    for (int i = 0; i < MAX_IGNORES; ++i) {
        const uint64_t entry = *reinterpret_cast<const uint64_t *>(
            social + OFF_IGNORE_LIST + i * IGNORE_ENTRY_STRIDE);
        if (entry == 0)
            break;
        if (entry == guid)
            return true;
    }
    return false;
}

} // namespace FriendList
