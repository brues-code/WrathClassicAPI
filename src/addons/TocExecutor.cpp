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

// Sole owner of the hook on the TOC file-list executor
// (`Offsets::FUN_TOC_EXECUTOR`) — the point where the engine is about to run a
// TOC's files, and the seam two features need.
//
// A hook target may carry only ONE detour: the LichLoader front-end installs
// with MinHook, which rejects a second hook on an address outright. So the two
// consumers below register nothing themselves; this module hooks once and calls
// them in a fixed order before handing off to the engine.

#include "addons/TocExecutor.h"

#include "Game.h"
#include "Offsets.h"
#include "addons/GetAddOnLocalTable.h"
#include "addons/SavedVarsFirst.h"

#include <cstdint>

namespace Addons::TocExecutor {

void RunObservers(const char *tocPath, const char *addonName) {
    // 1. Capture the addon's namespace table. MUST run first: it identifies the
    //    table by reading the Lua stack top, and step 2 runs Lua on that same
    //    state. Pure peek + registry stash, so it leaves the stack as it found it.
    AddOns::GetAddOnLocalTable::OnTocExecute(tocPath, addonName);

    // 2. Load a `## LoadSavedVariablesFirst` addon's SavedVariables, so its files
    //    see them at file scope. Runs Lua.
    Addons::SavedVarsFirst::OnTocExecute(tocPath, addonName);
}

namespace {

// `int __cdecl(const char *tocPath, const char *addonName, void *unused,
// void **logger)` — see Offsets::FUN_TOC_EXECUTOR.
using TocExecutor_t = int(__cdecl *)(const char *tocPath, const char *addonName, void *unused,
                                     void **logger);
TocExecutor_t TocExecutor_o = nullptr;

int __cdecl TocExecutor_h(const char *tocPath, const char *addonName, void *unused,
                          void **logger) {
    RunObservers(tocPath, addonName);
    return TocExecutor_o(tocPath, addonName, unused, logger);
}

const Game::HookAutoRegister _hookreg{Offsets::FUN_TOC_EXECUTOR,
                                      reinterpret_cast<void *>(&TocExecutor_h),
                                      reinterpret_cast<void **>(&TocExecutor_o)};

} // namespace

} // namespace Addons::TocExecutor
