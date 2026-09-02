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

// `C_PlayerInfo.CanUseItem(itemInfo) -> canUse` — true iff the local player meets
// the item's use/equip requirements (the "is the item red in the tooltip" gate).
// Distinct from `IsUsableItem` (is the on-use ability castable right now). All of
// these must pass:
//   1. Proficiency  — weapon/armor subclass the player is trained in (the
//      plate-on-a-Mage gate; via VAR_PROFICIENCY_TABLE).
//   2. RequiredLevel <= player level.
//   3. AllowableClass / AllowableRace masks include the player.
//   4. RequiredSkill — the player has the item's skill line at rank >= required
//      (a mount "Requires Riding (150)").
//   5. RequiredSpell — the player knows the item's prerequisite spell (a crafting
//      specialization like "Requires Armorsmith").
//   6. RequiredReputation — the player's standing with the item's faction reaches
//      the required reaction band.
// (WotLK dropped the old PvP-rank item gates, so RequiredHonorRank/CityRank are
// unset on live items and aren't checked.)
//
// Cache-miss handling matches the C_Item peeks: a synchronous false without
// firing a load — the item requirement data lives in the item-stats cache.

#include "Game.h"
#include "Offsets.h"
#include "item/Arg.h"
#include "unit/Player.h"

#include <cstdint>

