// This file is part of WrathClassicAPI.
//
// WrathClassicAPI is free software: you can redistribute it and/or modify it under the terms
// of the GNU Lesser General Public License as published by the Free Software Foundation, either
// version 3 of the License, or (at your option) any later version.
//
// WrathClassicAPI is distributed in the hope that it will be useful, but WITHOUT ANY
// WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE. See the GNU Lesser General Public License for more details.
//
// You should have received a copy of the GNU Lesser General Public License along with
// WrathClassicAPI. If not, see <https://www.gnu.org/licenses/>.

#pragma once

#include <cstdint>

namespace Item::ID {

// Reads the itemID off a resolved `CGItem *` via the instance block at
// `OFF_ITEM_INSTANCE_BLOCK + OFF_INSTANCE_BLOCK_ITEM_ID`. Returns 0 for
// null/unpopulated items so callers can short-circuit to a nil push.
int FromCGItem(const uint8_t *item);

} // namespace Item::ID
