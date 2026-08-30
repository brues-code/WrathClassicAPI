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

// FrameScript in-game Lua-state teardown — see `Offsets::FUN_FRAMESCRIPT_SHUTDOWN`.
// Fires on `/reload` and `/logout` before the old state is destroyed. Pre-hooked
// so modules can clear reload-fragile state (Lua refs, pointer-keyed maps) while
// the old state is still valid (see `Game::ReloadAutoRegister`).
using FrameScriptShutdown_t = void(__cdecl *)();
static FrameScriptShutdown_t FrameScriptShutdown_o = nullptr;

static void __cdecl FrameScriptShutdown_h() {
    Game::RunReloadCleanups();
    FrameScriptShutdown_o();
}

// ---------------------------------------------------------------------------
// Hook install — kept OFF the Windows loader lock.
//
// Every MH_ApplyQueued freezes all process threads (CreateToolhelp32Snapshot +
// SuspendThread/GetThreadContext each). Doing that from DllMain, under the
// loader lock, is the pattern MinHook documents as unsafe: it can stall the
// remote LoadLibrary thread and it races the DllMains of the OTHER DLLs the
// loader injects after us (each injection is serialized, so a worker spawned
// from our DllMain starts while LichLoader is still injecting the next
// lichloader.txt entry, and our prologue patching then fights that DLL's
// DllMain over the same engine functions).
//
// So DllMain installs nothing. The install runs later, off the loader lock,
// via one of two triggers that both funnel through the latched
// EnsureInitialized():
//   * LichCore's `Load` export, called on the game's main thread after the
//     process resumes, when every DllMain in the injection chain has already
//     completed, serialized in lichloader.txt order — no race, and
//   * a fallback worker thread we spawn from DllMain, ONLY when LichCore is
//     not present (LichLoader runs without it, just warns), so a plain
//     injection still initializes.
// This mirrors ClassicAPI's DllMain exactly (VanillaFixes `Load` there;
// LichCore `Load` here).
// ---------------------------------------------------------------------------

static volatile LONG g_initClaimed = 0;    // 0 until a thread takes the installer role
static volatile LONG g_initDone = 0;       // 0 until the install has finished
static volatile LONG g_initResult = 1;     // 0 = success, 1 = failure (until proven)
static volatile LONG g_mhInitialized = 0;  // MH_Initialize succeeded (gates detach teardown)

static bool CreateAndQueue(uintptr_t offset, void *hook, void **original) {
    auto *target = reinterpret_cast<LPVOID>(offset);
    if (MH_CreateHook(target, hook, original) != MH_OK)
        return false;
    // Queue only — a single MH_ApplyQueued below applies the whole batch in
    // one thread-freeze.
    if (MH_QueueEnableHook(target) != MH_OK)
        return false;
    return true;
}

static bool InstallHooks() {
    if (MH_Initialize() != MH_OK)
        return false;
    InterlockedExchange(&g_mhInitialized, 1);

    // Disable the engine's "function pointer must live in Wow.exe's .text"
    // gate before any of our Script_* closures are invoked. A plain data
    // write (no thread-freeze); safe here, well before our closures are
    // registered by the UIBindings hook.
    DisableInvalidFunctionPtrCheck();

    // Core bootstrap hook: fires the module chain once the in-game Lua
    // state is ready (see UIBindingsInit_h). Create + queue only.
    if (!CreateAndQueue(Offsets::FUN_UIBINDINGS_INIT,
                        reinterpret_cast<void *>(UIBindingsInit_h),
                        reinterpret_cast<void **>(&UIBindingsInit_o)))
        return false;

    // Core reload hook: fires module reload-cleanups before the in-game
    // Lua state is torn down on /reload or /logout (see FrameScriptShutdown_h).
    if (!CreateAndQueue(Offsets::FUN_FRAMESCRIPT_SHUTDOWN,
                        reinterpret_cast<void *>(FrameScriptShutdown_h),
                        reinterpret_cast<void **>(&FrameScriptShutdown_o)))
        return false;

    // Feature hooks declared via `Game::HookAutoRegister` at file scope in
    // their respective modules (create + queue-enable, no apply yet).
    if (!Game::RunHookRegistrations())
        return false;

    // One thread-freeze that activates every queued hook at once.
    return MH_ApplyQueued() == MH_OK;
}

// Runs InstallHooks exactly once. Returns 0 on success, 1 on failure. If a
// second caller arrives while the install is in flight, it blocks briefly so
// both callers observe the real result.
static DWORD EnsureInitialized() {
    if (InterlockedCompareExchange(&g_initClaimed, 1, 0) == 0) {
        g_initResult = InstallHooks() ? 0 : 1;
        InterlockedExchange(&g_initDone, 1);
    } else {
        while (InterlockedCompareExchange(&g_initDone, 0, 0) == 0)
            Sleep(1);
    }
    return static_cast<DWORD>(g_initResult);
}

static DWORD WINAPI InitWorker(LPVOID) {
    EnsureInitialized();
    return 0;
}

// LichCore calls this on the game's MAIN thread after injection
// (GetProcAddress(module, "Load")), outside the loader lock and with no
// timeout. Returns 0 on success; LichCore reports any non-zero result to the
// user. Exported undecorated as "Load" via src/WrathClassicAPI.def.
extern "C" DWORD __cdecl Load() { return EnsureInitialized(); }

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID lpReserved) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(hModule);

        // Install nothing here (loader lock — see the block comment above).
        // Under LichCore, `Load` is the ONLY install trigger; spawning the
        // worker would race the DllMains of the DLLs LichLoader injects after
        // us. Only when LichCore is absent (LichLoader ran without it) do we
        // self-init from a worker off the lock.
        if (GetModuleHandleW(L"LichCore.dll") == nullptr) {
            // The new thread cannot run its body until DllMain returns and the
            // loader lock releases, and we never wait on it.
            // DisableThreadLibraryCalls suppressed its THREAD_ATTACH.
            HANDLE worker = CreateThread(nullptr, 0, InitWorker, nullptr, 0, nullptr);
            if (worker != nullptr)
                CloseHandle(worker);
        }
    } else if (reason == DLL_PROCESS_DETACH) {
        // Only tear down if we actually initialized.
        if (g_mhInitialized)
            MH_Uninitialize();
    }
    return TRUE;
}
