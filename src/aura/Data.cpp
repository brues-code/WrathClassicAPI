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

// Aura-table primitives. Contract in `Data.h`; Lua-side binding in `Api.cpp`.

#include "Data.h"

#include "Game.h"
#include "Offsets.h"

#include <cstdint>
#include <cstring>

namespace Aura::Data {

namespace {

// `_DAT_009e56b0` in the engine = 0.001 (ms → seconds). Hardcoded
// here rather than dereferenced because the engine never overwrites
// the value at runtime — it's a constant table entry the compiler
// emitted for the `* 0.001` multiplications throughout the aura path.
constexpr double MS_TO_SEC = 0.001;

// Layout of the engine's stack-allocated record buffer that
// FUN_DBC_COPY_RECORD writes into. The record fields we read are at
// well-known offsets within this buffer (see `Offsets.h`); the size
// matches the engine's hardcoded `0x2A8` byte copy (and the locale-
// aware path's matching 0x2A8 destination length).
struct SpellRecordBuffer {
    uint8_t bytes[Offsets::SPELL_DBC_RECORD_SIZE];
};

using DBCCopyRecord_t = int(__thiscall *)(void *desc, int id, void *outBuffer);
using DBCGetRecordPtr_t = uintptr_t(__thiscall *)(void *anchor, int id);
using GuidToToken_t = const char *(__cdecl *)(const uint32_t *guidPair);

// Resolve caster-GUID-pair → unit token via the engine helper. The
// engine writes party/raid/arena formatted tokens into a shared
// static buffer at 0x00C25B7C and returns its address — the result
// is only safe to read until the next call into the engine, so
// callers push immediately to Lua (which copies).
const char *ResolveCasterToken(const uint8_t *auraEntry) {
    auto fn = reinterpret_cast<GuidToToken_t>(
        static_cast<uintptr_t>(Offsets::FUN_AURA_GUID_TO_TOKEN));
    return fn(reinterpret_cast<const uint32_t *>(auraEntry + Offsets::OFF_AURA_CASTER_GUID_LO));
}

// Copy the Spell.dbc record for `spellID` into `buffer`. Returns
// true on hit. We always go through the record-copy helper rather
// than reading the raw record array directly: the engine has two
// "shapes" for the record (locale-aware split-string layout vs
// pre-resolved single-string layout) and FUN_DBC_COPY_RECORD
// normalizes both into the buffer-layout our offsets target.
bool CopySpellRecord(uint32_t spellID, SpellRecordBuffer *buffer) {
    if (spellID == 0)
        return false;
    auto desc = reinterpret_cast<void *>(
        static_cast<uintptr_t>(Offsets::VAR_SPELL_DBC_DESC));
    auto fn = reinterpret_cast<DBCCopyRecord_t>(
        static_cast<uintptr_t>(Offsets::FUN_DBC_COPY_RECORD));
    return fn(desc, static_cast<int>(spellID), buffer) != 0;
}

// Look up a record pointer in a pointer-anchored DBC (SpellIcon,
// SpellDispelType). Returns nullptr on out-of-range or null slot.
const uint8_t *DBCGetByAnchor(uintptr_t anchor, int id) {
    if (id <= 0)
        return nullptr;
    auto fn = reinterpret_cast<DBCGetRecordPtr_t>(
        static_cast<uintptr_t>(Offsets::FUN_DBC_GET_RECORD_PTR));
    return reinterpret_cast<const uint8_t *>(
        fn(reinterpret_cast<void *>(anchor), id));
}

const char *SpellIconPath(uint32_t iconID) {
    const uint8_t *record = DBCGetByAnchor(Offsets::VAR_SPELLICON_DBC_ANCHOR,
                                           static_cast<int>(iconID));
    if (record == nullptr)
        return nullptr;
    return *reinterpret_cast<const char *const *>(
        record + Offsets::OFF_SPELLICON_PATH);
}

const char *DispelTypeName(uint32_t dispelID) {
    if (dispelID == 0)
        return nullptr;
    const uint8_t *record = DBCGetByAnchor(Offsets::VAR_SPELLDISPEL_DBC_ANCHOR,
                                           static_cast<int>(dispelID));
    if (record == nullptr)
        return nullptr;
    if (*reinterpret_cast<const int *>(
            record + Offsets::OFF_SPELLDISPEL_HAS_NAME) == 0)
        return nullptr;
    return *reinterpret_cast<const char *const *>(
        record + Offsets::OFF_SPELLDISPEL_NAME);
}

// Convert engine's `expiration_ms` field (u32) to a double in
// seconds. The engine handles the high-bit-set case as the
// equivalent u32→double promotion (`+ 2^32` if negative) followed
// by `* 0.001`; we use static_cast<uint32_t>→double which is the
// same thing.
double MsToSeconds(uint32_t ms) {
    return static_cast<double>(ms) * MS_TO_SEC;
}

uint32_t ReadSpellIDFromEntry(const uint8_t *entry) {
    return *reinterpret_cast<const uint32_t *>(entry + Offsets::OFF_AURA_SPELL_ID);
}

uint8_t ReadFlagsByteFromEntry(const uint8_t *entry) {
    return *(entry + Offsets::OFF_AURA_FLAGS);
}

uint8_t ReadStacksFromEntry(const uint8_t *entry) {
    return *(entry + Offsets::OFF_AURA_STACKS);
}

uint32_t ReadDurationMs(const uint8_t *entry) {
    return *reinterpret_cast<const uint32_t *>(entry + Offsets::OFF_AURA_DURATION_MS);
}

uint32_t ReadExpirationMs(const uint8_t *entry) {
    return *reinterpret_cast<const uint32_t *>(entry + Offsets::OFF_AURA_EXPIRATION_MS);
}

bool MatchesFilter(const uint8_t *entry, Filter filter) {
    const bool harmful = (ReadFlagsByteFromEntry(entry) & Offsets::AURA_FLAG_HARMFUL) != 0;
    return (filter == Filter::Helpful) ? !harmful : harmful;
}

bool IsLiveSlot(const uint8_t *entry) {
    if (ReadSpellIDFromEntry(entry) == 0)
        return false;
    return (ReadFlagsByteFromEntry(entry) & Offsets::AURA_FLAG_EFF_INDEX_MASK) != 0;
}

} // namespace

int SlotCount(const uint8_t *unit) {
    if (unit == nullptr)
        return 0;
    const uint32_t inlineCount = *reinterpret_cast<const uint32_t *>(
        unit + Offsets::OFF_CGUNIT_AURA_INLINE_COUNT);
    if (inlineCount != 0xFFFFFFFFu)
        return static_cast<int>(inlineCount);
    return *reinterpret_cast<const int *>(
        unit + Offsets::OFF_CGUNIT_AURA_OVERFLOW_COUNT);
}

const uint8_t *EntryAt(const uint8_t *unit, int slot) {
    if (unit == nullptr || slot < 0)
        return nullptr;
    const uint32_t inlineCount = *reinterpret_cast<const uint32_t *>(
        unit + Offsets::OFF_CGUNIT_AURA_INLINE_COUNT);
    const uint8_t *base;
    int count;
    if (inlineCount != 0xFFFFFFFFu) {
        base = unit + Offsets::OFF_CGUNIT_AURA_INLINE_BASE;
        count = static_cast<int>(inlineCount);
    } else {
        base = *reinterpret_cast<const uint8_t *const *>(
            unit + Offsets::OFF_CGUNIT_AURA_OVERFLOW_BASE_PTR);
        count = *reinterpret_cast<const int *>(
            unit + Offsets::OFF_CGUNIT_AURA_OVERFLOW_COUNT);
    }
    if (base == nullptr || slot >= count)
        return nullptr;
    return base + slot * Offsets::AURA_ENTRY_STRIDE;
}

bool IsSlotPopulated(const uint8_t *unit, int slot) {
    const uint8_t *entry = EntryAt(unit, slot);
    if (entry == nullptr)
        return false;
    return IsLiveSlot(entry);
}

bool IsHelpful(const uint8_t *entry) {
    if (entry == nullptr)
        return false;
    return (ReadFlagsByteFromEntry(entry) & Offsets::AURA_FLAG_HARMFUL) == 0;
}

int FindNthSlot(const uint8_t *unit, int oneBasedIndex, Filter filter) {
    if (unit == nullptr || oneBasedIndex < 1)
        return -1;
    const int total = SlotCount(unit);
    int matches = 0;
    for (int slot = 0; slot < total; ++slot) {
        const uint8_t *entry = EntryAt(unit, slot);
        if (entry == nullptr || !IsLiveSlot(entry))
            continue;
        if (!MatchesFilter(entry, filter))
            continue;
        if (++matches == oneBasedIndex)
            return slot;
    }
    return -1;
}

int FindSlotBySpellID(const uint8_t *unit, uint32_t spellID,
                      const Filter *filter) {
    if (unit == nullptr || spellID == 0)
        return -1;
    const int total = SlotCount(unit);
    for (int slot = 0; slot < total; ++slot) {
        const uint8_t *entry = EntryAt(unit, slot);
        if (entry == nullptr || !IsLiveSlot(entry) ||
            ReadSpellIDFromEntry(entry) != spellID)
            continue;
        if (filter != nullptr && !MatchesFilter(entry, *filter))
            continue;
        return slot;
    }
    return -1;
}

void Push(void *L, const uint8_t *unit, int slot) {
    const uint8_t *entry = EntryAt(unit, slot);
    const uint32_t spellID = (entry != nullptr) ? ReadSpellIDFromEntry(entry) : 0;
    const bool helpful = IsHelpful(entry);

    SpellRecordBuffer record{};
    const bool haveSpell = CopySpellRecord(spellID, &record);

    const char *name = nullptr;
    const char *icon = nullptr;
    const char *dispel = nullptr;
    if (haveSpell) {
        name = *reinterpret_cast<const char *const *>(
            record.bytes + Offsets::OFF_SPELL_NAME);
        const uint32_t iconID = *reinterpret_cast<const uint32_t *>(
            record.bytes + Offsets::OFF_SPELL_ICON_DBC_ID);
        icon = SpellIconPath(iconID);
        const uint32_t dispelID = *reinterpret_cast<const uint32_t *>(
            record.bytes + Offsets::OFF_SPELL_DISPEL_TYPE_ID);
        dispel = DispelTypeName(dispelID);
    }

    const int applications = (entry != nullptr) ? ReadStacksFromEntry(entry) : 0;
    const double duration = (entry != nullptr)
        ? MsToSeconds(ReadDurationMs(entry)) : 0.0;
    const double expiration = (entry != nullptr)
        ? MsToSeconds(ReadExpirationMs(entry)) : 0.0;
    const char *source = (entry != nullptr) ? ResolveCasterToken(entry) : nullptr;

    const bool fromPlayerOrPet = source != nullptr &&
        (std::strcmp(source, "player") == 0 || std::strcmp(source, "pet") == 0);

    Game::Lua::NewTable(L);

    Game::Lua::SetFieldString(L, "name", name);
    Game::Lua::SetFieldString(L, "icon", icon);
    Game::Lua::SetFieldNumber(L, "applications", static_cast<double>(applications));
    Game::Lua::SetFieldNumber(L, "spellId", static_cast<double>(spellID));
    Game::Lua::SetFieldString(L, "dispelName", dispel);
    Game::Lua::SetFieldBool(L, "isHelpful", helpful);
    Game::Lua::SetFieldBool(L, "isHarmful", !helpful);
    Game::Lua::SetFieldNumber(L, "duration", duration);
    Game::Lua::SetFieldNumber(L, "expirationTime", expiration);
    Game::Lua::SetFieldString(L, "sourceUnit", source);
    Game::Lua::SetFieldBool(L, "isFromPlayerOrPlayerPet", fromPlayerOrPet);
    Game::Lua::SetFieldNumber(L, "timeMod", 1.0);

    // Vanilla-truthful zeros / falses for fields whose underlying
    // engine systems don't exist in 3.3.5. Modern returns sensible
    // defaults for these in legacy content too, so addons that read
    // them won't crash even when no real data is available.
    Game::Lua::SetFieldNumber(L, "charges", 0);
    Game::Lua::SetFieldNumber(L, "maxCharges", 0);
    Game::Lua::SetFieldBool(L, "isStealable", false);
    Game::Lua::SetFieldBool(L, "isBossAura", false);
    Game::Lua::SetFieldBool(L, "isNameplateOnly", false);
    Game::Lua::SetFieldBool(L, "nameplateShowAll", false);
    Game::Lua::SetFieldBool(L, "nameplateShowPersonal", false);
    Game::Lua::SetFieldBool(L, "canApplyAura", false);
    Game::Lua::SetFieldBool(L, "shouldConsolidate", false);
    Game::Lua::SetFieldBool(L, "isRaid", false);

    // `auraInstanceID`, `points` deliberately omitted — modern
    // returns nil for those when they don't apply, and Lua reading
    // a missing key yields nil, so no explicit field needed.
}

} // namespace Aura::Data
