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

// `## LoadSavedVariablesFirst: 1` — modern TOC directive, backported.
//
// Retail loads a flagged addon's SavedVariables BEFORE its Lua runs, so
// file-scope code (`local db = MyAddonDB`) sees restored config immediately.
// 3.3.5 does the opposite: the addon loader (FUN_ADDON_LOADADDON) runs the
// addon's files first, THEN loads the account and per-character SavedVariables,
// THEN fires ADDON_LOADED. So file-scope SV is always nil, and the engine never
// parses the directive.
//
// We observe the per-addon file-list loader (FUN_TOC_EXECUTOR, which the addon
// loader calls to run the addon's files; the hook itself is owned by
// src/addons/TocExecutor.cpp, shared with GetAddOnLocalTable). For an addon
// whose TOC declares `## LoadSavedVariablesFirst` (nonzero), load its
// SavedVariables first —
// mirroring the two paths the engine itself loads a few steps later, and gating
// each on file existence exactly as the engine does (3.3.5 loads SV on
// existence, not on the `## SavedVariables` declaration):
//   account : WTF\Account\<account>\SavedVariables\<Name>.lua
//   per-char: WTF\Account\<account>\<realm>\<character>\SavedVariables\<Name>.lua
// then return so the files run with SV present.
//
// The engine STILL runs its own SavedVariables load right after the files. Left
// alone, that re-load would overwrite any value the addon wrote at file scope
// before ADDON_LOADED. So we also hook FUN_LUA_LOAD_FILE and suppress that one
// redundant re-load: each path we pre-load is remembered, and the engine's
// imminent load of that exact path is skipped once. That makes file-scope reads
// AND writes both survive. Our own early load calls the trampoline directly, so
// it is never self-suppressed. FUN_LUA_LOAD_FILE is a cold, addon-load-time path
// (the file-list loader runs the addon's own files through a different loader).

#include "addons/SavedVarsFirst.h"

