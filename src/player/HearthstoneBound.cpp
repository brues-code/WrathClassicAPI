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

// `HEARTHSTONE_BOUND` (no payload) — fires each time the player binds their
// hearthstone, including a rebind at the same inn. Addons re-read
// `GetBindLocation()` on it. Ported from ClassicAPI's
// src/player/HearthstoneBound.cpp.
//
// The trigger is SMSG_PLAYERBOUND, not the SMSG_BINDPOINTUPDATE ClassicAPI
// hooks: the server re-sends the bind point at every login and map change,
// but sends SMSG_PLAYERBOUND only from the bind spell effect, right after
// the bind-point update that carries the new location. So nothing needs
// filtering, and GetBindLocation() already answers with the new inn when
// the event fires.

#include "Game.h"
#include "Offsets.h"
#include "event/Custom.h"

#include <cstdint>

namespace Player::HearthstoneBound {

namespace {

const char *const kHearthstoneBound = "HEARTHSTONE_BOUND";
const Event::Custom::AutoReserve _reserveHearthstoneBound{kHearthstoneBound};

using PacketHandler_t = int(__cdecl *)(void *param, uint32_t msgId, uint32_t time, void *msg);
PacketHandler_t g_packetHandlerOriginal = nullptr;

int __cdecl PacketHandler_h(void *param, uint32_t msgId, uint32_t time, void *msg) {
    const int result = g_packetHandlerOriginal(param, msgId, time, msg);
    if (msgId == Offsets::OPCODE_SMSG_PLAYERBOUND)
        Event::Custom::Fire(Event::Custom::Lookup(kHearthstoneBound), "");
    return result;
}

const Game::HookAutoRegister _hookreg{
    Offsets::FUN_PACKET_PLAYER_GROUP_9,
    reinterpret_cast<void *>(&PacketHandler_h),
    reinterpret_cast<void **>(&g_packetHandlerOriginal)};

} // namespace

} // namespace Player::HearthstoneBound
