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

// `PLAYER_SWING(swingDuration, swingType)` and `Enum.PlayerSwingType` — the
// Classic swing-timer event. Ported from ClassicAPI's src/combat/Swing.cpp,
// with the timer rules taken from the 3.3.5 server (AzerothCore):
//
//   melee swing          PlayerUpdates.cpp  resets that hand and RANGED; the other
//                                           hand is pushed to at least 200 ms
//   on-next-swing spell  (same path)        the main-hand swing it replaces
//   autorepeat shot      Unit::_UpdateAutoRepeatSpell  resets RANGED, MAIN and OFF
//   finished cast        Spell::_cast       resets all three, see CastResets
//   cast that doesn't    Unit::Update       pauses MAIN/OFF while it is cast
//     reset timers
//   attack start         Unit::Attack       a ready off hand waits for the main
//                                           hand plus 50% of its speed
//   parried              Unit::DealMeleeDamage  the server's parry haste
//
// The client sees these through its own packet parsers: ATTACKROUNDINFO::Unpack
// (SMSG_ATTACKER_STATE_UPDATE), CGUnit_C::SetAttacking (SMSG_ATTACK_START), and
// read-only peeks at the head of SMSG_SPELL_START / _GO / _FAILURE.
//
// Each change only marks a swing type dirty; the frame tick fires at most one
// event per type per frame with the remaining time. Extra attacks (Windfury,
// Sword Specialization) send several packets for one reset, and still produce
// one event.

#include "combat/Swing.h"

#include "Game.h"
#include "Guid.h"
#include "Offsets.h"
#include "event/Custom.h"
#include "item/ID.h"
#include "item/Location.h"
#include "spell/Lookup.h"
#include "tick/FrameTick.h"
#include "unit/Player.h"
#include "unit/Position.h"
#include "unit/Resolve.h"

#include <windows.h>

#include <algorithm>
#include <cstdint>