#include "Game.h"
#include "Offsets.h"
#include "addons/EngineIO.h"
#include "addons/Toc.h"

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace Addons::SavedVarsFirst {

namespace {

using Addons::EngineIO::FileExistsFn;
using Addons::EngineIO::FileReadFn;
using Addons::EngineIO::SMemFreeFn;
using Addons::Toc::FindValue;
using Addons::Toc::Lower;

// The Lua file loader (Offsets::FUN_LUA_LOAD_FILE), plain __cdecl on this build.
using LuaLoadFile_t = uint32_t(__cdecl *)(const char *path, void *ctx);
using NameGetter_t = const char *(__cdecl *)(); // no-arg session-name readers

LuaLoadFile_t LuaLoadFile_o = nullptr;

// Full SV paths we pre-loaded whose imminent engine re-load must be skipped
// once. Populated in LoadSavedVarsEarly, drained by LuaLoad_h. Only ever holds a
// handful of entries (one addon's SV files, briefly). Main-thread only.
std::vector<std::string> g_pendingSuppress;

constexpr size_t NPOS = static_cast<size_t>(-1);

bool SamePathCI(const char *a, const char *b) {
    for (; *a != '\0' && *b != '\0'; ++a, ++b)
        if (Lower(*a) != Lower(*b)) return false;
    return *a == *b;
}

// Extract "<Name>" from an addon base-TOC path `…\AddOns\<Name>\<Name>.toc`.
// Returns false for anything else (GlueXML.toc, FrameXML.toc, nested paths, …)
// so the reorder only touches real addon loads.
bool AddonNameFromToc(const char *path, char *out, size_t outSize) {
    const size_t len = std::strlen(path);
    if (len < 4 || !(path[len - 4] == '.' && Lower(path[len - 3]) == 't' &&
                     Lower(path[len - 2]) == 'o' && Lower(path[len - 1]) == 'c'))
        return false;
    size_t lastSep = NPOS;
    for (size_t i = len; i-- > 0;)
        if (path[i] == '\\' || path[i] == '/') { lastSep = i; break; }
    if (lastSep == NPOS) return false;
    size_t prevSep = NPOS;
    for (size_t i = lastSep; i-- > 0;)
        if (path[i] == '\\' || path[i] == '/') { prevSep = i; break; }
    if (prevSep == NPOS) return false;
    size_t gpSep = NPOS;
    for (size_t i = prevSep; i-- > 0;)
        if (path[i] == '\\' || path[i] == '/') { gpSep = i; break; }
    if (gpSep == NPOS) return false;
    if (prevSep - (gpSep + 1) != 6 ||
        !Addons::Toc::EqCI(path + gpSep + 1, 6, "AddOns"))
        return false;
    const size_t nameLen = lastSep - (prevSep + 1);
    if (nameLen == 0 || nameLen + 1 > outSize) return false;
    std::memcpy(out, path + prevSep + 1, nameLen);
    out[nameLen] = '\0';
    return true;
}

// True iff the leading integer of a TOC value is nonzero (WoW numeric flag).
bool NonzeroFlag(const char *v, size_t n) {
    long val = 0;
    bool digit = false;
    for (size_t i = 0; i < n && v[i] >= '0' && v[i] <= '9'; ++i) {
        val = val * 10 + (v[i] - '0');
        digit = true;
    }
    return digit && val != 0;
}

// Read the addon's TOC (through the hooked FUN_FILE_READ, so a rewritten/flavor
// TOC is honored) and report whether `## LoadSavedVariablesFirst` is nonzero.
bool WantsSavedVarsFirst(const char *tocPath) {
    auto FileRead =
        reinterpret_cast<FileReadFn>(static_cast<uintptr_t>(Offsets::FUN_FILE_READ));
    void *buf = nullptr;
    size_t size = 0;
    if (FileRead(0, tocPath, &buf, &size, 1, 1, 0) == 0 || buf == nullptr)
        return false;
    const char *v = nullptr;
    size_t n = 0;
    const bool want =
        FindValue(static_cast<const char *>(buf), size,
                  "## LoadSavedVariablesFirst:", &v, &n) &&
        NonzeroFlag(v, n);
    reinterpret_cast<SMemFreeFn>(static_cast<uintptr_t>(Offsets::FUN_STORM_SMEM_FREE))(
        buf, __FILE__, __LINE__, 0);
    return want;
}

bool FileExists(const char *path) {
    return reinterpret_cast<FileExistsFn>(
               static_cast<uintptr_t>(Offsets::FUN_FILE_EXISTS))(path, 1) != 0;
}

// Load a SavedVariables file, then mark its path so the engine's own re-load of
// it (right after the addon's files) is suppressed — preserving any file-scope
// write. Calls the trampoline directly so this load is never itself suppressed.
void RunLuaFile(const char *path) {
    if (LuaLoadFile_o == nullptr)
        return;
    LuaLoadFile_o(path, nullptr); // ctx is null-safe (see Offsets::FUN_LUA_LOAD_FILE)
    g_pendingSuppress.emplace_back(path);
}

// Load the addon's SavedVariables early, mirroring the two paths and the
// existence gate FUN_ADDON_LOADADDON uses for its own SV step, so every path we
// pre-load is one the engine will re-load (and thus every suppress entry drains).
void LoadSavedVarsEarly(const char *addonName) {
    const char *account =
        reinterpret_cast<const char *>(static_cast<uintptr_t>(Offsets::VAR_ACCOUNT_NAME));
    if (account == nullptr || *account == '\0')
        return;

    char path[260];

    // Account-wide (WTF\Account\<account>\SavedVariables\<Name>.lua).
    std::snprintf(path, sizeof path, "WTF\\Account\\%s\\SavedVariables\\%s.lua",
                  account, addonName);
    if (FileExists(path))
        RunLuaFile(path);

    // Realm-scoped per-character — the only per-char file the engine loads.
    const char *realm = reinterpret_cast<NameGetter_t>(
        static_cast<uintptr_t>(Offsets::FUN_GET_REALM_NAME))();
    const char *character = reinterpret_cast<NameGetter_t>(
        static_cast<uintptr_t>(Offsets::FUN_GET_LOGIN_ACCOUNT_NAME))();
    if (realm == nullptr || *realm == '\0' || character == nullptr ||
        *character == '\0')
        return;
    std::snprintf(path, sizeof path,
                  "WTF\\Account\\%s\\%s\\%s\\SavedVariables\\%s.lua", account,
                  realm, character, addonName);
    if (FileExists(path))
        RunLuaFile(path);
}

// Skip the engine's one redundant SavedVariables re-load per pre-loaded path.
// Every other file load passes straight through. Match is by full path
// (case-insensitive); a path is suppressed exactly once. Our own early load
// bypasses this via the trampoline.
uint32_t __cdecl LuaLoad_h(const char *path, void *ctx) {
    if (path != nullptr) {
        for (size_t i = 0; i < g_pendingSuppress.size(); ++i) {
            if (SamePathCI(g_pendingSuppress[i].c_str(), path)) {
                g_pendingSuppress.erase(g_pendingSuppress.begin() +
                                        static_cast<std::ptrdiff_t>(i));
                return 0; // suppress — the addon's file-scope SV writes survive
            }
        }
    }
    return LuaLoadFile_o(path, ctx);
}

const Game::HookAutoRegister _hookLua{
    Offsets::FUN_LUA_LOAD_FILE, reinterpret_cast<void *>(&LuaLoad_h),
    reinterpret_cast<void **>(&LuaLoadFile_o)};

} // namespace

void OnTocExecute(const char *tocPath, const char *) {
    char addonName[128];
    if (tocPath != nullptr && AddonNameFromToc(tocPath, addonName, sizeof addonName) &&
        WantsSavedVarsFirst(tocPath))
        LoadSavedVarsEarly(addonName);
}

} // namespace Addons::SavedVarsFirst
