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

#include "net/Packet.h"

#include "Offsets.h"

namespace Net::Packet {

namespace {

using Ctor_t = void *(__thiscall *)(void *self);
using PutU32_t = void *(__thiscall *)(void *self, uint32_t value);
using Finalize_t = void(__thiscall *)(void *self);
using Dtor_t = void(__thiscall *)(void *self);
using Send2_t = void(__cdecl *)(void *msg);

} // namespace

void SendUint32(uint32_t opcode, uint32_t value) {
    alignas(4) uint8_t msg[Offsets::CDATASTORE_SIZE];
    reinterpret_cast<Ctor_t>(Offsets::FUN_CDATASTORE_CTOR)(msg);
    reinterpret_cast<PutU32_t>(Offsets::FUN_CDATASTORE_PUT_U32)(msg, opcode);
    reinterpret_cast<PutU32_t>(Offsets::FUN_CDATASTORE_PUT_U32)(msg, value);
    reinterpret_cast<Finalize_t>(Offsets::FUN_CDATASTORE_FINALIZE)(msg);
    reinterpret_cast<Send2_t>(Offsets::FUN_CLIENT_SERVICES_SEND2)(msg);
    reinterpret_cast<Dtor_t>(Offsets::FUN_CDATASTORE_DTOR)(msg);
}

} // namespace Net::Packet
