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

// `BAG_UPDATE_DELAYED` event — fires once per frame in which any
// `BAG_UPDATE` fired. Addons listen to DELAYED instead of BAG_UPDATE
// and rescan once per frame regardless of how many updates fired.
// Ported from ClassicAPI's src/bag/UpdateDelayed.cpp (1.12), re-derived
// for 3.3.5: the keyring direct-fire branch folded into the item->bag
// resolver here, so two hooks cover all five fire sites (1.12 needed
// three).
//
// Architecture: two MinHooks plus one frame-tick subscription.
//
//   1. `FUN_BAG_ITEM_TO_BAG` — item->container resolver (most common
//      path, incl. backpack/keyring/token). Post-hook sets `g_pending`.
//   2. `FUN_BAG_SLOT_DIFF` — bag-slot diff loop (bag equip/unequip and
//      the world-enter bag registration). Post-hook sets `g_pending`.
//   3. `Tick::FrameTick::AutoSubscribe` — shared per-frame subscription.
//      The callback drains `g_pending` and fires DELAYED if set.
//
// Both hook targets are quiet (4 and 2 callers, in the 0x005Dxxxx
// item/container region no popular DLL touches). Coverage verified by
// imm32-scanning .text for the BAG_UPDATE event ID (0x13A) and
// classifying every hit — exactly five fire sites, all in these two
// functions. The diff loop calls into the resolver's caller path on
// some transitions; double-set is moot: `g_pending` is an idempotent
// bool until the per-frame drain clears it.

#include "Game.h"
#include "Offsets.h"
#include "event/Custom.h"
#include "tick/FrameTick.h"

#include <cstdint>

namespace Bag::UpdateDelayed {

namespace {

constexpr const char *kEventName = "BAG_UPDATE_DELAYED";

const Event::Custom::AutoReserve _reserve{kEventName};

// Set by the bag-update hooks, drained by the frame-tick callback.
// Single bool, no atomics needed — the engine is single-threaded.
bool g_pending = false;

// `FUN_BAG_ITEM_TO_BAG` — `void __cdecl(int guidLo, int guidHi)`.
using ItemToBag_t = void(__cdecl *)(int guidLo, int guidHi);
ItemToBag_t ItemToBag_o = nullptr;
void __cdecl ItemToBag_h(int guidLo, int guidHi) {
    ItemToBag_o(guidLo, guidHi);
    g_pending = true;
}

// `FUN_BAG_SLOT_DIFF` — `void __cdecl(void)`.
using BagSlotDiff_t = void(__cdecl *)();
BagSlotDiff_t BagSlotDiff_o = nullptr;
void __cdecl BagSlotDiff_h() {
    BagSlotDiff_o();
    g_pending = true;
}

// Per-frame drain. Runs at the tail of each frame (after the engine's
// OnUpdate dispatch); all of this frame's BAG_UPDATEs have already
// fired, so one DELAYED covers them.
void OnFrame() {
    if (!g_pending)
        return;
    // Bag changes aren't meaningful until the player is actually in the
    // world. During any loading screen (initial login, zone/instance
    // transition) the engine re-populates bag slots as objects stream in
    // (setting g_pending) while still behind the screen. Hold the pending
    // flag — do NOT clear it — until the in-world flag is set; the first
    // in-world frame then emits a single DELAYED covering the settled
    // inventory. VAR_IN_WORLD clears on every transition, so this
    // re-suppresses on later zone changes too, not just initial login.
    if (*reinterpret_cast<const volatile uint8_t *>(
            static_cast<uintptr_t>(Offsets::VAR_IN_WORLD)) == 0)
        return;
    g_pending = false;
    Event::Custom::Fire(Event::Custom::Lookup(kEventName), "");
}

const Game::HookAutoRegister _hookItemToBag{
    Offsets::FUN_BAG_ITEM_TO_BAG,
    reinterpret_cast<void *>(&ItemToBag_h),
    reinterpret_cast<void **>(&ItemToBag_o)};

const Game::HookAutoRegister _hookSlotDiff{
    Offsets::FUN_BAG_SLOT_DIFF,
    reinterpret_cast<void *>(&BagSlotDiff_h),
    reinterpret_cast<void **>(&BagSlotDiff_o)};

const Tick::FrameTick::AutoSubscribe _tickSub{&OnFrame};

} // namespace

} // namespace Bag::UpdateDelayed
