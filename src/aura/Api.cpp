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

// `C_UnitAuras.*` — modern aura-data namespace. Returns AuraData
// tables built by `Aura::Data::Push` (see `Data.h`). 3.3.5's aura
// system already carries everything modern needs per-entry
// (caster GUID, duration, expiration, stacks) so we can match the
// modern AuraData surface much more faithfully than ClassicAPI's
// 1.12 port — `duration`, `expirationTime`, and `sourceUnit` are
// populated for ALL units, not just the local player.
//
// Filter parsing mirrors modern: `"HELPFUL"` (default) vs
// `"HARMFUL"`. The other modern filter tokens
// (`PLAYER` / `RAID` / `CANCELABLE` / `INCLUDE_NAME_PLATE_ONLY`)
// are accepted but no-ops — they'd require either source-side
// caster classification we don't expose or modern-only systems
// (nameplate-only auras) that don't exist in 3.3.5.
//
// `HIDDEN` — a WrathClassicAPI extension, not a modern token. The aura array
// holds entries the client's own buff frame never shows an icon for
// (secondary-skill tracking like Find Herbs, stance/shapeshift-form auras
// like Defensive Stance — see `Aura::Data::IsVisibleSlot`); every getter here
// excludes those by default to match, and `HIDDEN` in the filter string opts
// back into seeing them.

#include "Data.h"

#include "Game.h"
#include "Offsets.h"
#include "ui/ColorData.h"
#include "unit/Resolve.h"

#include <cstdint>
#include <cstring>