namespace Combat::Swing {

namespace {

template <typename T> T Read(const void *base, uintptr_t offset) {
    return *reinterpret_cast<const T *>(static_cast<const uint8_t *>(base) + offset);
}

using GetItemRecord_t = const uint8_t *(__thiscall *)(void *cache, uint32_t itemID,
                                                      const uint64_t *guid, void *callback,
                                                      void *userData, int unused);

// Item-stats cache peek — no query fired on a miss (callback = nullptr).
// Equipped items are always cached.
const uint8_t *PeekItemRecord(uint32_t itemID) {
    auto fn = reinterpret_cast<GetItemRecord_t>(Offsets::FUN_DBCACHE_ITEMSTATS_GET_RECORD);
    auto *cache = reinterpret_cast<void *>(Offsets::VAR_ITEMDB_CACHE);
    const uint64_t zeroGuid = 0;
    return fn(cache, itemID, &zeroGuid, nullptr, nullptr, 0);
}

constexpr int kSlotMainHand = 16;
constexpr int kSlotOffHand = 17;
constexpr int kSlotRanged = 18;

const uint8_t *EquippedWeaponStats(int slot1Based) {
    const int itemID = Item::ID::FromCGItem(Item::Location::ResolveEquipmentSlot(slot1Based));
    if (itemID <= 0)
        return nullptr;
    const uint8_t *stats = PeekItemRecord(static_cast<uint32_t>(itemID));
    if (stats == nullptr ||
        Read<uint32_t>(stats, Offsets::OFF_ITEMSTATS_CLASS) != Offsets::ITEM_CLASS_WEAPON)
        return nullptr;
    return stats;
}

uint64_t PlayerGuid() {
    using GetGuid_t = uint64_t(__cdecl *)();
    return reinterpret_cast<GetGuid_t>(static_cast<uintptr_t>(Offsets::FUN_LOCAL_PLAYER_GUID))();
}

// The server's leeway test (WorldObject::GetLeewayBonusRangeForTargets): the
// unit runs, strafes or falls, and isn't walking.
bool MovingForLeeway(const void *unit) {
    const uint32_t flags = Read<uint32_t>(unit, Offsets::OFF_UNIT_MOVEMENT_FLAGS);
    constexpr uint32_t kMoving = Offsets::MOVEMENTFLAG_FORWARD | Offsets::MOVEMENTFLAG_STRAFE_LEFT |
                                 Offsets::MOVEMENTFLAG_STRAFE_RIGHT |
                                 Offsets::MOVEMENTFLAG_FALLING;
    return (flags & kMoving) != 0 && (flags & Offsets::MOVEMENTFLAG_WALKING) == 0;
}

} // namespace

uint32_t AttackTimeMs(SwingType type) {
    const uint8_t *desc = Unit::Descriptor(Unit::LocalPlayer());
    if (desc == nullptr)
        return 0;
    const uintptr_t off = type == MAIN_HAND ? Offsets::OFF_UNIT_FIELD_MAINHAND_ATTACK_TIME
                          : type == OFF_HAND ? Offsets::OFF_UNIT_FIELD_OFFHAND_ATTACK_TIME
                                             : Offsets::OFF_UNIT_FIELD_RANGED_ATTACK_TIME;
    return Read<uint32_t>(desc, off);
}

bool HasWeapon(SwingType type) {
    switch (type) {
    case MAIN_HAND:
        return true;
    case OFF_HAND:
        // The engine's dual-wield latch (what CanDualWield reads).
        return *reinterpret_cast<const uint32_t *>(
                   static_cast<uintptr_t>(Offsets::VAR_DUAL_WIELD_GRANT_SPELL_ID)) != 0 &&
               EquippedWeaponStats(kSlotOffHand) != nullptr;
    case RANGED:
        return EquippedWeaponStats(kSlotRanged) != nullptr;
    }
    return false;
}

// Unit::IsWithinMeleeRange: 3D distance under max(reach + reach + 4/3, 5),
// plus 2.66 yd when both units are on the move.
bool InMeleeRange(void *player, void *target) {
    const uint8_t *playerDesc = Unit::Descriptor(player);
    const uint8_t *targetDesc = Unit::Descriptor(target);
    float p[3];
    float t[3];
    if (playerDesc == nullptr || targetDesc == nullptr || !Unit::Position::Read(player, p) ||
        !Unit::Position::Read(target, t))
        return false;
    float range = Read<float>(playerDesc, Offsets::OFF_UNIT_FIELD_COMBAT_REACH) +
                  Read<float>(targetDesc, Offsets::OFF_UNIT_FIELD_COMBAT_REACH) + 4.0f / 3.0f;
    range = (std::max)(range, 5.0f);
    if (MovingForLeeway(player) && MovingForLeeway(target))
        range += 2.66f;
    const float dx = p[0] - t[0];
    const float dy = p[1] - t[1];
    const float dz = p[2] - t[2];
    return dx * dx + dy * dy + dz * dz < range * range;
}

namespace {

constexpr const char *kEventName = "PLAYER_SWING";

const Event::Custom::AutoReserve _reserve{kEventName};

// ---- Per-swing-type state ------------------------------------------------

struct State {
    DWORD endMs = 0;
    bool active = false; // at least one reset seen this session
    bool dirty = false;  // fire PLAYER_SWING on the next frame
};
State g_swing[3];

// GetTickCount wraps every ~49 days; a signed difference stays correct.
uint32_t Remaining(SwingType type) {
    const auto delta = static_cast<int32_t>(g_swing[type].endMs - GetTickCount());
    return delta > 0 ? static_cast<uint32_t>(delta) : 0;
}

void SetRemaining(SwingType type, uint32_t remainingMs) {
    if (!HasWeapon(type))
        return;
    State &s = g_swing[type];
    s.endMs = GetTickCount() + remainingMs;
    s.active = true;
    s.dirty = true;
}

// Unit::resetAttackTimer: the full (hasted) swing time.
void Reset(SwingType type) {
    const uint32_t duration = AttackTimeMs(type);
    if (duration != 0)
        SetRemaining(type, duration);
}

// After one hand swings, the server holds the other hand at 200 ms or more
// so the two never land together (ATTACK_DISPLAY_DELAY).
constexpr uint32_t kAttackDisplayDelayMs = 200;

void OnMeleeSwing(SwingType hand) {
    Reset(hand);
    Reset(RANGED);
    const SwingType other = hand == MAIN_HAND ? OFF_HAND : MAIN_HAND;
    if (Remaining(other) < kAttackDisplayDelayMs)
        SetRemaining(other, kAttackDisplayDelayMs);
}

// ---- Parry haste -----------------------------------------------------------

constexpr uint32_t kUnarmedDelayMs = 2000;

// The unhasted swing time for a hand (Unit::GetAttackTime). The descriptor
// carries the hasted value, so read the weapon's own delay.
uint32_t UnhastedDelayMs(SwingType hand) {
    const uint8_t *stats = EquippedWeaponStats(hand == OFF_HAND ? kSlotOffHand : kSlotMainHand);
    if (stats != nullptr) {
        const uint32_t delay = Read<uint32_t>(stats, Offsets::OFF_ITEMSTATS_DELAY);
        if (delay != 0)
            return delay;
    }
    return kUnarmedDelayMs;
}

// Unit::DealMeleeDamage, VICTIMSTATE_PARRY: the hand with less time left (the
// off hand only with an off-hand weapon) is cut to 20% of its unhasted delay if
// it had 20%..60% left, or loses 40% of the delay if it had more than 60% left.
void ApplyParryHaste() {
    const uint32_t mainLeft = Remaining(MAIN_HAND);
    const uint32_t offLeft = Remaining(OFF_HAND);
    const SwingType hand = HasWeapon(OFF_HAND) && offLeft < mainLeft ? OFF_HAND : MAIN_HAND;
    if (!g_swing[hand].active)
        return;

    const auto left = static_cast<float>(hand == OFF_HAND ? offLeft : mainLeft);
    const float p20 = static_cast<float>(UnhastedDelayMs(hand)) * 0.20f;
    const float p60 = 3.0f * p20;
    if (left > p20 && left <= p60)
        SetRemaining(hand, static_cast<uint32_t>(p20));
    else if (left > p60)
        SetRemaining(hand, static_cast<uint32_t>(left - 2.0f * p20));
}

// ---- SMSG_ATTACKER_STATE_UPDATE ------------------------------------------

using AttackRoundUnpack_t = void(__fastcall *)(void *self, void *edx, void *msg);
AttackRoundUnpack_t g_unpackOriginal = nullptr;

void __fastcall AttackRoundUnpack_h(void *self, void *edx, void *msg) {
    g_unpackOriginal(self, edx, msg);
    const uint64_t player = PlayerGuid();
    if (player == 0)
        return;
    if (Read<uint64_t>(self, Offsets::OFF_ATTACKROUND_ATTACKER) == player) {
        const uint32_t hitInfo = Read<uint32_t>(self, Offsets::OFF_ATTACKROUND_HIT_INFO);
        OnMeleeSwing((hitInfo & Offsets::HITINFO_OFFHAND) != 0 ? OFF_HAND : MAIN_HAND);
    } else if (Read<uint64_t>(self, Offsets::OFF_ATTACKROUND_TARGET) == player &&
               Read<uint32_t>(self, Offsets::OFF_ATTACKROUND_VICTIM_STATE) ==
                   Offsets::VICTIMSTATE_PARRY) {
        ApplyParryHaste();
    }
}

const Game::HookAutoRegister _unpackHook{Offsets::FUN_ATTACK_ROUND_UNPACK,
                                         reinterpret_cast<void *>(&AttackRoundUnpack_h),
                                         reinterpret_cast<void **>(&g_unpackOriginal)};

// ---- SMSG_ATTACK_START -----------------------------------------------------

using SetAttacking_t = void(__fastcall *)(void *self, void *edx, uint32_t victimLo,
                                          uint32_t victimHi);
SetAttacking_t g_setAttackingOriginal = nullptr;

// Unit::Attack: a ready off hand is delayed until the main hand's next swing
// plus 50% of the main-hand speed. Out of range the server then holds it
// until the target is reachable, with no packet, so only report it when the
// time can come true: in melee range.
void __fastcall SetAttacking_h(void *self, void *edx, uint32_t victimLo, uint32_t victimHi) {
    g_setAttackingOriginal(self, edx, victimLo, victimHi);
    void *player = Unit::LocalPlayer();
    if (player == nullptr || self != player || Remaining(OFF_HAND) != 0)
        return;
    void *victim = Guid::ResolveObject({victimLo, victimHi}, Offsets::OBJ_FLAGS_UNIT);
    if (InMeleeRange(player, victim))
        SetRemaining(OFF_HAND, Remaining(MAIN_HAND) + AttackTimeMs(MAIN_HAND) / 2);
}

const Game::HookAutoRegister _setAttackingHook{Offsets::FUN_UNIT_SET_ATTACKING,
                                               reinterpret_cast<void *>(&SetAttacking_h),
                                               reinterpret_cast<void **>(&g_setAttackingOriginal)};

// ---- Spell casts -----------------------------------------------------------

// The player's cast in progress, from its SPELL_START. A cast that doesn't
// reset the timers pauses the melee timers while it is cast, so their
// remaining time is kept here and restored when it ends.
struct Cast {
    bool valid = false;
    uint8_t castCount = 0;
    uint32_t spellID = 0;
    int32_t castTimeMs = 0;
    bool pauses = false;
    uint32_t pausedLeft[2] = {};
} g_cast;

struct SpellFlags {
    uint32_t attr;
    uint32_t attrEx2;
    uint32_t attrEx6;
    uint32_t interrupt;
    int32_t baseCastTimeMs;
};

bool ReadSpellFlags(uint32_t spellID, SpellFlags *out) {
    uint8_t rec[Offsets::SPELL_DBC_RECORD_SIZE];
    if (!Spell::Lookup::CopyRecord(spellID, rec))
        return false;
    out->attr = Read<uint32_t>(rec, Offsets::OFF_SPELL_RECORD_ATTRIBUTES);
    out->attrEx2 = Read<uint32_t>(rec, Offsets::OFF_SPELL_ATTRIBUTES_EX2);
    out->attrEx6 = Read<uint32_t>(rec, Offsets::OFF_SPELL_ATTRIBUTES_EX6);
    out->interrupt = Read<uint32_t>(rec, Offsets::OFF_SPELL_INTERRUPT_FLAGS);
    // SpellInfo::CalcCastTime with no caster: the DBC base, +500 ms for a
    // ranged-slot spell that isn't autorepeat.
    int32_t base =
        Spell::Lookup::CastTimeMs(Read<uint32_t>(rec, Offsets::OFF_SPELL_CASTING_TIME_INDEX));
    if ((out->attr & Offsets::SPELL_ATTR_USES_RANGED_SLOT) != 0 &&
        (out->attrEx2 & Offsets::SPELL_ATTR_EX2_AUTOREPEAT) == 0)
        base += 500;
    out->baseCastTimeMs = base;
    return true;
}

// Spell::IsAutoActionResetSpell and the checks around it in Spell::_cast.
// `castTimeMs` is this cast's actual cast time.
bool CastResets(const SpellFlags &f, uint8_t castCount, int32_t castTimeMs) {
    if (castCount == 0) // triggered
        return false;
    if ((f.interrupt & Offsets::SPELL_INTERRUPT_FLAG_INTERRUPT) == 0)
        return false;
    if ((f.attrEx2 & Offsets::SPELL_ATTR_EX2_NOT_RESET_AUTO_ACTIONS) != 0)
        return false;
    if (castTimeMs <= 0) {
        if ((f.attrEx6 & Offsets::SPELL_ATTR_EX6_NO_RESET_IF_INSTANT) != 0)
            return false;
        if (f.baseCastTimeMs > 0) // made instant (Presence of Mind, Art of War, ...)
            return false;
    }
    return true;
}

bool IsCurrentCast(uint8_t castCount, uint32_t spellID) {
    return g_cast.valid && g_cast.castCount == castCount && g_cast.spellID == spellID;
}

// The paused melee timers run again from where they stopped.
void EndCast() {
    if (g_cast.pauses) {
        for (int t = 0; t < 2; ++t)
            if (g_cast.pausedLeft[t] != 0)
                SetRemaining(static_cast<SwingType>(t), g_cast.pausedLeft[t]);
    }
    g_cast.valid = false;
}

void OnSpellStart(uint32_t spellID, uint8_t castCount, int32_t castTimeMs) {
    SpellFlags f{};
    if (castCount == 0 || !ReadSpellFlags(spellID, &f))
        return;
    g_cast = {};
    g_cast.valid = true;
    g_cast.castCount = castCount;
    g_cast.spellID = spellID;
    g_cast.castTimeMs = castTimeMs;
    g_cast.pauses =
        castTimeMs > 0 && (f.attrEx2 & Offsets::SPELL_ATTR_EX2_NOT_RESET_AUTO_ACTIONS) != 0;
    if (g_cast.pauses) {
        g_cast.pausedLeft[MAIN_HAND] = Remaining(MAIN_HAND);
        g_cast.pausedLeft[OFF_HAND] = Remaining(OFF_HAND);
    }
}

void OnSpellGo(uint32_t spellID, uint8_t castCount) {
    SpellFlags f{};
    if (!ReadSpellFlags(spellID, &f))
        return;

    if ((f.attr & Offsets::SPELL_ATTR_ON_NEXT_SWING) != 0) {
        // Goes off in place of a main-hand swing.
        OnMeleeSwing(MAIN_HAND);
        return;
    }
    if ((f.attrEx2 & Offsets::SPELL_ATTR_EX2_AUTOREPEAT) != 0) {
        Reset(RANGED);
        Reset(MAIN_HAND);
        Reset(OFF_HAND);
        return;
    }

    int32_t castTimeMs = 0;
    if (IsCurrentCast(castCount, spellID)) {
        castTimeMs = g_cast.castTimeMs;
        EndCast();
    }
    if (CastResets(f, castCount, castTimeMs)) {
        Reset(MAIN_HAND);
        Reset(OFF_HAND);
        Reset(RANGED);
    }
}

using GetU8_t = void *(__fastcall *)(void *msg, void *edx, uint8_t *out);
using GetU32_t = void *(__fastcall *)(void *msg, void *edx, uint32_t *out);
using GetPackedGuid_t = void *(__cdecl *)(void *msg, uint64_t *out);

// Reads a spell packet's head through the engine's own readers. Callers put
// the read cursor back afterwards, so the handler sees the packet untouched.
struct SpellPacketHead {
    uint64_t caster = 0;
    uint8_t castCount = 0;
    uint32_t spellID = 0;
};

uint32_t &ReadCursor(void *msg) {
    return *reinterpret_cast<uint32_t *>(static_cast<uint8_t *>(msg) + Offsets::OFF_DATASTORE_READ);
}

uint64_t ReadPackedGuid(void *msg) {
    uint64_t guid = 0;
    reinterpret_cast<GetPackedGuid_t>(Offsets::FUN_DATASTORE_GET_PACKED_GUID)(msg, &guid);
    return guid;
}

uint8_t ReadU8(void *msg) {
    uint8_t v = 0;
    reinterpret_cast<GetU8_t>(Offsets::FUN_DATASTORE_GET_U8)(msg, nullptr, &v);
    return v;
}

uint32_t ReadU32(void *msg) {
    uint32_t v = 0;
    reinterpret_cast<GetU32_t>(Offsets::FUN_DATASTORE_GET_U32)(msg, nullptr, &v);
    return v;
}

// SMSG_SPELL_START / SMSG_SPELL_GO: packed castItem, packed caster, castCount,
// spellID, castFlags, then (START) castTime.
void PeekSpellCast(uint32_t msgId, void *msg) {
    uint32_t &cursor = ReadCursor(msg);
    const uint32_t saved = cursor;
    ReadPackedGuid(msg); // castItem
    const uint64_t caster = ReadPackedGuid(msg);
    const uint8_t castCount = ReadU8(msg);
    const uint32_t spellID = ReadU32(msg);
    ReadU32(msg); // castFlags
    const auto castTimeMs = static_cast<int32_t>(ReadU32(msg));
    cursor = saved;

    const uint64_t player = PlayerGuid();
    if (player == 0 || caster != player || spellID == 0)
        return;
    if (msgId == Offsets::SMSG_SPELL_START)
        OnSpellStart(spellID, castCount, castTimeMs);
    else
        OnSpellGo(spellID, castCount);
}

using SpellPacket_t = int(__cdecl *)(void *param, uint32_t msgId, uint32_t time, void *msg);
SpellPacket_t g_spellCastPacketOriginal = nullptr;
SpellPacket_t g_spellFailurePacketOriginal = nullptr;

int __cdecl SpellCastPacket_h(void *param, uint32_t msgId, uint32_t time, void *msg) {
    if ((msgId == Offsets::SMSG_SPELL_START || msgId == Offsets::SMSG_SPELL_GO) && msg != nullptr)
        PeekSpellCast(msgId, msg);
    return g_spellCastPacketOriginal(param, msgId, time, msg);
}

// SMSG_SPELL_FAILURE: packed caster, castCount, spellID. A cancelled cast
// ends the pause too.
int __cdecl SpellFailurePacket_h(void *param, uint32_t msgId, uint32_t time, void *msg) {
    if (msg != nullptr && g_cast.valid) {
        uint32_t &cursor = ReadCursor(msg);
        const uint32_t saved = cursor;
        const uint64_t caster = ReadPackedGuid(msg);
        const uint8_t castCount = ReadU8(msg);
        const uint32_t spellID = ReadU32(msg);
        cursor = saved;
        if (caster == PlayerGuid() && IsCurrentCast(castCount, spellID))
            EndCast();
    }
    return g_spellFailurePacketOriginal(param, msgId, time, msg);
}

const Game::HookAutoRegister _spellCastPacketHook{
    Offsets::FUN_PACKET_SPELL_CAST, reinterpret_cast<void *>(&SpellCastPacket_h),
    reinterpret_cast<void **>(&g_spellCastPacketOriginal)};

const Game::HookAutoRegister _spellFailurePacketHook{
    Offsets::FUN_PACKET_SPELL_FAILURE, reinterpret_cast<void *>(&SpellFailurePacket_h),
    reinterpret_cast<void **>(&g_spellFailurePacketOriginal)};

// ---- PLAYER_SWING fire (once per type per frame) -------------------------

void OnFrame() {
    const int eventID = Event::Custom::Lookup(kEventName);
    for (int t = 0; t < 3; ++t) {
        State &s = g_swing[t];
        if (!s.dirty)
            continue;
        s.dirty = false;
        const uint32_t remaining = Remaining(static_cast<SwingType>(t));
        if (remaining == 0)
            continue; // already elapsed: nothing to show
        Event::Custom::Fire(eventID, "%f%d", static_cast<double>(remaining) * 0.001, t);
    }
}

const Tick::FrameTick::AutoSubscribe _tickSub{&OnFrame};

// ---- Enum.PlayerSwingType ------------------------------------------------

const Game::Lua::EnumIntegerEntry kSwingTypeEntries[] = {
    {"MainHand", MAIN_HAND},
    {"OffHand", OFF_HAND},
    {"Ranged", RANGED},
};

void RegisterLuaFunctions() {
    Game::Lua::RegisterIntegerEnum("Enum", "PlayerSwingType", kSwingTypeEntries,
                                   static_cast<int>(sizeof(kSwingTypeEntries) /
                                                    sizeof(kSwingTypeEntries[0])));
}

const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace

} // namespace Combat::Swing
