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

#include "Data.h"

#include "Game.h"
#include "Offsets.h"
#include "ui/ColorData.h"

#include <cstdint>
#include <cstring>

namespace Aura::Api {

namespace {

using ResolveUnitToken_t = void *(__cdecl *)(const char *token);

const uint8_t *ResolveUnit(const char *token) {
    if (token == nullptr)
        return nullptr;
    auto fn = reinterpret_cast<ResolveUnitToken_t>(
        static_cast<uintptr_t>(Offsets::FUN_RESOLVE_UNIT_TOKEN));
    return static_cast<const uint8_t *>(fn(token));
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
                    Data::Filter filter) {
    const int slot = Data::FindNthSlot(unit, oneBasedIndex, filter);
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
    return PushAuraByIndex(L, ResolveUnit(unitTok), index, ParseFilter(filterStr));
}

int __cdecl Script_GetBuffDataByIndex(void *L) {
    const char *unitTok = ArgUnit(L, 1);
    const int index = ArgInt(L, 2);
    if (unitTok == nullptr || index < 1) {
        Game::Lua::PushNil(L);
        return 1;
    }
    return PushAuraByIndex(L, ResolveUnit(unitTok), index, Data::Filter::Helpful);
}

int __cdecl Script_GetDebuffDataByIndex(void *L) {
    const char *unitTok = ArgUnit(L, 1);
    const int index = ArgInt(L, 2);
    if (unitTok == nullptr || index < 1) {
        Game::Lua::PushNil(L);
        return 1;
    }
    return PushAuraByIndex(L, ResolveUnit(unitTok), index, Data::Filter::Harmful);
}

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
    const int slot = Data::FindSlotBySpellID(unit, static_cast<uint32_t>(spellID), fp);
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
    const int slot = Data::FindSlotBySpellID(unit, static_cast<uint32_t>(spellID), nullptr);
    if (slot < 0) {
        Game::Lua::PushNil(L);
        return 1;
    }
    Data::Push(L, unit, slot);
    return 1;
}

// Walks every aura entry on `unit` once, pushing AuraData tables
// matching `filter` (or all if `filter==nullptr`) into the array
// at `outerIdx` starting at `nextKey`. Updates `nextKey` so a
// follow-up call (e.g. helpful then harmful) can append to the
// same outer table.
void AppendAuras(void *L, const uint8_t *unit, int outerIdx,
                 const Data::Filter *filter, int &nextKey) {
    const int total = Data::SlotCount(unit);
    for (int slot = 0; slot < total; ++slot) {
        if (!Data::IsSlotPopulated(unit, slot))
            continue;
        if (filter != nullptr) {
            const bool helpful = Data::IsHelpful(Data::EntryAt(unit, slot));
            if ((*filter == Data::Filter::Helpful) != helpful)
                continue;
        }
        Game::Lua::PushNumber(L, static_cast<double>(nextKey++));
        Data::Push(L, unit, slot);
        Game::Lua::RawSet(L, outerIdx);
    }
}

int __cdecl Script_GetUnitAuras(void *L) {
    const char *unitTok = ArgUnit(L, 1);
    const char *filterStr = ArgOptString(L, 2);
    const uint8_t *unit = ResolveUnit(unitTok);

    Game::Lua::SetTop(L, 0);
    Game::Lua::NewTable(L);
    if (unit == nullptr)
        return 1;

    int nextKey = 1;
    if (filterStr == nullptr) {
        AppendAuras(L, unit, 1, nullptr, nextKey);
    } else {
        const Data::Filter f = ParseFilter(filterStr);
        AppendAuras(L, unit, 1, &f, nextKey);
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
