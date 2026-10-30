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

// `UnitHasIncomingResurrection(unit)` and `INCOMING_RESURRECT_CHANGED` —
// 4.3.4's CGIncomingResurrection (Ui/IncomingResurrection.cpp). Ported from
// ClassicAPI's src/unit/IncomingResurrection.cpp.
//
// 4.3.4 adds {caster, spell} under a cast's object target, a unit or a corpse,
// when SMSG_SPELL_START names a resurrect spell
// (PendingSpellCast::OnSpellStartPacket), and removes it when the cast ends
// (FinishPendingSpellCast). Each change signals INCOMING_RESURRECT_CHANGED to
// every token naming the target, or the corpse's owner when the target is a
// corpse (Script_SendUnitSignal, which 3.3.5 has too). The query answers for
// the unit itself, then for its corpse (HasIncomingResurrection).
//
// Where 3.3.5 differs:
//   - Resurrect effects. 4.3.4 tests effect 18 alone. 3.3.5's class
//     resurrections (Resurrection, Redemption, Ancestral Spirit, Rebirth,
//     Revive, every rank) carry 113, RESURRECT_NEW, and 18 only NPC spells
//     (Spell.dbc), so both count.
//   - The cast's end. 3.3.5 keeps a PendingSpellCast only for the player's
//     own casts (4.3.4 added one for any resurrection), so a remote cast's
//     target is gone once SPELL_START is handled. The entries live here, and
//     the end comes from the packets that run FinishPendingSpellCast for a
//     remote caster: SMSG_SPELL_GO, and SMSG_SPELL_FAILURE, which the server
//     sends with every interrupt and cancel. FinishPendingSpellCast takes its
//     arguments in registers, so it isn't hooked. A cast is matched by caster,
//     castCount and spell, the identity GetPendingSpellCast uses.
//   - Lifetime. 4.3.4 empties the table on entering and leaving the world. A
//     cast whose caster leaves view never reports its end, so an entry whose
//     caster is no longer visible ends on the next frame, which covers a world
//     change as well.
//   - One cast per caster: a caster's new resurrection replaces its last.

#include "unit/IncomingResurrection.h"

#include "Game.h"
#include "Guid.h"
#include "Offsets.h"
#include "event/Custom.h"
#include "net/DataStore.h"
#include "spell/Lookup.h"
#include "tick/FrameTick.h"
#include "unit/Player.h"
#include "unit/Resolve.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace Unit::IncomingResurrection {

