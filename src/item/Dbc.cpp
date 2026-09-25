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

#include "item/Dbc.h"

#include "Offsets.h"

namespace Item::Dbc {

// The inline WowClientDB GetRow: bounds-check the itemID against
// [minID, maxID], then index the id->record table (null entries are gaps).
const uint8_t *GetRecord(uint32_t itemID) {
    const auto *store = reinterpret_cast<const uint8_t *>(Offsets::VAR_ITEMDBC_STORE);
    const int id = static_cast<int>(itemID);
    const int minID = *reinterpret_cast<const int *>(store + Offsets::OFF_ITEMDBC_STORE_MIN_ID);
    const int maxID = *reinterpret_cast<const int *>(store + Offsets::OFF_ITEMDBC_STORE_MAX_ID);
    if (id < minID || id > maxID)
        return nullptr;
    auto *index = *reinterpret_cast<const uint8_t *const *const *>(
        store + Offsets::OFF_ITEMDBC_STORE_INDEX);
    if (index == nullptr)
        return nullptr;
    return index[id - minID];
}

} // namespace Item::Dbc