namespace Aura::Api {

namespace {

const uint8_t *ResolveUnit(const char *token) {
    return static_cast<const uint8_t *>(Unit::ResolveToken(token));
}

// Case-sensitive substring match for the modern filter tokens.
// Defaults to HELPFUL when the string is absent or doesn't mention
// HARMFUL — same convention modern uses.
Data::Filter ParseFilter(const char *filter) {
    if (filter == nullptr)
        return Data::Filter::Helpful;
    if (std::strstr(filter, "HARMFUL") != nullptr)
        return Data::Filter::Harmful;
    return Data::Filter::Helpful;
}

// `HIDDEN` — a WrathClassicAPI extension, not a modern token. By default the
// index/slot/bulk getters skip auras the client's own buff frame never shows
// an icon for (see `Aura::Data::IsVisibleSlot`); a filter string containing
// `HIDDEN` opts back into seeing them.
bool ParseIncludeHidden(const char *filter) {
    return filter != nullptr && std::strstr(filter, "HIDDEN") != nullptr;
}

const char *ArgUnit(void *L, int idx) {
    if (!Game::Lua::IsString(L, idx))
        return nullptr;
    return Game::Lua::ToString(L, idx);
}

int ArgInt(void *L, int idx) {
    if (!Game::Lua::IsNumber(L, idx))
        return 0;
    return static_cast<int>(Game::Lua::ToNumber(L, idx));
}

const char *ArgOptString(void *L, int idx) {
    if (!Game::Lua::IsString(L, idx))
        return nullptr;
    return Game::Lua::ToString(L, idx);
}

// Pushes the `oneBasedIndex`-th aura on `unit` matching `filter`,
// or nil if none. Shared by GetAuraDataByIndex /
// GetBuffDataByIndex / GetDebuffDataByIndex.
int PushAuraByIndex(void *L, const uint8_t *unit, int oneBasedIndex,
                    Data::Filter filter, bool includeHidden) {
    const int slot = Data::FindNthSlot(unit, oneBasedIndex, filter, includeHidden);
    if (slot < 0) {
        Game::Lua::PushNil(L);
        return 1;
    }
    Data::Push(L, unit, slot);
    return 1;
}

int __cdecl Script_GetAuraDataByIndex(void *L) {
    const char *unitTok = ArgUnit(L, 1);
    const int index = ArgInt(L, 2);
    const char *filterStr = ArgOptString(L, 3);
    if (unitTok == nullptr || index < 1) {
        Game::Lua::PushNil(L);
        return 1;
    }
    return PushAuraByIndex(L, ResolveUnit(unitTok), index, ParseFilter(filterStr),
                           ParseIncludeHidden(filterStr));
}

int __cdecl Script_GetBuffDataByIndex(void *L) {
    const char *unitTok = ArgUnit(L, 1);
    const int index = ArgInt(L, 2);
    const char *filterStr = ArgOptString(L, 3);
    if (unitTok == nullptr || index < 1) {
        Game::Lua::PushNil(L);
        return 1;
    }
    return PushAuraByIndex(L, ResolveUnit(unitTok), index, Data::Filter::Helpful,
                           ParseIncludeHidden(filterStr));
}

int __cdecl Script_GetDebuffDataByIndex(void *L) {
    const char *unitTok = ArgUnit(L, 1);
    const int index = ArgInt(L, 2);
    const char *filterStr = ArgOptString(L, 3);
    if (unitTok == nullptr || index < 1) {
        Game::Lua::PushNil(L);
        return 1;
    }
    return PushAuraByIndex(L, ResolveUnit(unitTok), index, Data::Filter::Harmful,
                           ParseIncludeHidden(filterStr));
}

// By-spellID lookups always find a hidden aura (tracking / stance / etc.)
// too — the caller already named the exact spell they want, so gating that
// on visibility would just make a correct call return nil.
int __cdecl Script_GetUnitAuraBySpellID(void *L) {
    const char *unitTok = ArgUnit(L, 1);
    const int spellID = ArgInt(L, 2);
    const char *filterStr = ArgOptString(L, 3);
    if (unitTok == nullptr || spellID <= 0) {
        Game::Lua::PushNil(L);
        return 1;
    }
    const uint8_t *unit = ResolveUnit(unitTok);
    Data::Filter f;
    const Data::Filter *fp = nullptr;
    if (filterStr != nullptr) {
        f = ParseFilter(filterStr);
        fp = &f;
    }
    const int slot = Data::FindSlotBySpellID(unit, static_cast<uint32_t>(spellID), fp,
                                             /*includeHidden=*/true);
    if (slot < 0) {
        Game::Lua::PushNil(L);
        return 1;
    }
    Data::Push(L, unit, slot);
    return 1;
}

int __cdecl Script_GetPlayerAuraBySpellID(void *L) {
    const int spellID = ArgInt(L, 1);
    if (spellID <= 0) {
        Game::Lua::PushNil(L);
        return 1;
    }
    const uint8_t *unit = ResolveUnit("player");
    const int slot = Data::FindSlotBySpellID(unit, static_cast<uint32_t>(spellID), nullptr,
                                             /*includeHidden=*/true);
    if (slot < 0) {
        Game::Lua::PushNil(L);
        return 1;
    }
    Data::Push(L, unit, slot);
    return 1;
}

// `C_UnitAuras.GetAuraSlots(unit [, filter [, maxSlots [, continuationToken]]])`
//   -> continuationToken, slot1, slot2, ...
//
// Enumerates the slot ids of the auras on `unit` matching `filter`, in the
// order the by-index getters visit them, `maxSlots` at a time (nil / 0 = all).
// The first return is the token to pass back for the next batch, or nil when
// this batch reached the end — modern's batching contract, and what makes a
// full aura scan linear: one enumeration per batch plus a direct by-slot fetch
// per aura, instead of a fresh from-slot-0 walk per index.
//
// The token is the aura-array index to resume at, plus one; opaque to callers.
// Slot ids are the aura-array indices themselves, which is what lets
// `GetAuraDataBySlot` fetch one with no walk.
int __cdecl Script_GetAuraSlots(void *L) {
    const char *unitTok = ArgUnit(L, 1);
    const char *filterStr = ArgOptString(L, 2);
    const int maxSlots = ArgInt(L, 3);
    const int token = ArgInt(L, 4);

    const uint8_t *unit = ResolveUnit(unitTok);
    const Data::Filter filter = ParseFilter(filterStr);
    const bool includeHidden = ParseIncludeHidden(filterStr);
    const int first = (token > 0) ? token - 1 : 0;

    // Count this batch (and find where the next one resumes) before pushing
    // anything, so the stack is grown exactly once for a known count.
    int n = 0;
    int resume = -1;
    for (int slot = Data::NextSlot(unit, first, filter, includeHidden); slot >= 0;
         slot = Data::NextSlot(unit, slot + 1, filter, includeHidden)) {
        if (maxSlots > 0 && n == maxSlots) {
            resume = slot;
            break;
        }
        ++n;
    }

    // Everything below only pushes, so the args can go; a batch is n + 1
    // values, past the headroom a C function is guaranteed.
    Game::Lua::SetTop(L, 0);
    if (Game::Lua::CheckStack(L, n + 1) == 0) {
        Game::Lua::PushNil(L);
        return 1;
    }
    if (resume >= 0)
        Game::Lua::PushNumber(L, static_cast<double>(resume + 1));
    else
        Game::Lua::PushNil(L);
    int slot = Data::NextSlot(unit, first, filter, includeHidden);
    for (int i = 0; i < n; ++i, slot = Data::NextSlot(unit, slot + 1, filter, includeHidden))
        Game::Lua::PushNumber(L, static_cast<double>(slot));
    return n + 1;
}

// Shared body of GetAuraDataBySlot (table) / UnitAuraBySlot (positional):
// pushes the aura a slot id from GetAuraSlots names, or a single nil for an id
// that no longer names one.
int PushAuraBySlot(void *L, Data::Emit emit) {
    const char *unitTok = ArgUnit(L, 1);
    if (unitTok == nullptr || !Game::Lua::IsNumber(L, 2)) {
        Game::Lua::PushNil(L);
        return 1;
    }
    if (Data::PushBySlot(L, ResolveUnit(unitTok), ArgInt(L, 2), emit))
        return (emit == Data::Emit::Positional) ? Data::POSITIONAL_COUNT : 1;
    Game::Lua::PushNil(L);
    return 1;
}

// `C_UnitAuras.GetAuraDataBySlot(unit, slot)` -> AuraData | nil
int __cdecl Script_GetAuraDataBySlot(void *L) {
    return PushAuraBySlot(L, Data::Emit::Table);
}

// `C_UnitAuras.UnitAuraBySlot(unit, slot)` -> the 15 positional UnitAura
// values | nil. Allocation-free sibling of GetAuraDataBySlot for per-frame
// scans that don't want a table per aura.
int __cdecl Script_UnitAuraBySlot(void *L) {
    return PushAuraBySlot(L, Data::Emit::Positional);
}

// Walks every aura entry on `unit` once, pushing AuraData tables
// matching `filter` (or all if `filter==nullptr`) into the array
// at `outerIdx` starting at `nextKey`. Updates `nextKey` so a
// follow-up call (e.g. helpful then harmful) can append to the
// same outer table. Hidden auras (see `Data::IsVisibleSlot`) are
// skipped unless `includeHidden` is true.
void AppendAuras(void *L, const uint8_t *unit, int outerIdx,
                 const Data::Filter *filter, int &nextKey, bool includeHidden) {
    const int total = Data::SlotCount(unit);
    for (int slot = 0; slot < total; ++slot) {
        if (!Data::IsSlotPopulated(unit, slot))
            continue;
        if (filter != nullptr) {
            const bool helpful = Data::IsHelpful(Data::EntryAt(unit, slot));
            if ((*filter == Data::Filter::Helpful) != helpful)
                continue;
        }
        if (!includeHidden && !Data::IsVisibleSlot(unit, slot))
            continue;
        Game::Lua::PushNumber(L, static_cast<double>(nextKey++));
        Data::Push(L, unit, slot);
        Game::Lua::RawSet(L, outerIdx);
    }
}

int __cdecl Script_GetUnitAuras(void *L) {
    const char *unitTok = ArgUnit(L, 1);
    const char *filterStr = ArgOptString(L, 2);
    const uint8_t *unit = ResolveUnit(unitTok);
    const bool includeHidden = ParseIncludeHidden(filterStr);

    Game::Lua::SetTop(L, 0);
    Game::Lua::NewTable(L);
    if (unit == nullptr)
        return 1;

    int nextKey = 1;
    if (filterStr == nullptr) {
        AppendAuras(L, unit, 1, nullptr, nextKey, includeHidden);
    } else {
        const Data::Filter f = ParseFilter(filterStr);
        AppendAuras(L, unit, 1, &f, nextKey, includeHidden);
    }
    return 1;
}

// Pushes a plain `{r, g, b, a}` table decoded from a packed argb
// int. Used only on the fallback path where no addon-side helper
// has wrapped the entry as a `ColorMixin` global.
void PushPlainColorTable(void *L, int32_t argb) {
    const uint32_t v = static_cast<uint32_t>(argb);
    Game::Lua::NewTable(L);
    Game::Lua::SetFieldNumber(L, "r", ((v >> 16) & 0xFF) / 255.0);
    Game::Lua::SetFieldNumber(L, "g", ((v >>  8) & 0xFF) / 255.0);
    Game::Lua::SetFieldNumber(L, "b", ( v        & 0xFF) / 255.0);
    Game::Lua::SetFieldNumber(L, "a", ((v >> 24) & 0xFF) / 255.0);
}

bool PushColorByTag(void *L, const char *baseTag) {
    for (int i = 0; i < UI::ColorData::kColorCount; ++i) {
        if (std::strcmp(UI::ColorData::kColors[i].baseTag, baseTag) == 0) {
            PushPlainColorTable(L, UI::ColorData::kColors[i].argb);
            return true;
        }
    }
    return false;
}

// `C_UnitAuras.GetAuraDispelTypeColor(type)` — mirrors modern's:
//   return _G["DEBUFF_TYPE_"..type:upper().."_COLOR"]
//          or DEBUFF_TYPE_NONE_COLOR
//
// Primary path: read `_G[DEBUFF_TYPE_<TYPE>_COLOR]` — if some
// addon (or our future Color util) has already wrapped the
// constant as a ColorMixin and stashed it on the globals, we
// return that ColorMixin directly so it keeps `:GetRGB()` etc.
//
// Fallback path: build a plain `{r,g,b,a}` table from the embedded
// `UI::ColorData` snapshot. The Enrage row is a ClassicAPI
// extension already carried in `ColorData.h`.
int __cdecl Script_GetAuraDispelTypeColor(void *L) {
    const char *type = ArgOptString(L, 1);

    char tag[64];
    if (type != nullptr && type[0] != '\0') {
        std::memcpy(tag, "DEBUFF_TYPE_", 12);
        size_t off = 12;
        for (size_t i = 0; type[i] != '\0' && off < sizeof(tag) - 7; ++i, ++off) {
            const char c = type[i];
            tag[off] = (c >= 'a' && c <= 'z') ? c - 32 : c;
        }
        std::memcpy(tag + off, "_COLOR", 7); // includes the NUL
    } else {
        std::memcpy(tag, "DEBUFF_TYPE_NONE_COLOR", 23);
    }

    Game::Lua::SetTop(L, 0);
    Game::Lua::GetGlobal(L, tag);
    if (Game::Lua::Type(L, -1) == Game::Lua::TYPE_TABLE)
        return 1;

    Game::Lua::SetTop(L, 0);
    if (PushColorByTag(L, tag))
        return 1;
    PushColorByTag(L, "DEBUFF_TYPE_NONE_COLOR");
    return 1;
}

void RegisterLuaFunctions() {
    Game::Lua::RegisterTableFunction("C_UnitAuras", "GetAuraDataByIndex",
                                     &Script_GetAuraDataByIndex);
    Game::Lua::RegisterTableFunction("C_UnitAuras", "GetBuffDataByIndex",
                                     &Script_GetBuffDataByIndex);
    Game::Lua::RegisterTableFunction("C_UnitAuras", "GetDebuffDataByIndex",
                                     &Script_GetDebuffDataByIndex);
    Game::Lua::RegisterTableFunction("C_UnitAuras", "GetAuraSlots",
                                     &Script_GetAuraSlots);
    Game::Lua::RegisterTableFunction("C_UnitAuras", "GetAuraDataBySlot",
                                     &Script_GetAuraDataBySlot);
    Game::Lua::RegisterTableFunction("C_UnitAuras", "UnitAuraBySlot",
                                     &Script_UnitAuraBySlot);
    Game::Lua::RegisterTableFunction("C_UnitAuras", "GetUnitAuraBySpellID",
                                     &Script_GetUnitAuraBySpellID);
    Game::Lua::RegisterTableFunction("C_UnitAuras", "GetPlayerAuraBySpellID",
                                     &Script_GetPlayerAuraBySpellID);
    Game::Lua::RegisterTableFunction("C_UnitAuras", "GetUnitAuras",
                                     &Script_GetUnitAuras);
    Game::Lua::RegisterTableFunction("C_UnitAuras", "GetAuraDispelTypeColor",
                                     &Script_GetAuraDispelTypeColor);
}

const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace
} // namespace Aura::Api
