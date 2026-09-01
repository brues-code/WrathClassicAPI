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

#include <cstdint>

// Shared unit-token resolution for any module that reads a unit by token. Every
// `unit/*` (and several other) modules otherwise re-declare the same resolver
// typedef and repeat the descriptor null-check; these two inline wrappers are
// the single definition.

namespace Unit {

// Resolves a unit token ("player", "target", "partyN", "raidN", "mouseover", …)
// to its CGUnit object, or nullptr when the token is null / unresolvable / the
// unit is outside the client's object-sync range. The engine's resolver returns
// null (it does not raise) for tokens it can't resolve, so a bad token degrades
// cleanly.
inline void *ResolveToken(const char *token) {
    if (token == nullptr)
        return nullptr;
    using Fn = void *(__cdecl *)(const char *);
    return reinterpret_cast<Fn>(Offsets::FUN_RESOLVE_UNIT_TOKEN)(token);
}

// The unit's UpdateField descriptor (`obj + OFF_UNIT_DESCRIPTOR`) — the block the
// per-field reads (race/class bytes, health, …) index into — or nullptr if `obj`
// is null or its descriptor isn't populated yet (e.g. mid engine-init at first
// login).
inline const uint8_t *Descriptor(const void *obj) {
    if (obj == nullptr)
        return nullptr;
    return *reinterpret_cast<const uint8_t *const *>(
        static_cast<const uint8_t *>(obj) + Offsets::OFF_UNIT_DESCRIPTOR);
}

// Where a unit's synced stats come from. `Object` = the live in-world object
// (read via its descriptor); `Party`/`Raid` = the group-roster stat cache for a
// member outside the client's object-sync range; `None` = unresolvable.
enum class MemberSource { None, Object, Party, Raid };

struct Member {
    MemberSource source = MemberSource::None;
    // For `Object`, the unit's descriptor (already dereferenced). For `Party` /
    // `Raid`, the roster record. Field layouts differ per source, so the caller
    // reads the stat it wants off `data` using the offsets matching `source`.
    const uint8_t *data = nullptr;
};

// Resolve a token to its stats source, walking the same fallthrough the engine's
// per-stat accessors (UnitHealth, UnitPower, …) all repeat: the live object's
// descriptor if the unit is in the client's sync range, else the party then raid
// roster record, else `None`. Lets a feature read a unit's stats whether or not
// the client currently holds a live object for it (e.g. a raid member across the
// zone) off one resolve + roster lookup.
inline Member ResolveMember(const char *token) {
    if (token == nullptr)
        return {};
    if (const uint8_t *desc = Descriptor(ResolveToken(token)))
        return {MemberSource::Object, desc};

    uint32_t guid[2] = {0, 0};
    using TokenToGuid_t = char(__cdecl *)(const char *token, uint32_t out[2], char flag);
    reinterpret_cast<TokenToGuid_t>(Offsets::FUN_TOKEN_TO_GUID)(token, guid, 0);

    using RosterByGuid_t = const uint8_t *(__cdecl *)(const uint32_t guid[2]);
    if (const uint8_t *rec =
            reinterpret_cast<RosterByGuid_t>(Offsets::FUN_PARTY_ROSTER_BY_GUID)(guid))
        return {MemberSource::Party, rec};
    if (const uint8_t *rec =
            reinterpret_cast<RosterByGuid_t>(Offsets::FUN_RAID_ROSTER_BY_GUID)(guid))
        return {MemberSource::Raid, rec};
    return {};
}

} // namespace Unit
