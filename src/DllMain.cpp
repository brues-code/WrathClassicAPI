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

#include "Common.h"
#include "Game.h"
#include "MinHook.h"
#include "Offsets.h"

#include <windows.h>

// `UIBindings::Initialize()` — see the load-order notes on
// `Offsets::FUN_UIBINDINGS_INIT`. Hooked POST as the bootstrap
// signal: in-game-only, fires once after the in-game event table
// is populated and before FrameXML.toc / addons load.
using UIBindingsInit_t = void(__cdecl *)();
static UIBindingsInit_t UIBindingsInit_o = nullptr;

// Open the engine's "valid Lua-C function pointer" range wide enough
// to accept any user-mode pointer. The check function at
// `FUN_0086B5A0` reads these globals on every closure dispatch and
// errors if the pointer falls outside `[lo, hi)`. Original values
// pin the range to `Wow.exe`'s own .text section; our DLL-resident
// `Script_*` closures are well outside it, so the check would
// otherwise raise ERROR #134.
//
// awesome_wotlk uses the same approach. Sharing this data write
// (instead of MinHooking the check function) means our DLL coexists
// cleanly with theirs — no detour competition at a hot dispatch
// site.
static void DisableInvalidFunctionPtrCheck() {
    *reinterpret_cast<DWORD *>(Offsets::VAR_VALID_FUNCPTR_LO) = 1;
    *reinterpret_cast<DWORD *>(Offsets::VAR_VALID_FUNCPTR_HI) = 0x7FFFFFFF;
}

static void __cdecl UIBindingsInit_h() {
    UIBindingsInit_o();
    // Engine has finished the in-game `GameUIInit` work up through
    // FillEvents + CVars + UIBindings init. Lua state is in-game,
    // event table is populated, FrameXML.toc has not yet loaded.
    // Run the module chain so our globals are visible to addon main
    // chunks (which run as part of FrameXML.toc loading, transitively).
    Game::RunModuleRegistrations();
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID lpReserved) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(hModule);

        if (MH_Initialize() != MH_OK)
            return FALSE;

        // Disable the engine's "function pointer must live in Wow.exe's
        // .text" gate before any of our Script_* closures are invoked.
        DisableInvalidFunctionPtrCheck();

        HOOK_FUNCTION(Offsets::FUN_UIBINDINGS_INIT, UIBindingsInit_h, UIBindingsInit_o);

        // Feature hooks declared via `Game::HookAutoRegister` at file
        // scope in their respective modules. Installed AFTER the core
        // bootstrap hook so feature hooks can rely on MinHook being
        // live but the install order between features themselves is
        // unspecified (static-init order across TUs).
        if (!Game::RunHookRegistrations())
            return FALSE;
    } else if (reason == DLL_PROCESS_DETACH) {
        MH_Uninitialize();
    }
    return TRUE;
}
