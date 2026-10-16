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

// `C_SwingTimer.EnableRangeCheck` / `IsTargetWithinSwingRange` and
// `PLAYER_SWING_RANGE_UPDATE(swingType, isInRange, checksRange)`. Ported from
// ClassicAPI's src/combat/SwingRange.cpp.
//
// Melee uses Combat::Swing::InMeleeRange, the server's own reach test.
//
// Ranged asks the engine's IsSpellInRange core (Spell_C_RangeCheckSelected)
// with the player's ranged attack spell as the probe: the running autorepeat
// spell if there is one, else the spell for the equipped weapon — Shoot for a
// wand, Throw for a thrown weapon, Auto Shot (35 yd) for a bow, gun or
// crossbow if the player knows it, else Shoot (30 yd).
//
// On top of the geometry, "no weapon for this swing type", "no target" and
// "target can't be attacked" report no check (isInRange nil), and the
// per-frame poll becomes a change-driven event.

#include "combat/Swing.h"

#include "Game.h"
#include "Offsets.h"
#include "event/Custom.h"
#include "item/ID.h"
#include "item/Location.h"
#include "spell/IsPlayerSpell.h"
#include "spell/Lookup.h"
#include "tick/FrameTick.h"
#include "unit/Player.h"
#include "unit/Resolve.h"

#include <cstdint>

