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

// Shared between `Combat::Swing` (PLAYER_SWING, Enum.PlayerSwingType) and
// `Combat::SwingRange` (C_SwingTimer, PLAYER_SWING_RANGE_UPDATE).

namespace Combat::Swing {

// `Enum.PlayerSwingType` values, as in Blizzard's SwingTimerDocumentation.
enum SwingType : int { MAIN_HAND = 0, OFF_HAND = 1, RANGED = 2 };

// The player's current (hasted) swing time for `type`, in ms, from the
// player's own descriptor. 0 before the player is in the world.
uint32_t AttackTimeMs(SwingType type);

// Whether `type` applies to the player right now. Main hand always does
// (unarmed attacks swing too). Off hand needs a weapon in the off-hand slot
// and the ability to dual wield; ranged needs a ranged weapon (not a relic).
// The descriptor's attack-time fields can't answer this: the server fills
// them for every hand whether or not a weapon is equipped.
bool HasWeapon(SwingType type);

// The server's melee reach test between two units (CGUnit_C pointers), as in
// Unit::IsWithinMeleeRange. False if either is null.
bool InMeleeRange(void *player, void *target);

} // namespace Combat::Swing
