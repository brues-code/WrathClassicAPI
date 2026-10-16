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

// `PLAYER_ENTERING_WORLD(isInitialLogin, isReloadingUi)` — the 8.0.1
// payload, backported. Ported from ClassicAPI's src/event/EnteringWorld.cpp.
//
// The engine fires PEW with no payload from CGGameUI::EnterWorld, through
// the variadic dispatcher vFrameScript_SignalEvent. We hook that dispatcher
// and, for PEW only, hand the original the two booleans instead (`%b`, so
// they arrive as real true/false).
//
// Both flags come from the engine's own state:
//   - isReloadingUi: CGGameUI::m_currentlyReloadingUI, set for the whole
//     /reload UI rebuild that fires PEW.
//   - isInitialLogin: EnterWorld fires PLAYER_LOGIN just before PEW when
//     the UI was freshly built — a character login or a /reload — so a
//     PLAYER_LOGIN that isn't part of a reload marks a fresh login.
// A loading screen fires neither, so both are false.

#include "Game.h"
#include "Offsets.h"
#include "event/Custom.h"

#include <cstdarg>
#include <cstdint>

namespace Event::EnteringWorld {

namespace {

using vSignalEvent_t = void(__cdecl *)(int eventID, const char *format, va_list args);
vSignalEvent_t g_signalEventOriginal = nullptr;

// Engine event IDs, resolved once the event table exists; -1 matches no
// event. They're fixed for the process.
int g_loginID = -1;
int g_enteringWorldID = -1;
// PLAYER_LOGIN fired since the last PLAYER_ENTERING_WORLD.
bool g_loginFired = false;

void __cdecl SignalEvent_h(int eventID, const char *format, va_list args) {
    if (eventID == g_loginID) {
        g_loginFired = true;
    } else if (eventID == g_enteringWorldID) {
        const bool isReloadingUi = *reinterpret_cast<const volatile uint8_t *>(
                                       static_cast<uintptr_t>(
                                           Offsets::VAR_UI_CURRENTLY_RELOADING)) != 0;
        const bool isInitialLogin = g_loginFired && !isReloadingUi;
        g_loginFired = false;
        int payload[2] = {isInitialLogin ? 1 : 0, isReloadingUi ? 1 : 0};
        g_signalEventOriginal(eventID, "%b%b", reinterpret_cast<va_list>(payload));
        return;
    }
    g_signalEventOriginal(eventID, format, args);
}

const Game::HookAutoRegister _hookreg{
    Offsets::FUN_VFIRE_EVENT,
    reinterpret_cast<void *>(&SignalEvent_h),
    reinterpret_cast<void **>(&g_signalEventOriginal)};

// Runs on every in-game UI build, before EnterWorld fires the events.
void ResolveEventIDs() {
    g_loginID = Event::Custom::Lookup("PLAYER_LOGIN");
    g_enteringWorldID = Event::Custom::Lookup("PLAYER_ENTERING_WORLD");
}

const Game::ModuleAutoRegister _autoreg{&ResolveEventIDs};

} // namespace

} // namespace Event::EnteringWorld
