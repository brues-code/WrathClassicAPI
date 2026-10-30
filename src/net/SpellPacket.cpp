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

// Sole owner of the hooks on the spell packet handlers. See net/SpellPacket.h.

#include "net/SpellPacket.h"

#include "Game.h"
#include "Offsets.h"
#include "combat/Swing.h"
#include "net/DataStore.h"
#include "unit/IncomingResurrection.h"

#include <cstdint>

namespace Net::SpellPacket {

void RunCastObservers(uint32_t msgId, void *msg) {
    uint32_t &cursor = DataStore::Cursor(msg);
    const uint32_t start = cursor;

    // The player's swing timers (PLAYER_SWING).
    Combat::Swing::OnSpellCastPacket(msgId, msg);
    cursor = start;

    // Resurrections cast at a unit (UnitHasIncomingResurrection).
    Unit::IncomingResurrection::OnSpellCastPacket(msgId, msg);
    cursor = start;
}

void RunFailureObservers(void *msg) {
    uint32_t &cursor = DataStore::Cursor(msg);
    const uint32_t start = cursor;

    Combat::Swing::OnSpellFailurePacket(msg);
    cursor = start;

    Unit::IncomingResurrection::OnSpellFailurePacket(msg);
    cursor = start;
}

namespace {

// The engine's packet-handler signature, `int __cdecl(void *param, uint32
// msgId, uint32 time, CDataStore *msg)`.
using Handler_t = int(__cdecl *)(void *param, uint32_t msgId, uint32_t time, void *msg);
Handler_t g_castOriginal = nullptr;
Handler_t g_failureOriginal = nullptr;

int __cdecl SpellCast_h(void *param, uint32_t msgId, uint32_t time, void *msg) {
    if ((msgId == Offsets::SMSG_SPELL_START || msgId == Offsets::SMSG_SPELL_GO) && msg != nullptr)
        RunCastObservers(msgId, msg);
    return g_castOriginal(param, msgId, time, msg);
}

int __cdecl SpellFailure_h(void *param, uint32_t msgId, uint32_t time, void *msg) {
    if (msg != nullptr)
        RunFailureObservers(msg);
    return g_failureOriginal(param, msgId, time, msg);
}

const Game::HookAutoRegister _castHook{Offsets::FUN_PACKET_SPELL_CAST,
                                       reinterpret_cast<void *>(&SpellCast_h),
                                       reinterpret_cast<void **>(&g_castOriginal)};

const Game::HookAutoRegister _failureHook{Offsets::FUN_PACKET_SPELL_FAILURE,
                                          reinterpret_cast<void *>(&SpellFailure_h),
                                          reinterpret_cast<void **>(&g_failureOriginal)};

} // namespace

} // namespace Net::SpellPacket
