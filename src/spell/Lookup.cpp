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

#include "spell/Lookup.h"

#include "Offsets.h"

namespace Spell::Lookup {

namespace {

using DBCCopyRecord_t = int(__thiscall *)(void *desc, int id, void *outBuffer);
using DBCGetRecordPtr_t = uintptr_t(__thiscall *)(void *anchor, int id);

// Pointer-returning lookup into a `[min,max]`-bounded, pointer-anchored DBC
// (SpellIcon, SpellDispelType). Returns nullptr for out-of-range / null slots.
const uint8_t *ByAnchor(uintptr_t anchor, int id) {
    if (id <= 0)
        return nullptr;
    auto fn = reinterpret_cast<DBCGetRecordPtr_t>(
        static_cast<uintptr_t>(Offsets::FUN_DBC_GET_RECORD_PTR));
    return reinterpret_cast<const uint8_t *>(fn(reinterpret_cast<void *>(anchor), id));
}

} // namespace

bool CopyRecord(uint32_t spellID, uint8_t *out) {
    if (spellID == 0)
        return false;
    auto fn = reinterpret_cast<DBCCopyRecord_t>(
        static_cast<uintptr_t>(Offsets::FUN_DBC_COPY_RECORD));
    auto *desc = reinterpret_cast<void *>(
        static_cast<uintptr_t>(Offsets::VAR_SPELL_DBC_DESC));
    return fn(desc, static_cast<int>(spellID), out) != 0;
}

const char *IconPath(uint32_t iconID) {
    const uint8_t *record =
        ByAnchor(Offsets::VAR_SPELLICON_DBC_ANCHOR, static_cast<int>(iconID));
    if (record == nullptr)
        return nullptr;
    return *reinterpret_cast<const char *const *>(record + Offsets::OFF_SPELLICON_PATH);
}

const char *DispelTypeName(uint32_t dispelID) {
    if (dispelID == 0)
        return nullptr;
    const uint8_t *record =
        ByAnchor(Offsets::VAR_SPELLDISPEL_DBC_ANCHOR, static_cast<int>(dispelID));
    if (record == nullptr)
        return nullptr;
    if (*reinterpret_cast<const int *>(record + Offsets::OFF_SPELLDISPEL_HAS_NAME) == 0)
        return nullptr;
    return *reinterpret_cast<const char *const *>(record + Offsets::OFF_SPELLDISPEL_NAME);
}

int CastTimeMs(uint32_t castTimeIndex) {
    const uint8_t *record =
        ByAnchor(Offsets::VAR_SPELLCASTTIMES_DBC_ANCHOR, static_cast<int>(castTimeIndex));
    if (record == nullptr)
        return 0;
    return *reinterpret_cast<const int *>(record + Offsets::OFF_SPELLCASTTIMES_BASE_MS);
}

bool Range(uint32_t rangeIndex, float *minRange, float *maxRange) {
    *minRange = 0.0f;
    *maxRange = 0.0f;
    const uint8_t *record =
        ByAnchor(Offsets::VAR_SPELLRANGE_DBC_ANCHOR, static_cast<int>(rangeIndex));
    if (record == nullptr)
        return false;
    *minRange = *reinterpret_cast<const float *>(record + Offsets::OFF_SPELLRANGE_MIN);
    *maxRange = *reinterpret_cast<const float *>(record + Offsets::OFF_SPELLRANGE_MAX);
    return true;
}

const char *NameForSpell(uint32_t spellID) {
    uint8_t buf[Offsets::SPELL_DBC_RECORD_SIZE];
    if (!CopyRecord(spellID, buf))
        return nullptr;
    return *reinterpret_cast<const char *const *>(buf + Offsets::OFF_SPELL_NAME);
}

const char *IconPathForSpell(uint32_t spellID) {
    uint8_t buf[Offsets::SPELL_DBC_RECORD_SIZE];
    if (!CopyRecord(spellID, buf))
        return nullptr;
    const uint32_t iconID =
        *reinterpret_cast<const uint32_t *>(buf + Offsets::OFF_SPELL_ICON_DBC_ID);
    return IconPath(iconID);
}

} // namespace Spell::Lookup
