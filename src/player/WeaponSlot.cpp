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

// `WEAPON_SLOT_CHANGED` (no payload) — the narrow sibling of
// PLAYER_EQUIPMENT_CHANGED that swing-timer consumers want: it fires only
// for the three weapon slots (16 main hand / 17 off hand / 18 ranged), so
// they don't filter a 19-slot event in Lua on every gear change. Ported
// from ClassicAPI's src/player/Equipment.cpp.
//
// The trigger is the engine's own: InventoryUpdate, the observer that runs
// for each changed inventory-slot field and fires PLAYER_EQUIPMENT_CHANGED.
// We hook it and mark weapon-slot changes; the fire is merged onto the
// next frame, because one action can change two weapon slots (a two-hander
// replacing a one-hander plus an off-hand). With no payload there's
// nothing to lose by merging, and Blizzard's generated docs flag this event
// `UniqueEvent`.
//
// Slot 18 is exempted for the classes whose slot 18 is a relic slot — see
// IsWeaponSlot.

#include "Game.h"
#include "Offsets.h"
#include "dbc/Store.h"
#include "event/Custom.h"
#include "tick/FrameTick.h"

#include <cstdint>

namespace Player::WeaponSlot {

namespace {

const char *const kWeaponSlotChanged = "WEAPON_SLOT_CHANGED";
const Event::Custom::AutoReserve _reserveWeaponSlotChanged{kWeaponSlotChanged};

// 0-based inventory slots, so Lua 16/17/18.
constexpr uint32_t kSlotMainHand = 15;
constexpr uint32_t kSlotOffHand = 16;
constexpr uint32_t kSlotRanged = 17;
bool g_weaponDirty = false;

// Whether slot 18 holds relics instead of a ranged weapon — the test
// Script_UnitHasRelicSlot makes. Read live: logging out to a character of
// another class changes the answer.
bool PlayerHasRelicSlot() {
    const int classID = *reinterpret_cast<const uint8_t *>(
        static_cast<uintptr_t>(Offsets::VAR_LOCAL_PLAYER_CLASS_BYTE));
    const uint8_t *cls = DBC::Record(Offsets::VAR_CHRCLASSES_DBC_MIN_INDEX,
                                     Offsets::VAR_CHRCLASSES_DBC_MAX_INDEX,
                                     Offsets::VAR_CHRCLASSES_DBC_INDEX_TABLE, classID);
    return cls != nullptr &&
           (*reinterpret_cast<const uint32_t *>(cls + Offsets::OFF_CHRCLASSES_FLAGS) &
            Offsets::CHRCLASSES_FLAG_RELIC_SLOT) != 0;
}

// A relic is a passive stat item, never something you swing, so a relic
// swap is not a weapon change.
bool IsWeaponSlot(uint32_t slot) {
    if (slot == kSlotMainHand || slot == kSlotOffHand)
        return true;
    return slot == kSlotRanged && !PlayerHasRelicSlot();
}

using InventoryUpdate_t = int(__cdecl *)(uint32_t guidLo, uint32_t guidHi, uint32_t fieldOffset,
                                         uint32_t count, const void *value, void *param);
InventoryUpdate_t g_inventoryUpdateOriginal = nullptr;

int __cdecl InventoryUpdate_h(uint32_t guidLo, uint32_t guidHi, uint32_t fieldOffset,
                              uint32_t count, const void *value, void *param) {
    const int result =
        g_inventoryUpdateOriginal(guidLo, guidHi, fieldOffset, count, value, param);
    // Mark only; the fire is merged onto the next frame.
    if (fieldOffset >= Offsets::INV_SLOT_FIELD_FIRST &&
        IsWeaponSlot((fieldOffset - Offsets::INV_SLOT_FIELD_FIRST) / 8))
        g_weaponDirty = true;
    return result;
}

const Game::HookAutoRegister _hookreg{
    Offsets::FUN_INVENTORY_UPDATE,
    reinterpret_cast<void *>(&InventoryUpdate_h),
    reinterpret_cast<void **>(&g_inventoryUpdateOriginal)};

// The ID check runs before clearing the flag: until the event is
// registered, a change is held rather than swallowed.
void OnTick() {
    if (!g_weaponDirty)
        return;
    const int eventID = Event::Custom::Lookup(kWeaponSlotChanged);
    if (eventID < 0)
        return;
    g_weaponDirty = false;
    Event::Custom::Fire(eventID, "");
}

const Tick::FrameTick::AutoSubscribe _tick{&OnTick};

} // namespace

} // namespace Player::WeaponSlot
