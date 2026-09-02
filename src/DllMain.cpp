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

// LichLoader front-end. This is the DLL LichLoader injects into Wow.exe. It owns
// the injection lifecycle (loader-lock-safe install, LichCore `Load` entry, the
// no-LichCore fallback worker) and backs the core's Game::IHookHost with MinHook.
// All engine hooks and Lua registrations live in the loader-agnostic core
// (Bootstrap.cpp + the feature modules); this file only decides WHEN and via
// WHICH hook engine they get installed.

#include "Game.h"
#include "MinHook.h"

#include <windows.h>

// ---------------------------------------------------------------------------
// MinHook-backed IHookHost.
//
// The core records its hooks (Bootstrap.cpp's lifecycle hooks + every feature
// module's HookAutoRegister) and installs them through Game::IHookHost, never
// referencing a hook engine directly. This front-end supplies the MinHook
// implementation: each Install create + queue-enables the detour; Commit applies
// the whole queued batch in a single thread-freeze (MH_ApplyQueued).
//
// Keeping the batch in one freeze matters on machines whose security stack
// intercepts SuspendThread per call — and since the install runs off the loader
// lock (see below), the freeze is safe here.
// ---------------------------------------------------------------------------
namespace {
struct MinHookHost final : Game::IHookHost {
    bool Install(uintptr_t target, void *detour, void **original) override {
        auto *t = reinterpret_cast<LPVOID>(target);
        if (MH_CreateHook(t, detour, original) != MH_OK)
            return false;
        // Queue only — Commit()'s single MH_ApplyQueued applies the whole batch.
        return MH_QueueEnableHook(t) == MH_OK;
    }
    bool Commit() { return MH_ApplyQueued() == MH_OK; }
};
MinHookHost g_host;
} // namespace

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

static bool InstallHooks() {
    if (MH_Initialize() != MH_OK)
        return false;
    InterlockedExchange(&g_mhInitialized, 1);

    // Core lifecycle hooks + the closure-pointer gate write (Bootstrap.cpp),
    // installed through our MinHook host. Create + queue only.
    if (!Game::InstallCoreHooks(g_host))
        return false;

    // Feature hooks declared via `Game::HookAutoRegister` at file scope in their
    // respective modules. Create + queue only.
    if (!Game::RunHookRegistrations(g_host))
        return false;

    // One thread-freeze that activates every queued hook at once.
    return g_host.Commit();
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
// timeout, at CGlueMgr::Initialize (the login-screen init). Returns 0 on
// success; LichCore reports any non-zero result to the user. Exported
// undecorated as "Load" via src/WrathClassicAPI.def.
extern "C" DWORD __cdecl Load() {
    const DWORD result = EnsureInitialized();
    // Fire login-screen registrations (console commands, ...) now: the login
    // `~` console is up and GlueXML has loaded. Main-thread, LichCore-only —
    // the fallback worker path has no login-screen-timed signal.
    if (result == 0)
        Game::RunGlueModuleRegistrations();
    return result;
}

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