namespace Player::CanUseItem {

namespace {

using GetItemRecord_t = const uint8_t *(__thiscall *)(void *cache, uint32_t itemID,
                                                      const uint64_t *guid, void *callback,
                                                      void *userData, int unused);
using SkillLineToSlot_t = int(__thiscall *)(void *player, uint32_t skillLineID);
using SkillRankBySlot_t = int(__thiscall *)(void *player, int slot);
using ReactionBand_t = int(__cdecl *)(int factionID);

// Item-stats cache peek — no query fired on a miss (callback = nullptr).
const uint8_t *PeekItemRecord(uint32_t itemID) {
    auto fn = reinterpret_cast<GetItemRecord_t>(Offsets::FUN_DBCACHE_ITEMSTATS_GET_RECORD);
    auto *cache = reinterpret_cast<void *>(Offsets::VAR_ITEMDB_CACHE);
    const uint64_t zeroGuid = 0;
    return fn(cache, itemID, &zeroGuid, nullptr, nullptr, 0);
}

uint32_t U32(const uint8_t *rec, int off) {
    return *reinterpret_cast<const uint32_t *>(rec + off);
}

// A class/race allow-mask restricts a 1-based index the player isn't part of;
// 0 and 0xFFFFFFFF both mean "no restriction".
bool RestrictedOut(uint32_t mask, uint32_t index1Based) {
    if (mask == 0 || mask == 0xFFFFFFFF || index1Based == 0)
        return false;
    return (mask & (1u << ((index1Based - 1) & 31))) == 0;
}

// Weapon/armor proficiency: false only when the item's class has a proficiency
// concept (mask != 0) and the item's subclass bit is unset.
bool ProficiencyMet(const uint8_t *record) {
    const uint32_t itemClass = U32(record, Offsets::OFF_ITEMSTATS_CLASS);
    const uint32_t subClass = U32(record, Offsets::OFF_ITEMSTATS_SUBCLASS);
    if (itemClass > 16) // table is 17 entries (class 0..16)
        return true;
    auto *table = reinterpret_cast<const uint32_t *>(
        static_cast<uintptr_t>(Offsets::VAR_PROFICIENCY_TABLE));
    const uint32_t mask = table[itemClass];
    if (mask == 0)
        return true; // class has no proficiency concept (consumables, …)
    return (mask & (1u << (subClass & 31))) != 0;
}

bool RequiredSkillMet(void *player, const uint8_t *record) {
    const uint32_t reqSkill = U32(record, Offsets::OFF_ITEMSTATS_REQUIRED_SKILL);
    if (reqSkill == 0)
        return true;
    const uint32_t reqRank = U32(record, Offsets::OFF_ITEMSTATS_REQUIRED_SKILL_RANK);
    const int slot = reinterpret_cast<SkillLineToSlot_t>(
        Offsets::FUN_SKILL_LINE_TO_SLOT)(player, reqSkill);
    if (slot < 0)
        return false; // player doesn't have the skill line at all
    const int rank = reinterpret_cast<SkillRankBySlot_t>(
        Offsets::FUN_SKILL_RANK_BY_SLOT)(player, slot);
    return rank >= static_cast<int>(reqRank);
}

// True if the local player knows `spellID` — the engine's spell-knowledge bitmap
// (covers trained abilities, talents, racials, profession recipes, and crafting
// specializations, which are all single learned spells).
bool PlayerKnowsSpell(int spellID) {
    if (spellID <= 0)
        return false;
    const uint32_t maxSpellID =
        *reinterpret_cast<const uint32_t *>(Offsets::VAR_MAX_SPELL_ID);
    if (static_cast<uint32_t>(spellID) > maxSpellID)
        return false;
    auto *bitmap = *reinterpret_cast<const uint32_t *const *>(
        Offsets::VAR_PLAYER_SPELL_BITMAP);
    if (bitmap == nullptr)
        return false;
    return (bitmap[spellID >> 5] & (1u << (spellID & 31))) != 0;
}

bool RequiredSpellMet(const uint8_t *record) {
    const int reqSpell = static_cast<int>(U32(record, Offsets::OFF_ITEMSTATS_REQUIRED_SPELL));
    if (reqSpell <= 0)
        return true;
    return PlayerKnowsSpell(reqSpell);
}

// RequiredReputation — the player's current reaction band with the item's faction
// must reach the required band (0 = Hated … 7 = Exalted).
bool RequiredReputationMet(const uint8_t *record) {
    const int faction = static_cast<int>(U32(record, Offsets::OFF_ITEMSTATS_REQUIRED_FACTION));
    if (faction == 0)
        return true;
    const uint32_t reqBand = U32(record, Offsets::OFF_ITEMSTATS_REQUIRED_FACTION_RANK);
    const int playerBand = reinterpret_cast<ReactionBand_t>(
        Offsets::FUN_REPUTATION_GET_REACTION_BAND)(faction);
    return playerBand >= static_cast<int>(reqBand);
}

bool ComputeCanUse(int itemID) {
    if (itemID <= 0)
        return false;
    const uint8_t *record = PeekItemRecord(static_cast<uint32_t>(itemID));
    if (record == nullptr)
        return false; // not cached → sync false, no load fired
    void *player = Unit::LocalPlayer();
    if (player == nullptr)
        return false; // pre-world
    const uint8_t *desc = *reinterpret_cast<const uint8_t *const *>(
        static_cast<const uint8_t *>(player) + Offsets::OFF_UNIT_DESCRIPTOR);
    if (desc == nullptr)
        return false;

    if (!ProficiencyMet(record))
        return false;

    const int playerLevel = *reinterpret_cast<const int *>(
        desc + Offsets::OFF_UNIT_FIELD_LEVEL);
    if (static_cast<int>(U32(record, Offsets::OFF_ITEMSTATS_REQUIRED_LEVEL)) > playerLevel)
        return false;

    const uint32_t playerClass = *(desc + Offsets::OFF_UNIT_DESCRIPTOR_CLASS_BYTE);
    if (RestrictedOut(U32(record, Offsets::OFF_ITEMSTATS_ALLOWABLE_CLASS), playerClass))
        return false;

    const uint32_t playerRace = *(desc + Offsets::OFF_UNIT_DESCRIPTOR_RACE_BYTE);
    if (RestrictedOut(U32(record, Offsets::OFF_ITEMSTATS_ALLOWABLE_RACE), playerRace))
        return false;

    if (!RequiredSkillMet(player, record))
        return false;
    if (!RequiredSpellMet(record))
        return false;
    if (!RequiredReputationMet(record))
        return false;

    return true;
}

int __cdecl Script_CanUseItem(void *L) {
    const int itemID = Item::Arg::ResolveItemID(L, 1);
    Game::Lua::PushBool(L, ComputeCanUse(itemID));
    return 1;
}

void RegisterLuaFunctions() {
    Game::Lua::RegisterTableFunction("C_PlayerInfo", "CanUseItem", &Script_CanUseItem);
}

const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace

} // namespace Player::CanUseItem
