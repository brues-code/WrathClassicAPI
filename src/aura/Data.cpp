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

// Aura-table primitives. Contract in `Data.h`; Lua-side binding in `Api.cpp`.

#include "Data.h"

#include "Game.h"
#include "Offsets.h"
#include "spell/Lookup.h"
#include "unit/Resolve.h"

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

using IsAuraStealable_t = bool(__cdecl *)(const uint8_t *targetUnit,
                                          const uint8_t *auraEntry,
                                          const uint8_t *spellRecord);

// Resolve an aura's caster to a unit token. The caster GUID pair sits at the
// start of the entry (OFF_AURA_CASTER_GUID_LO); Unit::GuidToToken wraps the
// engine resolver, whose party/raid/arena result lives in a shared buffer valid
// only until the next engine call — so callers push it to Lua immediately.
const char *ResolveCasterToken(const uint8_t *auraEntry) {
    return Unit::GuidToToken(
        reinterpret_cast<const uint32_t *>(auraEntry + Offsets::OFF_AURA_CASTER_GUID_LO));
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

// True unless `spellID`'s Spell.dbc record marks it a hidden/system aura —
// see the `SPELL_ATTR_HIDDEN_CLIENTSIDE` / `SPELL_ATTR_EX_NO_AURA_ICON` /
// `SPELL_AURA_TRACK_*` / `SPELL_AURA_MOD_STALKED` comments in Offsets.h.
// Fails closed (hidden) for a spellID with no record, matching the engine's
// own convention for an unresolvable aura.
bool IsVisible(uint32_t spellID) {
    SpellRecordBuffer record{};
    if (!::Spell::Lookup::CopyRecord(spellID, record.bytes))
        return false;

    const uint32_t attributes = *reinterpret_cast<const uint32_t *>(
        record.bytes + Offsets::OFF_SPELL_RECORD_ATTRIBUTES);
    if ((attributes & Offsets::SPELL_ATTR_HIDDEN_CLIENTSIDE) != 0)
        return false;

    const uint32_t attributesEx = *reinterpret_cast<const uint32_t *>(
        record.bytes + Offsets::OFF_SPELL_RECORD_ATTRIBUTES_EX);
    if ((attributesEx & Offsets::SPELL_ATTR_EX_NO_AURA_ICON) != 0)
        return false;

    // Hidden when every populated effect is a tracking/stalked aura — a
    // spell that combines one with a real effect (e.g. a damage component)
    // still counts as visible.
    bool sawEffect = false;
    bool sawNonTrackingEffect = false;
    for (int i = 0; i < 3; ++i) {
        const int32_t effect = *reinterpret_cast<const int32_t *>(
            record.bytes + Offsets::OFF_SPELL_EFFECT + i * 4);
        if (effect == 0)
            continue; // unused effect slot
        sawEffect = true;
        const int32_t auraName = *reinterpret_cast<const int32_t *>(
            record.bytes + Offsets::OFF_SPELL_EFFECT_APPLY_AURA_NAME + i * 4);
        if (auraName != Offsets::SPELL_AURA_TRACK_CREATURES &&
            auraName != Offsets::SPELL_AURA_TRACK_RESOURCES &&
            auraName != Offsets::SPELL_AURA_MOD_STALKED) {
            sawNonTrackingEffect = true;
        }
    }
    return !sawEffect || sawNonTrackingEffect;
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

bool IsVisibleSlot(const uint8_t *unit, int slot) {
    const uint8_t *entry = EntryAt(unit, slot);
    if (entry == nullptr)
        return true;
    return IsVisible(ReadSpellIDFromEntry(entry));
}

int FindNthSlot(const uint8_t *unit, int oneBasedIndex, Filter filter,
               bool includeHidden) {
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
        if (!includeHidden && !IsVisible(ReadSpellIDFromEntry(entry)))
            continue;
        if (++matches == oneBasedIndex)
            return slot;
    }
    return -1;
}

int NextSlot(const uint8_t *unit, int fromSlot, Filter filter, bool includeHidden) {
    if (unit == nullptr)
        return -1;
    const int total = SlotCount(unit);
    for (int slot = (fromSlot > 0) ? fromSlot : 0; slot < total; ++slot) {
        const uint8_t *entry = EntryAt(unit, slot);
        if (entry != nullptr && IsLiveSlot(entry) && MatchesFilter(entry, filter) &&
            (includeHidden || IsVisible(ReadSpellIDFromEntry(entry))))
            return slot;
    }
    return -1;
}

int FindSlotBySpellID(const uint8_t *unit, uint32_t spellID,
                      const Filter *filter, bool includeHidden) {
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
        if (!includeHidden && !IsVisible(spellID))
            continue;
        return slot;
    }
    return -1;
}

