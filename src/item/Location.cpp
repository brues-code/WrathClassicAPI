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

#include "item/Location.h"

#include "Game.h"
#include "Offsets.h"

#include <cstdint>

namespace Item::Location {

namespace {

// ResolveUnitToken is plain cdecl — the engine's own callers push
// the token string and CALL (see disassembly at FUN_005EA3E0 +0x2F).
// GetItemBySlot and CGItem::GetContainer are __thiscall, exposed as
// `__fastcall` with a dummy EDX: same register layout (ECX = this,
// EDX unused, remaining args on the stack). Same pattern we use for
// other engine class methods.
using ResolveUnitToken_t = void *(__cdecl *)(const char *token);
using GetItemBySlot_t = void *(__fastcall *)(void *invMgr, void *edx, int slot0Based);
// `CGItem::GetContainer` — vtable slot 10 (byte offset 0x28). Returns
// the item's CGContainer if it's a bag, else NULL. Called by the
// engine in `PackBagSlot` at FUN_005D7380 as
// `(**(code **)(*bagItem + 0x28))()`.
using GetContainer_t = void *(__fastcall *)(void *bagItem, void *edx);
constexpr int kGetContainerVtableSlot = 10;

void *ResolvePlayerInvMgr() {
    auto fn = reinterpret_cast<ResolveUnitToken_t>(Offsets::FUN_RESOLVE_UNIT_TOKEN);
    auto *player = static_cast<uint8_t *>(fn("player"));
    if (player == nullptr)
        return nullptr;
    return player + Offsets::OFF_PLAYER_INVENTORY_MANAGER;
}

const uint8_t *CallGetItemBySlot(void *invMgr, int slot0Based) {
    auto fn = reinterpret_cast<GetItemBySlot_t>(Offsets::FUN_ITEMMGR_GET_ITEM_BY_SLOT);
    return static_cast<const uint8_t *>(fn(invMgr, nullptr, slot0Based));
}

// Read `loc.fieldName` and return it as an int. Leaves the Lua stack
// as it found it. Returns false if the field is missing or non-numeric.
bool TryReadIntField(void *L, int locIdx, const char *fieldName, int *out) {
    Game::Lua::GetField(L, locIdx, fieldName); // pushes value
    const bool ok = Game::Lua::IsNumber(L, -1);
    if (ok)
        *out = static_cast<int>(Game::Lua::ToNumber(L, -1));
    Game::Lua::SetTop(L, -2); // pop value (or nil)
    return ok;
}

} // namespace

const uint8_t *ResolveEquipmentSlot(int slot1Based) {
    if (slot1Based < Offsets::EQUIPMENT_SLOT_FIRST ||
        slot1Based > Offsets::EQUIPMENT_SLOT_LAST)
        return nullptr;
    void *invMgr = ResolvePlayerInvMgr();
    if (invMgr == nullptr)
        return nullptr;
    // Engine's Lua wrappers all decrement the 1-based slot before
    // calling GetItemBySlot — the linearized slot index is 0-based.
    return CallGetItemBySlot(invMgr, slot1Based - 1);
}

const uint8_t *ResolveBagSlot(int bagID, int slotIndex) {
    if (slotIndex < 1)
        return nullptr;

    if (bagID == 0) {
        // Backpack. PackBagSlot's `case 0: *outSlot += 0x17`. invMgr
        // is the player's own; linear slot = (slotIndex - 1) + 0x17.
        constexpr int kBackpackSize = 16;
        constexpr int kBackpackLinearBase = 0x17;
        if (slotIndex > kBackpackSize)
            return nullptr;
        void *invMgr = ResolvePlayerInvMgr();
        if (invMgr == nullptr)
            return nullptr;
        return CallGetItemBySlot(invMgr, (slotIndex - 1) + kBackpackLinearBase);
    }

    if (bagID >= 1 && bagID <= 4) {
        // Equipped bag at character-pane slot 20..23 (`INVSLOT_BAG1 +
        // bagID - 1`, expressed as a 0-based linear index for the
        // direct GetItemBySlot call — sidesteps ResolveEquipmentSlot's
        // 1..19 bound check since those slots are past the visible
        // paperdoll range but still in the player's linearized
        // inventory array).
        void *invMgr = ResolvePlayerInvMgr();
        if (invMgr == nullptr)
            return nullptr;
        constexpr int kInvSlotBag1Linear0Based = 19; // (= INVSLOT_BAG1 - 1)
        const uint8_t *bagItem =
            CallGetItemBySlot(invMgr, kInvSlotBag1Linear0Based + bagID - 1);
        if (bagItem == nullptr)
            return nullptr;

        // CGItem::GetContainer via vtable slot 10. Returns the bag's
        // CGContainer if it's a container, else NULL. Pure C++ — the
        // thiscall is expressed as fastcall with a dummy EDX.
        auto vtable = *reinterpret_cast<void *const *const *>(bagItem);
        auto getContainer =
            reinterpret_cast<GetContainer_t>(vtable[kGetContainerVtableSlot]);
        void *container = getContainer(const_cast<uint8_t *>(bagItem), nullptr);
        if (container == nullptr)
            return nullptr;

        // Slot within the bag is 1-based in Lua, 0-based for the
        // engine. Engine bounds-checks slot < container->numSlots
        // internally; CallGetItemBySlot returns nullptr for OOR.
        return CallGetItemBySlot(container, slotIndex - 1);
    }

    // bagID < 0 (keyring, bank, etc.) and bagID > 4 are not supported
    // in this build — they correspond to character-pane slots outside
    // the standard equipment+bag range and use different invMgr paths.
    return nullptr;
}

bool IsLocationArg(void *L, int idx) {
    // Only the table forms are supported in this build. String GUID
    // forms (`"0xHHHHHHHHLLLLLLLL"`) deferred — needs the engine's
    // CGItemMgr GUID resolver re-derived.
    return Game::Lua::Type(L, idx) == Game::Lua::TYPE_TABLE;
}

const uint8_t *Resolve(void *L, int locIdx) {
    if (Game::Lua::Type(L, locIdx) != Game::Lua::TYPE_TABLE)
        return nullptr;

    int eqSlot = 0;
    if (TryReadIntField(L, locIdx, "equipmentSlotIndex", &eqSlot))
        return ResolveEquipmentSlot(eqSlot);

    int bagID = 0, slotIndex = 0;
    if (TryReadIntField(L, locIdx, "bagID", &bagID) &&
        TryReadIntField(L, locIdx, "slotIndex", &slotIndex))
        return ResolveBagSlot(bagID, slotIndex);

    return nullptr;
}

} // namespace Item::Location
