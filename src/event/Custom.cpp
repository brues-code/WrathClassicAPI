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

#include "Custom.h"

#include "Game.h"
#include "Offsets.h"

#include <cstdint>
#include <cstring>

namespace Event::Custom {

namespace {

// Reservations registered by `AutoReserve` at static-init time, before
// any engine code runs. `RegisterReservedEvents` (called from the
// GameUI post-hook) walks this list to build the extended name array.
constexpr int MAX_RESERVED = 32;
struct ReservedName {
    const char *name;
    int slot;  // -1 until RegisterReservedEvents has run
};
ReservedName g_reserved[MAX_RESERVED];
int g_reservedCount = 0;

using FillEvents_t = void(__cdecl *)(const char **list, int count);

// Read the current name from event-list entry `i`. Entry layout (from
// `FrameScript_FillEvents` source): hash at +0x00, name pointer at
// +0x14. Returns nullptr for empty slots.
//
// VAR_EVENT_LIST_PTR is the *address of a global variable* that *holds*
// the Event** base pointer. Two dereferences needed: read the
// variable's value to get the array base, then index into the array.
const char *ReadName(int i) {
    auto **arrayBase = *reinterpret_cast<uint8_t ***>(Offsets::VAR_EVENT_LIST_PTR);
    if (arrayBase == nullptr)
        return nullptr;
    auto *entry = arrayBase[i];
    if (entry == nullptr)
        return nullptr;
    return *reinterpret_cast<const char *const *>(
        entry + Offsets::OFF_EVENT_ENTRY_NAME_IN_ENTRY);
}

int CurrentCount() {
    return *reinterpret_cast<int *>(Offsets::VAR_EVENT_LIST_COUNT);
}

} // namespace

AutoReserve::AutoReserve(const char *name) {
    if (name == nullptr || g_reservedCount >= MAX_RESERVED)
        return;
    for (int i = 0; i < g_reservedCount; ++i) {
        if (std::strcmp(g_reserved[i].name, name) == 0)
            return;
    }
    g_reserved[g_reservedCount].name = name;
    g_reserved[g_reservedCount].slot = -1;
    ++g_reservedCount;
}

int Lookup(const char *name) {
    if (name == nullptr)
        return -1;
    for (int i = 0; i < g_reservedCount; ++i) {
        if (std::strcmp(g_reserved[i].name, name) == 0)
            return g_reserved[i].slot;
    }
    return -1;
}

void Fire(int eventID, const char *format, ...) {
    if (eventID < 0)
        return;
    using vFireEvent_t = void(__cdecl *)(int, const char *, va_list);
    auto fn = reinterpret_cast<vFireEvent_t>(Offsets::FUN_VFIRE_EVENT);
    va_list ap;
    va_start(ap, format);
    fn(eventID, format, ap);
    va_end(ap);
}

void RegisterReservedEvents() {
    const int existing = CurrentCount();
    if (existing <= 0)
        return;  // engine hasn't populated yet — wrong call timing

    // Build a combined name array: every currently-registered name
    // followed by our reservations. The engine's `FillEvents` is
    // idempotent for already-registered names (it finds them in the
    // hash table and reuses the existing Event* entry), so passing
    // the existing names back is non-destructive and preserves IDs.
    const char *combined[1024];
    const int maxCombined = static_cast<int>(sizeof(combined) / sizeof(combined[0]));
    if (existing + g_reservedCount > maxCombined)
        return;  // belt-and-suspenders bounds check

    for (int i = 0; i < existing; ++i)
        combined[i] = ReadName(i);
    for (int i = 0; i < g_reservedCount; ++i)
        combined[existing + i] = g_reserved[i].name;

    auto fillEvents = reinterpret_cast<FillEvents_t>(
        Offsets::FUN_FRAMESCRIPT_FILL_EVENTS);
    fillEvents(combined, existing + g_reservedCount);

    // Each reservation now lives at its position in the extended
    // list. Cache the integer ID for fast `Fire()` lookups.
    for (int i = 0; i < g_reservedCount; ++i)
        g_reserved[i].slot = existing + i;
}

namespace {

void RegisterLuaFunctions() {
    // No Lua functions to register here — the module's job is to
    // append reservations to the engine's event table. This runs
    // from `Game::RunModuleRegistrations` in the GameUI post-hook,
    // which is the correct timing (after `FrameScript_FillEvents`).
    RegisterReservedEvents();
}

const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace

} // namespace Event::Custom
