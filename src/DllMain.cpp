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

static Game::GameUIInit_t GameUIInit_o = nullptr;

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

static void __cdecl GameUIInit_h() {
    GameUIInit_o();
    // GameUI bootstrap has now run to completion — the in-game lua_State
    // is initialized, all ~2000 engine Lua C functions are registered,
    // and `FrameScript_FillEvents` has populated the engine's event
    // table. Both prerequisites for our modules are in place:
    //   * Lua function registration — `Game::Lua::RegisterTableFunction`
    //     can attach to the populated globals.
    //   * Custom event injection — `Event::Custom::Register` reads the
    //     engine's event-list pointer and appends to it.
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

        HOOK_FUNCTION(Offsets::FUN_GAME_UI_INIT, GameUIInit_h, GameUIInit_o);

        // Feature hooks declared via `Game::HookAutoRegister` at file
        // scope in their respective modules. Installed AFTER the core
        // GameUI hook so feature hooks can rely on MinHook being live
        // but the install order between features themselves is
        // unspecified (static-init order across TUs).
        if (!Game::RunHookRegistrations())
            return FALSE;
    } else if (reason == DLL_PROCESS_DETACH) {
        MH_Uninitialize();
    }
    return TRUE;
}
