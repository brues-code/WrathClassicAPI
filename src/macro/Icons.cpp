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

// `GetMacroIcons(iconList)` / `GetMacroItemIcons(iconList)` /
// `GetLooseMacroIcons(iconList)` / `GetLooseMacroItemIcons(iconList)` — the
// modern 4-function macro-icon enumeration surface. Each fills `iconList` (a new
// table when the arg is omitted) with icon texture paths and returns it.
//
// The engine's macro-icon loader already scans the icon files and splits them
// into two pre-classified lists (see Offsets.h): a spell list (Ability_*/Spell_*)
// and an item list (INV_*), each with loose disk icons folded in. So:
//   - GetMacroIcons      → the spell list
//   - GetMacroItemIcons  → the item list
//   - GetLooseMacroIcons / GetLooseMacroItemIcons → nothing (no separate loose
//     list on 3.3.5; loose icons are already in the two above). They return the
//     table unchanged so callers unioning all four don't double-count.
//
// Entries are full "Interface\\Icons\\<Name>" paths (3.3.5 has no fileID system),
// ready to hand straight to texture:SetTexture — the same shape the native
// GetMacroIconInfo returns.

#include "Game.h"
#include "Offsets.h"

#include <cstdint>
#include <cstdio>

namespace Macro::Icons {

namespace {

using LoadIcons_t = void(__cdecl *)();

int ListCount(uintptr_t countVar) {
    return *reinterpret_cast<const int *>(countVar);
}

const char *const *ListArray(uintptr_t arrayVar) {
    return *reinterpret_cast<const char *const *const *>(arrayVar);
}

// Both lists are built in a single loader call; kick it (as the native count /
// info functions do) when the spell-list count is still 0.
void EnsureLoaded() {
    if (ListCount(Offsets::VAR_MACRO_ICON_COUNT) == 0)
        reinterpret_cast<LoadIcons_t>(
            static_cast<uintptr_t>(Offsets::FUN_LOAD_MACRO_ICONS))();
}

// Leave the caller's table at stack index 1 (creating a fresh one when the arg
// was omitted / not a table), matching the modern `list = GetMacroIcons([list])`
// contract.
void PrepareTable(void *L) {
    if (Game::Lua::Type(L, 1) == Game::Lua::TYPE_TABLE) {
        Game::Lua::SetTop(L, 1);
    } else {
        Game::Lua::SetTop(L, 0);
        Game::Lua::NewTable(L);
    }
}

// Both native lists are seeded with the "INV_Misc_QuestionMark" placeholder at
// index 0. Modern base icon lists exclude it (the icon picker supplies its own
// leading "?"), so we skip it — case-insensitive, since it's the only entry we
// filter.
bool IsQuestionMark(const char *bn) {
    const char *q = "INV_Misc_QuestionMark";
    for (int i = 0;; ++i) {
        char a = bn[i], b = q[i];
        if (b == '\0')
            return a == '\0';
        if (a >= 'A' && a <= 'Z') a = static_cast<char>(a + 32);
        if (b >= 'A' && b <= 'Z') b = static_cast<char>(b + 32);
        if (a != b)
            return false;
    }
}

// First empty (nil) 1-based slot of the table at index 1 — where appending
// starts, so successive calls into the same table accumulate.
int FirstEmptyIndex(void *L) {
    int idx = 1;
    for (;;) {
        Game::Lua::PushNumber(L, static_cast<double>(idx));
        Game::Lua::RawGet(L, 1);
        const int t = Game::Lua::Type(L, -1);
        Game::Lua::SetTop(L, -2); // pop the probed value
        if (t == Game::Lua::TYPE_NIL)
            return idx;
        ++idx;
    }
}

int AppendList(void *L, uintptr_t arrayVar, uintptr_t countVar) {
    PrepareTable(L);
    EnsureLoaded();
    const int count = ListCount(countVar);
    const char *const *arr = ListArray(arrayVar);
    if (arr != nullptr && count > 0) {
        int idx = FirstEmptyIndex(L);
        for (int i = 0; i < count; ++i) {
            const char *bn = arr[i];
            if (bn == nullptr || *bn == '\0' || IsQuestionMark(bn))
                continue;
            char path[260];
            std::snprintf(path, sizeof(path), "Interface\\Icons\\%s", bn);
            Game::Lua::PushNumber(L, static_cast<double>(idx++));
            Game::Lua::PushString(L, path);
            Game::Lua::RawSet(L, 1);
        }
    }
    return 1; // the table
}

int __cdecl Script_GetMacroIcons(void *L) {
    return AppendList(L, Offsets::VAR_MACRO_ICON_ARRAY, Offsets::VAR_MACRO_ICON_COUNT);
}
int __cdecl Script_GetMacroItemIcons(void *L) {
    return AppendList(L, Offsets::VAR_MACRO_ITEM_ICON_ARRAY,
                      Offsets::VAR_MACRO_ITEM_ICON_COUNT);
}

// No separate loose catalogue on 3.3.5 — return the (possibly freshly created)
// table unchanged so unioning all four functions stays duplicate-free.
int __cdecl Script_GetLooseMacroIcons(void *L) {
    PrepareTable(L);
    return 1;
}
int __cdecl Script_GetLooseMacroItemIcons(void *L) {
    PrepareTable(L);
    return 1;
}

void RegisterLuaFunctions() {
    Game::Lua::RegisterGlobalFunction("GetMacroIcons", &Script_GetMacroIcons);
    Game::Lua::RegisterGlobalFunction("GetMacroItemIcons", &Script_GetMacroItemIcons);
    Game::Lua::RegisterGlobalFunction("GetLooseMacroIcons", &Script_GetLooseMacroIcons);
    Game::Lua::RegisterGlobalFunction("GetLooseMacroItemIcons",
                                      &Script_GetLooseMacroItemIcons);
}

const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace

} // namespace Macro::Icons