namespace Combat::SwingRange {

namespace {

using Combat::Swing::SwingType;

enum class RangeState { NoCheck, InRange, OutOfRange };

bool g_enabled[3] = {false, false, false};
RangeState g_lastState[3] = {RangeState::NoCheck, RangeState::NoCheck, RangeState::NoCheck};

constexpr int kAutoShotSpellID = 75;  // bow / gun / crossbow, hunters, 35 yd
constexpr int kShootSpellID = 3018;   // bow / gun / crossbow, others, 30 yd
constexpr int kWandShootSpellID = 5019;
constexpr int kThrowSpellID = 2764;
constexpr int kSlotRanged = 18;

using GetItemRecord_t = const uint8_t *(__thiscall *)(void *cache, uint32_t itemID,
                                                      const uint64_t *guid, void *callback,
                                                      void *userData, int unused);

// Item-stats cache peek — no query fired on a miss (callback = nullptr).
const uint8_t *PeekItemRecord(uint32_t itemID) {
    auto fn = reinterpret_cast<GetItemRecord_t>(Offsets::FUN_DBCACHE_ITEMSTATS_GET_RECORD);
    auto *cache = reinterpret_cast<void *>(Offsets::VAR_ITEMDB_CACHE);
    const uint64_t zeroGuid = 0;
    return fn(cache, itemID, &zeroGuid, nullptr, nullptr, 0);
}

int RangedProbeSpellID() {
    const auto active = *reinterpret_cast<const int32_t *>(
        static_cast<uintptr_t>(Offsets::VAR_AUTO_REPEATING_SPELL));
    if (active != 0)
        return active;

    const int itemID =
        Item::ID::FromCGItem(Item::Location::ResolveEquipmentSlot(kSlotRanged));
    const uint8_t *stats = itemID > 0 ? PeekItemRecord(static_cast<uint32_t>(itemID)) : nullptr;
    if (stats != nullptr) {
        const uint32_t subclass =
            *reinterpret_cast<const uint32_t *>(stats + Offsets::OFF_ITEMSTATS_SUBCLASS);
        if (subclass == Offsets::ITEM_SUBCLASS_WEAPON_WAND)
            return kWandShootSpellID;
        if (subclass == Offsets::ITEM_SUBCLASS_WEAPON_THROWN)
            return kThrowSpellID;
    }
    return Spell::IsPlayerSpell::PlayerKnowsSpell(kAutoShotSpellID) ? kAutoShotSpellID
                                                                     : kShootSpellID;
}

using CanAttack_t = bool(__thiscall *)(void *self, void *target);
using RangeCheck_t = bool(__cdecl *)(void *caster, const uint8_t *spell, uint32_t targetLo,
                                     uint32_t targetHi, uint8_t *inRange, bool report,
                                     void *cast);

RangeState Evaluate(SwingType type) {
    if (!Combat::Swing::HasWeapon(type))
        return RangeState::NoCheck;
    void *player = Unit::LocalPlayer();
    void *target = Unit::ResolveToken("target");
    if (player == nullptr || target == nullptr)
        return RangeState::NoCheck;
    if (!reinterpret_cast<CanAttack_t>(Offsets::FUN_UNIT_CAN_ATTACK)(player, target))
        return RangeState::NoCheck;

    if (type != Combat::Swing::RANGED)
        return Combat::Swing::InMeleeRange(player, target) ? RangeState::InRange
                                                            : RangeState::OutOfRange;

    uint8_t spell[Offsets::SPELL_DBC_RECORD_SIZE];
    const uint8_t *desc = Unit::Descriptor(target);
    if (desc == nullptr ||
        !Spell::Lookup::CopyRecord(static_cast<uint32_t>(RangedProbeSpellID()), spell))
        return RangeState::NoCheck;
    // OBJECT_FIELD_GUID opens the descriptor.
    const auto *guid = reinterpret_cast<const uint32_t *>(desc);
    uint8_t inRange = 0;
    if (!reinterpret_cast<RangeCheck_t>(Offsets::FUN_SPELL_RANGE_CHECK_SELECTED)(
            player, spell, guid[0], guid[1], &inRange, false, nullptr))
        return RangeState::NoCheck;
    return inRange != 0 ? RangeState::InRange : RangeState::OutOfRange;
}

bool ReadSwingType(void *L, int *out) {
    if (!Game::Lua::IsNumber(L, 1))
        return false;
    *out = static_cast<int>(Game::Lua::ToNumber(L, 1));
    return true;
}

// C_SwingTimer.EnableRangeCheck(swingType, enable)
int __cdecl Script_EnableRangeCheck(void *L) {
    int type = 0;
    if (!ReadSwingType(L, &type))
        return Game::Lua::Error(L, "Usage: C_SwingTimer.EnableRangeCheck(swingType, enable)");
    if (type < 0 || type > 2)
        return 0;
    const bool enable = Game::Lua::ToBoolean(L, 2) != 0;
    g_enabled[type] = enable;
    // Seed the change detector without firing: a caller reads the current
    // state with IsTargetWithinSwingRange right after enabling.
    g_lastState[type] = enable ? Evaluate(static_cast<SwingType>(type)) : RangeState::NoCheck;
    return 0;
}

// C_SwingTimer.IsTargetWithinSwingRange(swingType) -> isInRange or nil
int __cdecl Script_IsTargetWithinSwingRange(void *L) {
    int type = 0;
    if (!ReadSwingType(L, &type))
        return Game::Lua::Error(L, "Usage: C_SwingTimer.IsTargetWithinSwingRange(swingType)");
    const RangeState s =
        type >= 0 && type <= 2 ? Evaluate(static_cast<SwingType>(type)) : RangeState::NoCheck;
    if (s == RangeState::NoCheck)
        Game::Lua::PushNil(L);
    else
        Game::Lua::PushBool(L, s == RangeState::InRange);
    return 1;
}

// ---- PLAYER_SWING_RANGE_UPDATE(swingType, isInRange, checksRange) ---------

constexpr const char *kEventName = "PLAYER_SWING_RANGE_UPDATE";

const Event::Custom::AutoReserve _reserve{kEventName};

void OnFrame() {
    if (!g_enabled[0] && !g_enabled[1] && !g_enabled[2])
        return;
    const int eventID = Event::Custom::Lookup(kEventName);
    for (int t = 0; t < 3; ++t) {
        if (!g_enabled[t])
            continue;
        const RangeState cur = Evaluate(static_cast<SwingType>(t));
        if (cur == g_lastState[t])
            continue;
        g_lastState[t] = cur;
        Event::Custom::Fire(eventID, "%d%b%b", t, cur == RangeState::InRange ? 1 : 0,
                            cur != RangeState::NoCheck ? 1 : 0);
    }
}

const Tick::FrameTick::AutoSubscribe _tickSub{&OnFrame};

// The enable flags belong to the addons of one Lua session.
void PrepareForReload() {
    for (int t = 0; t < 3; ++t) {
        g_enabled[t] = false;
        g_lastState[t] = RangeState::NoCheck;
    }
}

const Game::ReloadAutoRegister _reload{&PrepareForReload};

void RegisterLuaFunctions() {
    Game::Lua::RegisterTableFunction("C_SwingTimer", "EnableRangeCheck",
                                     &Script_EnableRangeCheck);
    Game::Lua::RegisterTableFunction("C_SwingTimer", "IsTargetWithinSwingRange",
                                     &Script_IsTargetWithinSwingRange);
}

const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace

} // namespace Combat::SwingRange
