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

// `C_AddOns.GetAddOnLocalTable(addOnName)` — modern WoW backport.
//
// Returns the addon's per-addon namespace table (the second value
// the engine passes as `...` to every file in the addon) IF and only
// if the TOC declares `## AllowAddOnTableAccess: 1`. The table is the
// same one the addon's own files see via the standard
// `local addOnName, addon = ...` idiom, so cross-addon code can read
// shared state without going through an explicit global.
//
// 3.3.5 already creates the per-addon table — the LoadAddOn flow at
// `FUN_005f80b0` calls `lua_newtable(L)` immediately before invoking
// the TOC executor (FUN_TOC_EXECUTOR) and the engine's per-`.lua`
// loader (FUN_00818e50) does `lua_pushvalue(L, -4)` to dup that table
// as the 2nd `pcall` arg. After the executor returns the engine pops
// the table with `lua_settop(L, -2)`, so we have to intercept and
// hold a reference before that happens.
//
// Hook strategy: observe FUN_TOC_EXECUTOR (the hook itself is owned by
// src/addons/TocExecutor.cpp, which this shares with
// LoadSavedVariablesFirst). At entry, when the TOC path matches the
// addon-side layout AND the Lua stack top is a table, we stash the
// table into our own registry-stored lookup table keyed by lowercased
// addon name, and read the same TOC's `AllowAddOnTableAccess` directive
// into a C++ map — through the engine's file reader, as the engine reads
// the TOC (the engine itself keeps only Title/Notes/Author/Version/X-*
// as metadata). The Lua function then gates on the flag and serves from
// the registry table.
//
// Failure modes (all return nil to match modern WoW):
//   * Addon not loaded yet (no LoadAddOn fire)         → nil
//   * Addon loaded without `AllowAddOnTableAccess: 1` → nil
//   * Name misspelled / unknown addon                  → nil

#include "addons/GetAddOnLocalTable.h"

#include "Game.h"
#include "addons/TocFile.h"

#include <cctype>
#include <cstring>
#include <string>
#include <unordered_map>

namespace AddOns::GetAddOnLocalTable {

namespace {

// Registry key for our per-process addon-namespace lookup table.
// Stored in the Lua registry; lazy-created on first use.
constexpr const char *kRegistryKey = "WrathClassicAPI:AddOnNamespaces";

// Engine path layout for an addon's TOC file. Used to disambiguate the
// LoadAddOn caller from FrameXML.toc / Glues.toc loaders that share
// FUN_TOC_EXECUTOR but don't have a namespace table to capture.
constexpr const char *kAddOnPathPrefix = "Interface\\AddOns\\";

std::string Lowercase(const char *s) {
    std::string out;
    if (s == nullptr)
        return out;
    out.reserve(std::strlen(s));
    for (; *s != 0; ++s)
        out.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(*s))));
    return out;
}

// `lowercase(addon_name) -> AllowAddOnTableAccess` flag. Populated by
// OnTocExecute. Single-threaded access (Lua is single-threaded on this
// build) — no mutex needed.
std::unordered_map<std::string, bool> g_allowFlag;

// The namespace tables die with the Lua state, and every addon loads (and
// its TOC is read) again after /reload.
void PrepareForReload() { g_allowFlag.clear(); }

const Game::ReloadAutoRegister _reload{&PrepareForReload};

// Pushes our per-process namespace-lookup registry table onto the Lua
// stack, creating it on first use. Caller pops when done.
void PushNamespaceRegistry(void *L) {
    Game::Lua::GetField(L, Game::Lua::REGISTRY_INDEX, kRegistryKey);
    if (Game::Lua::Type(L, -1) != Game::Lua::TYPE_TABLE) {
        Game::Lua::SetTop(L, -2);                                        // pop non-table
        Game::Lua::CreateTable(L, 0, 16);                                // [t]
        Game::Lua::PushValue(L, -1);                                     // [t, t]
        Game::Lua::SetField(L, Game::Lua::REGISTRY_INDEX, kRegistryKey); // [t]
    }
}

int __cdecl Script_C_AddOns_GetAddOnLocalTable(void *L) {
    if (!Game::Lua::IsString(L, 1))
        return Game::Lua::Error(L, "Usage: C_AddOns.GetAddOnLocalTable(addOnName)");

    const std::string key = Lowercase(Game::Lua::ToString(L, 1));

    const auto it = g_allowFlag.find(key);
    if (it == g_allowFlag.end() || !it->second) {
        Game::Lua::PushNil(L);
        return 1;
    }

    PushNamespaceRegistry(L);                 // [registry]
    Game::Lua::GetField(L, -1, key.c_str());  // [registry, ns_or_nil]
    Game::Lua::Remove(L, -2);                 // [ns_or_nil]
    return 1;
}

void RegisterLuaFunctions() {
    Game::Lua::RegisterTableFunction("C_AddOns", "GetAddOnLocalTable",
                                     &Script_C_AddOns_GetAddOnLocalTable);
}

const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace

void OnTocExecute(const char *tocPath, const char *addonName) {
    void *L = Game::Lua::State();

    // Only the LoadAddOn caller has a fresh `lua_newtable` on the stack
    // at entry. Two filters disambiguate it from the FrameXML/Glues
    // callers: the path prefix, and the actual stack-top type.
    const bool isAddonPath =
        tocPath != nullptr &&
        std::strncmp(tocPath, kAddOnPathPrefix, std::strlen(kAddOnPathPrefix)) == 0;

    if (L == nullptr || addonName == nullptr || !isAddonPath ||
        Game::Lua::Type(L, -1) != Game::Lua::TYPE_TABLE)
        return;

    const std::string key = Lowercase(addonName);
    g_allowFlag[key] = Addons::TocFile::ReadFlag(tocPath, "AllowAddOnTableAccess");

    // Stack at entry: [..., ns_tbl]
    PushNamespaceRegistry(L);                // [..., ns_tbl, registry]
    Game::Lua::PushValue(L, -2);             // [..., ns_tbl, registry, ns_tbl]
    Game::Lua::SetField(L, -2, key.c_str()); // [..., ns_tbl, registry] (pops ns_tbl)
    Game::Lua::SetTop(L, -2);                // [..., ns_tbl] (pop registry)
}

} // namespace AddOns::GetAddOnLocalTable
