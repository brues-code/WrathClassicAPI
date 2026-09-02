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

// `C_Spell.GetSpellInfo(spellIdentifier) -> spellInfo` — a table describing a
// spell, or nil if the identifier resolves to no spell. `spellIdentifier` is a
// spell ID, name, name(subtext), or link (see Spell::Arg); a localized name
// resolves only when the spell is in your/pet's spellbook.
//
// Returned fields:
//   name           localized spell name
//   iconID         icon texture path (3.3.5 has no fileID system, so this is
//   originalIconID the "Interface\\Icons\\…" path string, ready for
//                  texture:SetTexture; both fields carry it — there is no
//                  spell-override system to distinguish them)
//   castTime       cast time in milliseconds (0 for instant spells)
//   minRange       minimum range in yards (0 if not applicable)
//   maxRange       maximum range in yards (0 if not applicable)
//   spellID        the resolved spell ID
//   rank           rank text ("Rank N"), or nil
//   powerType      power type the spell is cast from (0 = mana, 1 = rage,
//                  3 = energy, -2 = health, …)
//   isFunnel       true for health-funnel spells
//
// `rank`, `powerType`, and `isFunnel` are extras beyond the modern signature,
// carried from the same Spell.dbc record for parity with the sibling 1.12
// build's C_Spell.GetSpellInfo. (No `cost`: WotLK pays most caster spells as a
// percentage of the caster's base mana rather than the flat Spell.dbc field, so
// a static per-spell cost would read 0 or mislead.) Reads Spell.dbc plus its
// SpellIcon / SpellCastTimes / SpellRange sub-tables entirely from client data.

#include "Game.h"
#include "Offsets.h"
#include "spell/Arg.h"
#include "spell/Lookup.h"

#include <cstdint>
#include <cstdio>

namespace Spell::Info {

namespace {

struct SpellInfoData {
    int spellID;
    const char *name;
    const char *rank;
    const char *icon;
    int castTimeMs;
    float minRange;
    float maxRange;
    int powerType;
    bool isFunnel;
};

bool Read(int spellID, SpellInfoData &out) {
    if (spellID <= 0)
        return false;
    uint8_t buf[Offsets::SPELL_DBC_RECORD_SIZE];
    if (!Spell::Lookup::CopyRecord(static_cast<uint32_t>(spellID), buf))
        return false;

    out.spellID = spellID;
    out.name = *reinterpret_cast<const char *const *>(buf + Offsets::OFF_SPELL_NAME);
    out.rank = *reinterpret_cast<const char *const *>(buf + Offsets::OFF_SPELL_RANK);

    const uint32_t iconID =
        *reinterpret_cast<const uint32_t *>(buf + Offsets::OFF_SPELL_ICON_DBC_ID);
    out.icon = Spell::Lookup::IconPath(iconID);

    const uint32_t castIndex =
        *reinterpret_cast<const uint32_t *>(buf + Offsets::OFF_SPELL_CASTING_TIME_INDEX);
    out.castTimeMs = Spell::Lookup::CastTimeMs(castIndex);

    const uint32_t rangeIndex =
        *reinterpret_cast<const uint32_t *>(buf + Offsets::OFF_SPELL_RANGE_INDEX);
    Spell::Lookup::Range(rangeIndex, &out.minRange, &out.maxRange);

    out.powerType = *reinterpret_cast<const int *>(buf + Offsets::OFF_SPELL_POWER_TYPE);

    const uint32_t attrEx2 =
        *reinterpret_cast<const uint32_t *>(buf + Offsets::OFF_SPELL_ATTRIBUTES_EX2);
    out.isFunnel = (attrEx2 & Offsets::SPELL_ATTR_EX2_HEALTH_FUNNEL) != 0;
    return true;
}

int __cdecl Script_C_Spell_GetSpellInfo(void *L) {
    const int spellID = Spell::Arg::ResolveSpellID(L, 1);
    SpellInfoData info;
    if (!Read(spellID, info))
        return 0; // nil — unresolved identifier / unknown spell

    Game::Lua::NewTable(L);
    Game::Lua::SetFieldString(L, "name", info.name);
    // 3.3.5 has no fileID system, so both icon fields surface the icon PATH
    // string (feed straight to texture:SetTexture); there is no spell-override
    // system, so originalIconID equals iconID.
    Game::Lua::SetFieldString(L, "iconID", info.icon);
    Game::Lua::SetFieldString(L, "originalIconID", info.icon);
    Game::Lua::SetFieldNumber(L, "castTime", static_cast<double>(info.castTimeMs));
    Game::Lua::SetFieldNumber(L, "minRange", static_cast<double>(info.minRange));
    Game::Lua::SetFieldNumber(L, "maxRange", static_cast<double>(info.maxRange));
    Game::Lua::SetFieldNumber(L, "spellID", static_cast<double>(info.spellID));
    Game::Lua::SetFieldString(L, "rank", info.rank);
    Game::Lua::SetFieldNumber(L, "powerType", static_cast<double>(info.powerType));
    Game::Lua::SetFieldBool(L, "isFunnel", info.isFunnel);
    return 1;
}

// `C_Spell.GetSpellName(spellIdentifier) -> name` — the localized spell name, or
// nil if the identifier resolves to no spell. Same identifier forms as
// GetSpellInfo/GetSpellTexture.
int __cdecl Script_C_Spell_GetSpellName(void *L) {
    const int spellID = Spell::Arg::ResolveSpellID(L, 1);
    if (spellID <= 0)
        return 0;
    const char *name = Spell::Lookup::NameForSpell(static_cast<uint32_t>(spellID));
    if (name == nullptr || *name == '\0')
        return 0;
    Game::Lua::PushString(L, name);
    return 1;
}

// `C_Spell.GetSpellLink(spellIdentifier) -> link` — the spell hyperlink string
// "|cff71d5ff|Hspell:<id>|h[<name>]|h|r", or nil if the identifier resolves to no
// spell. Built directly (matching the engine's own link format) with the
// standard spell-link color; spellID and name come from the resolved spell.
int __cdecl Script_C_Spell_GetSpellLink(void *L) {
    const int spellID = Spell::Arg::ResolveSpellID(L, 1);
    if (spellID <= 0)
        return 0;
    const char *name = Spell::Lookup::NameForSpell(static_cast<uint32_t>(spellID));
    if (name == nullptr || *name == '\0')
        return 0;
    char link[256];
    std::snprintf(link, sizeof(link), "|cff71d5ff|Hspell:%d|h[%s]|h|r", spellID, name);
    Game::Lua::PushString(L, link);
    return 1;
}

void RegisterLuaFunctions() {
    Game::Lua::RegisterTableFunction("C_Spell", "GetSpellInfo",
                                     &Script_C_Spell_GetSpellInfo);
    Game::Lua::RegisterTableFunction("C_Spell", "GetSpellName",
                                     &Script_C_Spell_GetSpellName);
    Game::Lua::RegisterTableFunction("C_Spell", "GetSpellLink",
                                     &Script_C_Spell_GetSpellLink);
}

const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace

} // namespace Spell::Info
