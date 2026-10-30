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

#include "Offsets.h"

#include <cstdint>

// Reads an incoming packet (the engine's `CDataStore`) through the engine's own
// readers, for modules that peek at a packet before its handler runs. Each read
// advances the read cursor. A read past the end leaves the cursor past the end
// (CDataStore::FetchRead sets it to `m_size + 1`), and what it returned is not
// meaningful — test Overrun before trusting the values.

namespace Net::DataStore {

// The read cursor (`CDataStore::m_read`).
inline uint32_t &Cursor(void *msg) {
    return *reinterpret_cast<uint32_t *>(static_cast<uint8_t *>(msg) +
                                         Offsets::OFF_DATASTORE_READ);
}

// Whether a read ran past the end of the packet.
inline bool Overrun(void *msg) {
    const uint32_t size = *reinterpret_cast<const uint32_t *>(static_cast<uint8_t *>(msg) +
                                                              Offsets::OFF_DATASTORE_SIZE);
    return Cursor(msg) > size;
}

inline uint8_t ReadU8(void *msg) {
    using Get_t = void *(__fastcall *)(void *msg, void *edx, uint8_t *out);
    uint8_t v = 0;
    reinterpret_cast<Get_t>(Offsets::FUN_DATASTORE_GET_U8)(msg, nullptr, &v);
    return v;
}

inline uint32_t ReadU32(void *msg) {
    using Get_t = void *(__fastcall *)(void *msg, void *edx, uint32_t *out);
    uint32_t v = 0;
    reinterpret_cast<Get_t>(Offsets::FUN_DATASTORE_GET_U32)(msg, nullptr, &v);
    return v;
}

inline uint64_t ReadPackedGuid(void *msg) {
    using Get_t = void *(__cdecl *)(void *msg, uint64_t *out);
    uint64_t guid = 0;
    reinterpret_cast<Get_t>(Offsets::FUN_DATASTORE_GET_PACKED_GUID)(msg, &guid);
    return guid;
}

} // namespace Net::DataStore
