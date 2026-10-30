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

// 4.3.4's CGIncomingResurrection: the resurrections being cast at each unit or
// corpse. Backs `UnitHasIncomingResurrection` and `INCOMING_RESURRECT_CHANGED`.
namespace Unit::IncomingResurrection {

// Net::SpellPacket observers. An SMSG_SPELL_START of a resurrect spell with an
// object target begins a resurrection; the SMSG_SPELL_GO or SMSG_SPELL_FAILURE
// of that cast ends it. `msg` is read from its start.
void OnSpellCastPacket(uint32_t msgId, void *msg);
void OnSpellFailurePacket(void *msg);

} // namespace Unit::IncomingResurrection