namespace {

// Everything the two emitters need, resolved once from the aura entry and its
// Spell.dbc record. The string fields point at DBC storage (`name`, `icon`,
// `dispel`) or the engine's shared token buffer (`source`) — both stay valid
// across the Lua pushes that follow, which call no engine resolver.
struct Resolved {
    uint32_t spellID = 0;
    const char *name = nullptr;
    const char *icon = nullptr;
    const char *dispel = nullptr;
    const char *source = nullptr;
    int applications = 0;
    double duration = 0.0;
    double expiration = 0.0;
    bool helpful = false;
    bool fromPlayerOrPet = false;
    bool stealable = false;
};

Resolved ResolveAura(const uint8_t *unit, int slot) {
    const uint8_t *entry = EntryAt(unit, slot);
    const uint32_t spellID = (entry != nullptr) ? ReadSpellIDFromEntry(entry) : 0;
    const bool helpful = IsHelpful(entry);

    SpellRecordBuffer record{};
    const bool haveSpell = ::Spell::Lookup::CopyRecord(spellID, record.bytes);

    const char *name = nullptr;
    const char *icon = nullptr;
    const char *dispel = nullptr;
    if (haveSpell) {
        name = *reinterpret_cast<const char *const *>(
            record.bytes + Offsets::OFF_SPELL_NAME);
        const uint32_t iconID = *reinterpret_cast<const uint32_t *>(
            record.bytes + Offsets::OFF_SPELL_ICON_DBC_ID);
        icon = ::Spell::Lookup::IconPath(iconID);
        const uint32_t dispelID = *reinterpret_cast<const uint32_t *>(
            record.bytes + Offsets::OFF_SPELL_DISPEL_TYPE_ID);
        dispel = ::Spell::Lookup::DispelTypeName(dispelID);
    }

    const int applications = (entry != nullptr) ? ReadStacksFromEntry(entry) : 0;
    const double duration = (entry != nullptr)
        ? MsToSeconds(ReadDurationMs(entry)) : 0.0;
    const double expiration = (entry != nullptr)
        ? MsToSeconds(ReadExpirationMs(entry)) : 0.0;
    const char *source = (entry != nullptr) ? ResolveCasterToken(entry) : nullptr;

    const bool fromPlayerOrPet = source != nullptr &&
        (std::strcmp(source, "player") == 0 || std::strcmp(source, "pet") == 0);

    // Stealable predicate. Gated on (a) Spell.dbc record present —
    // engine helper indexes into it — and (b) the aura being on a
    // unit other than self. The engine helper short-circuits to
    // false when DAT_00BE5D68 (the player's stealable-dispel mask)
    // is zero, so non-mages always read false here.
    bool stealable = false;
    if (entry != nullptr && haveSpell && helpful) {
        auto fn = reinterpret_cast<IsAuraStealable_t>(
            static_cast<uintptr_t>(Offsets::FUN_AURA_IS_STEALABLE));
        stealable = fn(unit, entry, record.bytes);
    }

    Resolved out;
    out.spellID = spellID;
    out.name = name;
    out.icon = icon;
    out.dispel = dispel;
    out.source = source;
    out.applications = applications;
    out.duration = duration;
    out.expiration = expiration;
    out.helpful = helpful;
    out.fromPlayerOrPet = fromPlayerOrPet;
    out.stealable = stealable;
    return out;
}

// `Emit::Table` leaf — the modern `AuraData` table. Net stack effect: +1.
void BuildTable(void *L, const Resolved &a) {
    Game::Lua::NewTable(L);

    Game::Lua::SetFieldString(L, "name", a.name);
    Game::Lua::SetFieldString(L, "icon", a.icon);
    Game::Lua::SetFieldNumber(L, "applications", static_cast<double>(a.applications));
    Game::Lua::SetFieldNumber(L, "spellId", static_cast<double>(a.spellID));
    Game::Lua::SetFieldString(L, "dispelName", a.dispel);
    Game::Lua::SetFieldBool(L, "isHelpful", a.helpful);
    Game::Lua::SetFieldBool(L, "isHarmful", !a.helpful);
    Game::Lua::SetFieldNumber(L, "duration", a.duration);
    Game::Lua::SetFieldNumber(L, "expirationTime", a.expiration);
    Game::Lua::SetFieldString(L, "sourceUnit", a.source);
    Game::Lua::SetFieldBool(L, "isFromPlayerOrPlayerPet", a.fromPlayerOrPet);
    Game::Lua::SetFieldBool(L, "isStealable", a.stealable);
    Game::Lua::SetFieldNumber(L, "timeMod", 1.0);

    // `points` — modern's per-effect value list. 3.3.5 doesn't keep per-aura
    // effect amounts on the client (they're baked into stats at apply time), so
    // it's always an empty table, matching what modern returns for an aura that
    // exposes no points.
    Game::Lua::PushString(L, "points");
    Game::Lua::NewTable(L);
    Game::Lua::SetTable(L, -3);

    // Modern-shape fields with no 3.3.5 source — emitted with the same default
    // the modern AuraData carries so consumers reading them get a sensible value
    // rather than nil.
    Game::Lua::SetFieldBool(L, "isBossAura", false);
    Game::Lua::SetFieldBool(L, "isNameplateOnly", false);
    Game::Lua::SetFieldBool(L, "nameplateShowAll", false);
    Game::Lua::SetFieldBool(L, "nameplateShowPersonal", false);
    Game::Lua::SetFieldBool(L, "hideOnPartyFrames", false);
    Game::Lua::SetFieldBool(L, "canApplyAura", false);
    Game::Lua::SetFieldBool(L, "canActivePlayerDispel", false);
    Game::Lua::SetFieldBool(L, "isRaid", false);
    Game::Lua::SetFieldBool(L, "isTankRoleAura", false);
    Game::Lua::SetFieldBool(L, "isHealerRoleAura", false);
    Game::Lua::SetFieldBool(L, "isDPSRoleAura", false);

    // `auraInstanceID` omitted (nil): 3.3.5 has no per-application instance-ID
    // system, and the companion APIs (GetAuraDataByAuraInstanceID, the UNIT_AURA
    // instance payloads) don't exist here — a synthesized id would only mislead
    // callers that key on it.
}

// `Emit::Positional` leaf — the `UnitAura` value tuple, no table allocated.
// Net stack effect: +POSITIONAL_COUNT. Values mirror `BuildTable` exactly so
// the two shapes never disagree, absent strings included: those push nil here
// just as they leave the table's field nil.
void PushPositional(void *L, const Resolved &a) {
    Game::Lua::PushString(L, a.name);                             // 1  name
    Game::Lua::PushString(L, a.icon);                             // 2  icon
    Game::Lua::PushNumber(L, static_cast<double>(a.applications)); // 3  count
    Game::Lua::PushString(L, a.dispel);                           // 4  dispelType
    Game::Lua::PushNumber(L, a.duration);                         // 5  duration
    Game::Lua::PushNumber(L, a.expiration);                       // 6  expirationTime
    Game::Lua::PushString(L, a.source);                           // 7  sourceUnit
    Game::Lua::PushBool(L, a.stealable);                          // 8  isStealable
    Game::Lua::PushBool(L, false);                                // 9  nameplateShowPersonal
    Game::Lua::PushNumber(L, static_cast<double>(a.spellID));     // 10 spellId
    Game::Lua::PushBool(L, false);                                // 11 canApplyAura
    Game::Lua::PushBool(L, false);                                // 12 isBossDebuff
    Game::Lua::PushBool(L, a.fromPlayerOrPet);                    // 13 castByPlayer
    Game::Lua::PushBool(L, false);                                // 14 nameplateShowAll
    Game::Lua::PushNumber(L, 1.0);                                // 15 timeMod
}

} // namespace

void Push(void *L, const uint8_t *unit, int slot, Emit emit) {
    const Resolved a = ResolveAura(unit, slot);
    if (emit == Emit::Positional)
        PushPositional(L, a);
    else
        BuildTable(L, a);
}

bool PushBySlot(void *L, const uint8_t *unit, int slot, Emit emit) {
    if (!IsSlotPopulated(unit, slot))
        return false;
    Push(L, unit, slot, emit);
    return true;
}

} // namespace Aura::Data
