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

// Engine bootstrap wiring shared by every front-end. The detours here translate
// two engine seams into module-lifecycle calls, and the gate write admits our
// DLL-resident closures. A front-end installs all of it through its own
// IHookHost (MinHook for the LichLoader DLL, WXL's registry for a WXL
// extension) — so the offsets and detour bodies live in exactly one place.

#include "Game.h"
#include "Offsets.h"

#include <cstdint>

namespace Game {
namespace {

// `UIBindings::Initialize()` — see the load-order notes on
// `Offsets::FUN_UIBINDINGS_INIT`. Hooked POST as the bootstrap signal:
// in-game-only, fires once after the in-game event table is populated and
// before FrameXML.toc / addons load.
using UIBindingsInit_t = void(__cdecl *)();
UIBindingsInit_t UIBindingsInit_o = nullptr;

// FrameScript in-game Lua-state teardown — see `Offsets::FUN_FRAMESCRIPT_SHUTDOWN`.
// Fires on `/reload` and `/logout` before the old state is destroyed. Hooked PRE
// so modules can clear reload-fragile state (Lua refs, pointer-keyed maps) while
// the old state is still valid (see `Game::ReloadAutoRegister`).
using FrameScriptShutdown_t = void(__cdecl *)();
FrameScriptShutdown_t FrameScriptShutdown_o = nullptr;

// Open the engine's "valid Lua-C function pointer" range wide enough to accept
// any user-mode pointer. The check function at `FUN_0086B5A0` reads these
// globals on every closure dispatch and errors if the pointer falls outside
// `[lo, hi)`. Original values pin the range to `Wow.exe`'s own .text section;
// our DLL-resident `Script_*` closures are well outside it, so the check would
// otherwise raise ERROR #134.
//
// awesome_wotlk uses the same approach. Sharing this data write (instead of
// detouring the check function) means our DLL coexists cleanly with theirs —
// and with any WXL extension doing the same — because it's an idempotent write,
// not a detour competing at a hot dispatch site.
void DisableInvalidFunctionPtrCheck() {
    *reinterpret_cast<uint32_t *>(Offsets::VAR_VALID_FUNCPTR_LO) = 1;
    *reinterpret_cast<uint32_t *>(Offsets::VAR_VALID_FUNCPTR_HI) = 0x7FFFFFFF;
}

void __cdecl UIBindingsInit_h() {
    UIBindingsInit_o();
    // Re-assert the closure-pointer gate here as well as at install time. A
    // front-end may install before the engine has populated the range globals
    // (WXL arms extensions very early in engine init), in which case the
    // install-time write would be clobbered by the engine's own initialization.
    // This point is in-game, after that initialization and before any addon can
    // dispatch one of our closures, so the widened range is guaranteed live when
    // it matters. Idempotent, so the redundant install-time write costs nothing.
    DisableInvalidFunctionPtrCheck();
    // Engine has finished the in-game `GameUIInit` work up through FillEvents +
    // CVars + UIBindings init. Lua state is in-game, event table is populated,
    // FrameXML.toc has not yet loaded. Run the module chain so our globals are
    // visible to addon main chunks (which run as part of FrameXML.toc loading,
    // transitively).
    RunModuleRegistrations();
}

void __cdecl FrameScriptShutdown_h() {
    RunReloadCleanups();
    FrameScriptShutdown_o();
}

} // namespace

bool InstallCoreHooks(IHookHost &host) {
    // Disable the engine's "function pointer must live in Wow.exe's .text" gate
    // before any of our Script_* closures are invoked. A plain data write (no
    // thread-freeze); safe well before our closures are registered by the
    // UIBindings hook.
    DisableInvalidFunctionPtrCheck();

    // Bootstrap hook: fires the module chain once the in-game Lua state is ready
    // (see UIBindingsInit_h).
    if (!host.Install(Offsets::FUN_UIBINDINGS_INIT,
                      reinterpret_cast<void *>(&UIBindingsInit_h),
                      reinterpret_cast<void **>(&UIBindingsInit_o)))
        return false;

    // Reload hook: fires module reload-cleanups before the in-game Lua state is
    // torn down on /reload or /logout (see FrameScriptShutdown_h).
    if (!host.Install(Offsets::FUN_FRAMESCRIPT_SHUTDOWN,
                      reinterpret_cast<void *>(&FrameScriptShutdown_h),
                      reinterpret_cast<void **>(&FrameScriptShutdown_o)))
        return false;

    return true;
}

} // namespace Game
