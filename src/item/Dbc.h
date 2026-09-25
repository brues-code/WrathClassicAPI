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

namespace Item::Dbc {

// Returns the client `Item.dbc` row for `itemID`, or null when the client
// doesn't ship it (e.g. a server-custom item). Resident for every stock item
// with no server query. Read columns via the `OFF_ITEMDBC_*` offsets.
const uint8_t *GetRecord(uint32_t itemID);

} // namespace Item::Dbc
