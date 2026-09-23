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

// Inventory swap primitives. Contract in `Swap.h`; Lua-side bindings in
// `container/SwapItems.cpp` and `container/MoveItem.cpp`.

#include "item/Swap.h"

#include "Guid.h"
#include "Offsets.h"
#include "item/Location.h"
#include "unit/Resolve.h"

#include <cstdint>

namespace Item::Swap {

namespace {

// `Offsets::FUN_INVENTORY_SWAP`. The engine method is `__thiscall`; declaring
// it `__fastcall` with a dummy EDX gives the same register layout (ECX = this,
// EDX unused, the rest on the stack) — the pattern `Item::Location` already
// uses for GetItemBySlot / CGItem::GetContainer.
using SwapFn_t = void(__fastcall *)(void *player, void *edx,
                                    uint32_t srcItemLo, uint32_t srcItemHi,
                                    uint32_t srcContainerLo, uint32_t srcContainerHi,
                                    uint32_t srcLinearSlot,
                                    uint32_t dstContainerLo, uint32_t dstContainerHi,
                                    uint32_t dstLinearSlot,
                                    int flag);

// `Offsets::FUN_INVENTORY_SPLIT` — identical shape, `count` where the swap
// takes its flag.
using SplitFn_t = void(__fastcall *)(void *player, void *edx,
                                     uint32_t srcItemLo, uint32_t srcItemHi,
                                     uint32_t srcContainerLo, uint32_t srcContainerHi,
                                     uint32_t srcLinearSlot,
                                     uint32_t dstContainerLo, uint32_t dstContainerHi,
                                     uint32_t dstLinearSlot,
                                     int count);

// GUID of any CGObject-derived pointer (CGItem, CGContainer and CGPlayer all
// share the `+0x08 → instance block → +0x00 guid` layout).
Guid::Pair ReadGuid(const void *cgObject) {
    if (cgObject == nullptr)
        return {};
    auto *base = static_cast<const uint8_t *>(cgObject);
    auto *instance = *reinterpret_cast<const uint8_t *const *>(
        base + Offsets::OFF_ITEM_INSTANCE_BLOCK);
    if (instance == nullptr)
        return {};
    return {*reinterpret_cast<const uint32_t *>(instance + Offsets::OFF_INSTANCE_BLOCK_GUID),
            *reinterpret_cast<const uint32_t *>(instance + Offsets::OFF_INSTANCE_BLOCK_GUID + 4)};
}

// One side of a swap, in the form the engine primitive takes: which container
// owns the slot, and that slot's index within the container's own addressing.
struct Endpoint {
    Guid::Pair container;
    uint32_t linearSlot = 0;
};

// The player's own linear-slot range for a bagID, or `{0, 0}` when the bagID
// names a bag instead. Backpack, bank and keyring all live directly in the
// inventory manager and differ only in where their range starts.
struct PlayerRange {
    int first = 0;
    int last = 0;
    bool valid() const { return first != 0; }
};

PlayerRange PlayerRangeFor(int bagID) {
    if (bagID == 0)
        return {Offsets::INVMGR_BACKPACK_FIRST_SLOT, Offsets::INVMGR_BACKPACK_LAST_SLOT};
    if (bagID == -1)
        return {Offsets::INVMGR_BANK_MAIN_FIRST_SLOT, Offsets::INVMGR_BANK_MAIN_LAST_SLOT};
    if (bagID == -2)
        return {Offsets::INVMGR_KEYRING_FIRST_SLOT, Offsets::INVMGR_KEYRING_LAST_SLOT};
    return {};
}

// (bagID, 1-based slot) → `Endpoint`, mirroring the engine's own map in
// `FUN_005D7380`: backpack, bank and keyring are linear ranges of the player's
// inventory manager, while a bag — equipped or in the bank — is its own
// container addressed from 0.
//
// Both branches bound the slot the way the engine does, against the live slot
// count of the owning container rather than a hardcoded size, because
// FUN_INVENTORY_SWAP traps on an out-of-range linear slot rather than
// rejecting it.
bool Encode(int bagID, int slotInBag, Endpoint *out) {
    if (slotInBag < 1)
        return false;

    const PlayerRange range = PlayerRangeFor(bagID);
    if (range.valid()) {
        auto *player = static_cast<const uint8_t *>(Unit::ResolveToken("player"));
        if (player == nullptr)
            return false;
        const uint32_t linear = static_cast<uint32_t>(range.first - 1 + slotInBag);
        if (linear > static_cast<uint32_t>(range.last))
            return false; // would alias into the next range
        const uint32_t slotCount = *reinterpret_cast<const uint32_t *>(
            player + Offsets::OFF_PLAYER_INVENTORY_MANAGER + Offsets::OFF_INVMGR_SLOT_COUNT);
        if (linear >= slotCount)
            return false;
        out->container = ReadGuid(player);
        out->linearSlot = linear;
        return out->container.valid();
    }

    // Equipped bags (1..4) and bank bags (5..11). The bag's GUID is the
    // container's GUID — a bag is one object, and CGItem::GetContainer just
    // re-types it.
    const uint8_t *bagItem = Item::Location::ResolveEquippedBag(bagID);
    if (bagItem == nullptr)
        return false;
    const int numSlots = Item::Location::GetBagNumSlots(bagID);
    if (numSlots <= 0 || slotInBag > numSlots)
        return false;
    out->container = ReadGuid(bagItem);
    out->linearSlot = static_cast<uint32_t>(slotInBag - 1);
    return out->container.valid();
}

// Live stack count of an item — what `GetContainerItemInfo` reports as its
// count, not the item's max stack.
int StackCount(const uint8_t *item) {
    if (item == nullptr)
        return 0;
    auto *fields = *reinterpret_cast<const uint8_t *const *>(
        item + Offsets::OFF_ITEM_FIELD_BLOCK);
    if (fields == nullptr)
        return 0;
    return *reinterpret_cast<const int *>(
        fields + Offsets::OFF_ITEM_FIELD_STACK_COUNT);
}

// Everything either sender needs, resolved and validated together.
struct Request {
    void *player = nullptr;
    const uint8_t *srcItem = nullptr;
    Guid::Pair srcItemGuid;
    Endpoint src;
    Endpoint dst;
};

bool Prepare(int srcBag, int srcSlot, int dstBag, int dstSlot, Request *out) {
    if (srcBag == dstBag && srcSlot == dstSlot)
        return false; // a slot onto itself sends a packet that does nothing

    if (!Encode(srcBag, srcSlot, &out->src) || !Encode(dstBag, dstSlot, &out->dst))
        return false;

    // Source has to hold something — the engine builds the packet either way,
    // and the server answers an empty source with an error toast.
    out->srcItem = Item::Location::ResolveBagSlot(srcBag, srcSlot);
    if (out->srcItem == nullptr)
        return false;

    out->player = Unit::ResolveToken("player");
    if (out->player == nullptr)
        return false;

    out->srcItemGuid = ReadGuid(out->srcItem);
    return true;
}

} // namespace

bool Containers(int srcBag, int srcSlot, int dstBag, int dstSlot) {
    Request r;
    if (!Prepare(srcBag, srcSlot, dstBag, dstSlot, &r))
        return false;

    auto fn = reinterpret_cast<SwapFn_t>(
        static_cast<uintptr_t>(Offsets::FUN_INVENTORY_SWAP));
    fn(r.player, nullptr,
       r.srcItemGuid.lo, r.srcItemGuid.hi,
       r.src.container.lo, r.src.container.hi, r.src.linearSlot,
       r.dst.container.lo, r.dst.container.hi, r.dst.linearSlot,
       0);
    return true;
}

bool MoveCount(int srcBag, int srcSlot, int dstBag, int dstSlot, int count) {
    // Checked before anything else: the engine traps on a count below 1
    // rather than rejecting it.
    if (count < 1)
        return false;

    Request r;
    if (!Prepare(srcBag, srcSlot, dstBag, dstSlot, &r))
        return false;

    const int stack = StackCount(r.srcItem);
    if (count > stack)
        return false; // the server would refuse it; fail locally instead
    if (count == stack) {
        // A split may not empty its source, but a swap of the whole stack
        // onto a matching one merges — so "move everything" goes that way.
        return Containers(srcBag, srcSlot, dstBag, dstSlot);
    }

    auto fn = reinterpret_cast<SplitFn_t>(
        static_cast<uintptr_t>(Offsets::FUN_INVENTORY_SPLIT));
    fn(r.player, nullptr,
       r.srcItemGuid.lo, r.srcItemGuid.hi,
       r.src.container.lo, r.src.container.hi, r.src.linearSlot,
       r.dst.container.lo, r.dst.container.hi, r.dst.linearSlot,
       count);
    return true;
}

} // namespace Item::Swap
