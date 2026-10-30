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

#pragma once

#include <cstdint>

// The spell packet handlers, shared by the features that read those packets:
// `Offsets::FUN_PACKET_SPELL_CAST` (SMSG_SPELL_START / SMSG_SPELL_GO) and
// `Offsets::FUN_PACKET_SPELL_FAILURE` (SMSG_SPELL_FAILURE).
//
// A hook target may carry only one detour, so SpellPacket.cpp owns both hooks
// and calls each observer below before the engine's handler runs. Each observer
// is declared by its own module and reads the packet from its start with
// Net::DataStore; the read cursor is put back after each one, so observers are
// independent of each other and the handler sees the packet untouched.

namespace Net::SpellPacket {

// An SMSG_SPELL_START or SMSG_SPELL_GO (`msgId`) is about to be handled.
void RunCastObservers(uint32_t msgId, void *msg);

// An SMSG_SPELL_FAILURE is about to be handled.
void RunFailureObservers(void *msg);

} // namespace Net::SpellPacket
