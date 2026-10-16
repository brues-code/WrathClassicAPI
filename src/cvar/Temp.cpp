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

// `C_CVar.SetTempCVar(name, value)` / `C_CVar.RemoveTempCVar(name)` — change a
// cvar for this session without the change being saved, then put the saved
// value back. Ported from ClassicAPI's src/cvar/Temp.cpp.
//
// Two engine facts make this nearly free:
//
//   1. CVar::Set only marks the config for writing when its last argument is
//      true. A temp set passes false, so it applies the value and runs the
//      change callback exactly as SetCVar does, but asks for no save.
//   2. Every write-out, Config.wtf and the account and character configs
//      alike, walks the cvars through CVar::IterateForArchive, which reads
//      each cvar's value string. When something else causes a save, a
//      co-hook points each temp cvar's value at its saved string for the
//      walk and restores it after, so the file gets exactly the line it would
//      have had without the temp value.
//
// A plain SetCVar on a temp cvar wins: the entry notices the live value is no
// longer the one it set and drops itself, so the user's own value is saved
// and never rolled back. Entries keep the cvar's name, not its object: the
// client deletes and re-creates the account and character cvars on logout
// and character switch.
//
// Validation mirrors Script_SetCVar: a usage error, `Couldn't find CVar named
// '%s'`, and `"%s" is read-only`. A cvar that applies only after a restart
// stores a set in a separate pending value that is always saved, so it can't
// be set for the session; it's refused too.

#include "Game.h"
#include "Offsets.h"
#include "cvar/CVar.h"

#include <cstdint>
#include <string>
#include <vector>

namespace CVar::Temp {

namespace {

struct Entry {
    std::string name;
    std::string saved; // the value before the first temp set
    std::string temp;  // the value that landed (after the change callback)
};

std::vector<Entry> g_entries;

Entry *FindEntry(CVar::Handle cvar) {
    for (Entry &e : g_entries)
        if (CVar::Find(e.name.c_str()) == cvar)
            return &e;
    return nullptr;
}

void EraseEntry(const Entry *entry) {
    g_entries.erase(g_entries.begin() + (entry - g_entries.data()));
}

// True while the cvar exists and its live value is still the one SetTempCVar
// put there.
bool StillTemp(const Entry &e) {
    CVar::Handle cvar = CVar::Find(e.name.c_str());
    return cvar != nullptr && e.temp == CVar::Value(cvar);
}

// Script_SetCVar's own lookup and checks, with its error text.
CVar::Handle Resolve(void *L, const char *usage) {
    if (!Game::Lua::IsString(L, 1)) {
        Game::Lua::Error(L, usage);
        return nullptr;
    }
    const char *name = Game::Lua::ToString(L, 1);
    CVar::Handle cvar = CVar::Find(name);
    if (cvar == nullptr) {
        Game::Lua::Error(L, "Couldn't find CVar named '%s'", name);
        return nullptr;
    }
    if (CVar::IsReadOnly(cvar)) {
        Game::Lua::Error(L, "\"%s\" is read-only", name);
        return nullptr;
    }
    if ((CVar::Flags(cvar) & Offsets::CVAR_FLAG_LATCHED) != 0) {
        Game::Lua::Error(L, "\"%s\" applies only after a restart", name);
        return nullptr;
    }
    return cvar;
}

int __cdecl Script_SetTempCVar(void *L) {
    CVar::Handle cvar = Resolve(L, "Usage: C_CVar.SetTempCVar(\"cvar\", value)");
    if (cvar == nullptr || CVar::IsBlocked(cvar))
        return 0;
    const char *value = Game::Lua::IsString(L, 2) ? Game::Lua::ToString(L, 2) : "";

    Entry *entry = FindEntry(cvar);
    if (entry != nullptr && !StillTemp(*entry)) {
        // A plain SetCVar replaced the temp value; that is the saved value now.
        entry->saved = CVar::Value(cvar);
    } else if (entry == nullptr) {
        g_entries.push_back({Game::Lua::ToString(L, 1), CVar::Value(cvar), {}});
    }
    CVar::Set(cvar, value, /*persist=*/false);
    // Record what landed, not what was asked: a change callback may refuse or
    // clamp. Re-find rather than reuse `entry`, since the callback runs Lua.
    if (Entry *landed = FindEntry(cvar))
        landed->temp = CVar::Value(cvar);
    return 0;
}

int __cdecl Script_RemoveTempCVar(void *L) {
    CVar::Handle cvar = Resolve(L, "Usage: C_CVar.RemoveTempCVar(\"cvar\")");
    if (cvar == nullptr)
        return 0;
    Entry *entry = FindEntry(cvar);
    if (entry == nullptr)
        return 0;
    if (StillTemp(*entry)) {
        // Copy first: the change callback could reach back in here and move
        // the vector.
        const std::string saved = entry->saved;
        EraseEntry(entry);
        CVar::Set(cvar, saved.c_str(), /*persist=*/false);
    } else {
        EraseEntry(entry);
    }
    return 0;
}

// --- Write-out co-hook ----------------------------------------------------

using IterateForArchive_t = int(__cdecl *)(uint32_t category, uint32_t excludeFlags,
                                           void *callback, void *arg);
IterateForArchive_t g_iterateOriginal = nullptr;

int __cdecl IterateForArchive_h(uint32_t category, uint32_t excludeFlags, void *callback,
                                void *arg) {
    if (g_entries.empty())
        return g_iterateOriginal(category, excludeFlags, callback, arg);

    for (size_t i = g_entries.size(); i-- > 0;)
        if (!StillTemp(g_entries[i]))
            g_entries.erase(g_entries.begin() + i);

    std::vector<const char **> slots(g_entries.size());
    std::vector<const char *> live(g_entries.size());
    for (size_t i = 0; i < g_entries.size(); ++i) {
        slots[i] = CVar::ValueSlot(CVar::Find(g_entries[i].name.c_str()));
        live[i] = *slots[i];
        *slots[i] = g_entries[i].saved.c_str();
    }
    const int result = g_iterateOriginal(category, excludeFlags, callback, arg);
    for (size_t i = 0; i < g_entries.size(); ++i)
        *slots[i] = live[i];
    return result;
}

const Game::HookAutoRegister _hookreg{
    Offsets::FUN_CVAR_ITERATE_FOR_ARCHIVE,
    reinterpret_cast<void *>(&IterateForArchive_h),
    reinterpret_cast<void **>(&g_iterateOriginal)};

void RegisterLuaFunctions() {
    Game::Lua::RegisterTableFunction("C_CVar", "SetTempCVar", &Script_SetTempCVar);
    Game::Lua::RegisterTableFunction("C_CVar", "RemoveTempCVar", &Script_RemoveTempCVar);
}

// Cvar storage is process-global, so the same surface works pre-login.
const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};
const Game::GlueModuleAutoRegister _glueAutoreg{&RegisterLuaFunctions};

} // namespace

} // namespace CVar::Temp
