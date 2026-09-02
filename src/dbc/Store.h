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

namespace DBC {

// Row lookup in a `[min, max]`-bounded client DBC store — the WowClientDB shape
// the engine uses for ChrRaces, ChrClasses, AreaTable, Faction, …: three
// globals (min ID, max ID, pointer to an array of record pointers indexed by
// `id - min`). Returns the record for `id`, or null when `id` is out of range,
// the store is not loaded, or the slot is empty. Pass the store's
// `Offsets::VAR_*_DBC_{MIN_INDEX,MAX_INDEX,INDEX_TABLE}` triple.
inline const uint8_t *Record(uintptr_t minIndexVar, uintptr_t maxIndexVar,
                             uintptr_t indexTableVar, int id) {
    const int minIndex = *reinterpret_cast<const int32_t *>(minIndexVar);
    const int maxIndex = *reinterpret_cast<const int32_t *>(maxIndexVar);
    if (id < minIndex || id > maxIndex)
        return nullptr;
    auto *table = *reinterpret_cast<const uint8_t *const *const *>(indexTableVar);
    if (table == nullptr)
        return nullptr;
    return table[id - minIndex];
}

} // namespace DBC
