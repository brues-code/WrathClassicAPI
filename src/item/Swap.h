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

#pragma once

namespace Item::Swap {

// Atomic server-side swap of two container slots — the same engine primitive
// (`Offsets::FUN_INVENTORY_SWAP`) a drag-and-drop between bag slots lowers to,
// with the cursor never read or written.
//
// bagID follows the `GetContainerItemInfo` convention:
//   0    = backpack
//   1..4 = equipped bags (paperdoll slots 20..23)
// `slot` is 1-based. The bank (bagID -1 / 5..11) and the keyring (bagID -2)
// are not addressable here, matching what `Item::Location` resolves today —
// their linear-slot ranges are recorded in `Offsets::INVMGR_*` for whenever
// that changes.
//
// The destination may be empty (the engine treats that as a move) or occupied
// (atomic swap). The source must be occupied. Returns false without sending
// anything for: an out-of-range bagID, a slot outside its bag's real slot
// count, an unequipped bag, an empty source, or src and dst naming the same
// slot. Everything else is server-side — the send is fire-and-forget and
// failures come back through the normal SMSG_INVENTORY_CHANGE_FAILURE /
// BAG_UPDATE flow.
//
// No Lua involvement: every lookup goes through `Item::Location`'s Lua-free
// resolvers, so this is safe to call from a tick or event callback as well as
// from inside a Lua call.
bool Containers(int srcBag, int srcSlot, int dstBag, int dstSlot);

// Atomic "split `count` off the source stack and place it at the destination"
// (`Offsets::FUN_INVENTORY_SPLIT`) — what a cursor split-and-drop lowers to,
// again without touching the cursor. Same bagID / slot conventions and the
// same Lua-free guarantee as `Containers`.
//
// Server-enforced, all-or-nothing:
//   destination empty                 → `count` items are placed there
//   destination same item, room left  → `count` items merge into it
//   destination different / no room   → refused, nothing moves
//
// `count == the whole source stack` is rerouted through `Containers`: the
// server refuses a split that would empty its source ("Couldn't split those
// items"), while a swap of a full stack onto a matching one merges. So
// "move everything" works without the caller special-casing it.
//
// Returns false for everything `Containers` rejects, plus a `count` below 1
// or above the source stack. Note the source stack is read live, so a caller
// firing several of these in one frame — with no server replies in between —
// is comparing against pre-batch counts; drive such a batch from your own
// bookkeeping rather than from what the client currently reports.
bool MoveCount(int srcBag, int srcSlot, int dstBag, int dstSlot, int count);

} // namespace Item::Swap
