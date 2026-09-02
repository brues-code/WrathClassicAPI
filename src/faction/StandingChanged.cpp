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

// FACTION_STANDING_CHANGED — fires once per reputation change with
// `(factionID, newStanding, repGained)`. The modern event of the same name fires
// `(factionID, newStanding)`; we add `repGained` as a third arg because the
// engine has the delta on hand and addons commonly need it for "+X reputation"
// displays without re-deriving it from the chat string.
//
// We hook the engine's per-rep-change notify dispatcher (FUN_REPUTATION_FIRE_NOTIFY,
// the "+X reputation with <faction>" message). The SMSG_SET_FACTION_STANDING
// handler (FUN_005D20A0) calls it only when a faction's standing actually changed
// AND is visible — never from the bulk faction rebuild at login. So the event does
// not fire for the initial faction sync, matching modern semantics.
//
// The dispatcher is `__cdecl(int factionID, int delta, int isGeneric, float rate)`.
// `delta` is forwarded as `repGained` (positive on gain, negative on loss). The
// SMSG handler has already written the new standing by the time this fires, so we
// read the post-change total back via FUN_REPUTATION_GET_STANDING(factionID).

#include "Game.h"
#include "Offsets.h"
#include "event/Custom.h"

namespace Faction::StandingChanged {

namespace {

using FireNotify_t = void(__cdecl *)(int factionID, int delta, int isGeneric, float rate);
using GetStanding_t = int(__cdecl *)(int factionID);

FireNotify_t FireNotify_o = nullptr;

constexpr const char *kEventName = "FACTION_STANDING_CHANGED";

const Event::Custom::AutoReserve _reserve{kEventName};

// In-flight snapshot of the current rep change, valid only during the notify
// call. WoW's main thread is the only one that runs the rep-update SMSG handler
// and Lua callbacks, so a plain file static is sufficient.
struct LastChange {
    bool valid;
    int factionID;
    int newStanding;
    int delta;
};
LastChange g_last{};

int __cdecl Script_GetLastStandingChange(void *L) {
    if (!g_last.valid)
        return 0;
    Game::Lua::PushNumber(L, static_cast<double>(g_last.factionID));
    Game::Lua::PushNumber(L, static_cast<double>(g_last.newStanding));
    Game::Lua::PushNumber(L, static_cast<double>(g_last.delta));
    return 3;
}

void __cdecl FireNotify_h(int factionID, int delta, int isGeneric, float rate) {
    // The SMSG handler wrote the new standing before calling the notify, so
    // FUN_REPUTATION_GET_STANDING returns the post-change total.
    const int newStanding =
        reinterpret_cast<GetStanding_t>(Offsets::FUN_REPUTATION_GET_STANDING)(factionID);
    g_last = {true, factionID, newStanding, delta};

    // Forward to the engine first so its "+X reputation" message fires before our
    // FACTION_STANDING_CHANGED — preserves the ordering an addon relying on the
    // message would expect.
    FireNotify_o(factionID, delta, isGeneric, rate);

    Event::Custom::Fire(Event::Custom::Lookup(kEventName), "%d%d%d", factionID, newStanding,
                        delta);
    g_last = {};
}

void RegisterLuaFunctions() {
    Game::Lua::RegisterTableFunction("C_Reputation", "GetLastStandingChange",
                                     &Script_GetLastStandingChange);
}

const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

const Game::HookAutoRegister _hookreg{
    Offsets::FUN_REPUTATION_FIRE_NOTIFY,
    reinterpret_cast<void *>(&FireNotify_h),
    reinterpret_cast<void **>(&FireNotify_o)};

} // namespace

} // namespace Faction::StandingChanged