namespace {

template <typename T> T Read(const void *base, uintptr_t offset) {
    return *reinterpret_cast<const T *>(static_cast<const uint8_t *>(base) + offset);
}

const char *const kEventName = "INCOMING_RESURRECT_CHANGED";
const Event::Custom::AutoReserve _reserve{kEventName};

constexpr int32_t SPELL_EFFECT_RESURRECT = 18;
constexpr int32_t SPELL_EFFECT_RESURRECT_NEW = 113;

struct Entry {
    uint64_t target; // the cast's object target: a unit or a corpse
    uint64_t unit;   // who hears the change: the corpse's owner, else target
    uint64_t caster;
    uint8_t castCount;
    uint32_t spellID;
};

std::vector<Entry> g_entries;

void Signal(uint64_t unit) {
    const int eventID = Event::Custom::Lookup(kEventName);
    if (eventID < 0)
        return;
    using SendUnitSignal_t = void(__cdecl *)(const uint64_t *guid, int eventID);
    reinterpret_cast<SendUnitSignal_t>(Offsets::FUN_SCRIPT_SEND_UNIT_SIGNAL)(&unit, eventID);
}

// Ends every entry `match` accepts.
template <typename Match> void EndWhere(Match match) {
    for (size_t i = 0; i < g_entries.size();) {
        if (!match(g_entries[i])) {
            ++i;
            continue;
        }
        const uint64_t unit = g_entries[i].unit;
        g_entries.erase(g_entries.begin() + static_cast<std::ptrdiff_t>(i));
        Signal(unit);
    }
}

void End(uint64_t caster, uint8_t castCount, uint32_t spellID) {
    EndWhere([&](const Entry &e) {
        return e.caster == caster && e.castCount == castCount && e.spellID == spellID;
    });
}

// ---- Corpses ---------------------------------------------------------------

const uint8_t *CorpseData(const void *corpse) {
    return Read<const uint8_t *>(corpse, Offsets::OFF_CORPSE_DATA);
}

// The player who left the corpse `guid`, or 0 when `guid` is not a visible
// corpse.
uint64_t CorpseOwner(uint64_t guid) {
    const void *corpse = Guid::ResolveObject(Guid::Split(guid), Offsets::OBJ_FLAGS_CORPSE);
    return corpse != nullptr ? Read<uint64_t>(CorpseData(corpse), Offsets::OFF_CORPSE_FIELD_OWNER)
                             : 0;
}

struct CorpseScan {
    uint64_t owner;
    uint64_t corpse;
};

int __cdecl CorpseScanCallback(uint64_t guid, void *ctx) {
    auto *scan = static_cast<CorpseScan *>(ctx);
    const void *corpse = Guid::ResolveObject(Guid::Split(guid), Offsets::OBJ_FLAGS_CORPSE);
    if (corpse == nullptr)
        return 1;
    const uint8_t *data = CorpseData(corpse);
    if (Read<uint64_t>(data, Offsets::OFF_CORPSE_FIELD_OWNER) != scan->owner ||
        (Read<uint32_t>(data, Offsets::OFF_CORPSE_FIELD_FLAGS) & Offsets::CORPSE_FLAG_BONES) != 0)
        return 1;
    scan->corpse = guid;
    return 0;
}

// The visible corpse `owner` left, not counting bones, or 0 — 4.3.4's
// ClntObjMgrFindCorpseByOwner, over the visible objects.
uint64_t CorpseByOwner(uint64_t owner) {
    if (owner == 0 || Unit::LocalPlayer() == nullptr)
        return 0;
    using Callback_t = int(__cdecl *)(uint64_t guid, void *ctx);
    using EnumVisibleObjects_t = int(__cdecl *)(Callback_t cb, void *ctx);
    CorpseScan scan{owner, 0};
    reinterpret_cast<EnumVisibleObjects_t>(Offsets::FUN_CLNT_OBJ_MGR_ENUM_VISIBLE_OBJECTS)(
        &CorpseScanCallback, &scan);
    return scan.corpse;
}

// ---- Entries ---------------------------------------------------------------

void Begin(uint64_t target, uint64_t caster, uint8_t castCount, uint32_t spellID) {
    for (const Entry &e : g_entries) {
        if (e.caster == caster && e.castCount == castCount && e.spellID == spellID &&
            e.target == target)
            return;
    }
    EndWhere([&](const Entry &e) { return e.caster == caster; });
    const uint64_t owner = CorpseOwner(target);
    const uint64_t unit = owner != 0 ? owner : target;
    g_entries.push_back({target, unit, caster, castCount, spellID});
    Signal(unit);
}

bool Targeted(uint64_t target) {
    return std::any_of(g_entries.begin(), g_entries.end(),
                       [&](const Entry &e) { return e.target == target; });
}

bool HasIncomingResurrection(uint64_t guid) {
    if (guid == 0 || g_entries.empty())
        return false;
    if (Targeted(guid))
        return true;
    const uint64_t corpse = CorpseByOwner(guid);
    return corpse != 0 && Targeted(corpse);
}

bool HasEffect(const uint8_t *record, int32_t effect) {
    for (int i = 0; i < 3; ++i) {
        if (Read<int32_t>(record, Offsets::OFF_SPELL_EFFECT + i * 4) == effect)
            return true;
    }
    return false;
}

bool IsResurrect(uint32_t spellID) {
    uint8_t record[Offsets::SPELL_DBC_RECORD_SIZE];
    return Spell::Lookup::CopyRecord(spellID, record) &&
           (HasEffect(record, SPELL_EFFECT_RESURRECT) ||
            HasEffect(record, SPELL_EFFECT_RESURRECT_NEW));
}

void OnFrame() {
    if (g_entries.empty())
        return;
    EndWhere([](const Entry &e) {
        return Guid::ResolveObject(Guid::Split(e.caster), Offsets::OBJ_FLAGS_UNIT) == nullptr;
    });
}

const Tick::FrameTick::AutoSubscribe _tick{&OnFrame};

int __cdecl Script_UnitHasIncomingResurrection(void *L) {
    if (!Game::Lua::IsString(L, 1))
        return Game::Lua::Error(L, "Usage: UnitHasIncomingResurrection(\"unit\")");
    Game::Lua::PushBool(
        L, HasIncomingResurrection(Unit::GuidForToken(Game::Lua::ToString(L, 1))));
    return 1;
}

void RegisterLuaFunctions() {
    Game::Lua::RegisterGlobalFunction("UnitHasIncomingResurrection",
                                      &Script_UnitHasIncomingResurrection);
}

const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace

// SMSG_SPELL_START / SMSG_SPELL_GO: packed castItem, packed caster, castCount,
// spellID, castFlags, then for SPELL_START castTime and the targets.
void OnSpellCastPacket(uint32_t msgId, void *msg) {
    if (msgId == Offsets::SMSG_SPELL_GO && g_entries.empty())
        return;
    using namespace Net::DataStore;
    ReadPackedGuid(msg); // castItem
    const uint64_t caster = ReadPackedGuid(msg);
    const uint8_t castCount = ReadU8(msg);
    const uint32_t spellID = ReadU32(msg);
    if (msgId == Offsets::SMSG_SPELL_GO) {
        End(caster, castCount, spellID);
        return;
    }
    ReadU32(msg); // castFlags
    ReadU32(msg); // castTime
    const uint32_t targetMask = ReadU32(msg);
    const uint64_t target =
        (targetMask & Offsets::SPELL_TARGET_FLAGS_OBJECT) != 0 ? ReadPackedGuid(msg) : 0;
    if (Overrun(msg) || caster == 0 || target == 0 || !IsResurrect(spellID))
        return;
    Begin(target, caster, castCount, spellID);
}

// SMSG_SPELL_FAILURE: packed caster, castCount, spellID.
void OnSpellFailurePacket(void *msg) {
    if (g_entries.empty())
        return;
    using namespace Net::DataStore;
    const uint64_t caster = ReadPackedGuid(msg);
    const uint8_t castCount = ReadU8(msg);
    const uint32_t spellID = ReadU32(msg);
    End(caster, castCount, spellID);
}

} // namespace Unit::IncomingResurrection
