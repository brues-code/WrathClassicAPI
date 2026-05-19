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

#pragma once

#include "Offsets.h"

#include <cstdarg>

namespace Event::Custom {

// Reserve an event name to be appended to the engine's event table
// during the GameUI post-hook. Place a static instance at file scope:
//
//   static const Event::Custom::AutoReserve _r{"MY_EVENT"};
//
// Static-init queues the name onto an internal list before `DllMain`
// runs. `RegisterReservedEvents` (called from the GameUI post-hook in
// `Event::Custom`'s own module init, runs AFTER engine `FillEvents`)
// rebuilds the engine's name list with the engine's entries + our
// reservations appended, and re-calls `FrameScript_FillEvents`. The
// engine reuses existing hash-table entries for the original names
// (so their integer IDs are preserved) and allocates new ones for
// ours — assigning each a stable ID equal to its position in the
// extended list.
//
// Differences from ClassicAPI's (1.12) Custom event flow:
//   * 1.12 used a flat array and required claiming NULL slots. 3.3.5's
//     event registry is a hash table + ordered list; we can just call
//     the engine's batch populator with an extended list.
//   * No SStrDup needed — the engine stores name pointers by reference.
//     Name argument must outlive the process (string literals are fine).
//   * No /reload reservation reset needed — see the project note in
//     memory: /reload goes through a different path that doesn't
//     repopulate the event table.
//
// Same name reserved twice is deduped; more than 32 names silently
// drops the overflow (matches ClassicAPI's MAX_RESERVED cap).
struct AutoReserve {
    explicit AutoReserve(const char *name);
};

// Returns the integer event ID currently assigned to `name`, or -1 if
// the name isn't registered yet (e.g. before the GameUI post-hook has
// run, or for a reservation that overflowed). Caller should call this
// at fire time — IDs are stable for the process lifetime so they can
// be cached after the GameUI post-hook completes, but on a code path
// that could fire before that, look up each time.
int Lookup(const char *name);

// Dispatches a custom event via the engine's `FUN_VFIRE_EVENT`.
// `format` is a concatenation of these tokens, one per payload arg,
// no separators or literal text:
//   `%d` int, `%u` uint, `%f` double, `%s` const char *, `%b` int (bool)
// String args must outlive the call (engine doesn't copy them out of
// varargs); compile-time literals are fine.
//
// Examples:
//   Fire(eventID, "");                       // no payload
//   Fire(eventID, "%d", id);
//   Fire(eventID, "%d%d", itemID, success);  // GET_ITEM_INFO_RECEIVED
//   Fire(eventID, "%s%d", keyName, down);    // MODIFIER_STATE_CHANGED
//
// No-op for `eventID < 0`, which lets callers cheaply guard on the
// `Lookup()` result without an explicit if.
//
// `vFireEvent`'s real signature is `(int eventID, const char *fmt,
// va_list args)` — it expects a *pointer* to the args buffer, not
// individual args. We use `va_start` to grab the stack pointer to
// our caller's pushed varargs and pass that. Same as awesome_wotlk's
// `FrameScript::FireEvent`.
void Fire(int eventID, const char *format, ...);

// Internal: append all reserved names to the engine's event table by
// re-invoking `FrameScript_FillEvents` with the current name list +
// our reservations. Called from this module's `RegisterLuaFunctions`,
// which runs from the GameUI post-hook chain after the engine's own
// `FrameScript_FillEvents` has populated the initial entries.
void RegisterReservedEvents();

} // namespace Event::Custom
