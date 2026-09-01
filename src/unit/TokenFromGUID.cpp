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

// `UnitTokenFromGUID(guid) -> unitToken` — the unit token that currently refers
// to `guid` ("player", "target", "partyN", "raidN", "arenaN", "pet", "focus",
// "mouseover", …), or nil if no live unit token maps to it.
//
// Parses the GUID string the same way the engine does (HexString2Guid), then
// resolves it through the engine's authoritative keyword list (Unit::GuidToTokens
// — the chokepoint the engine itself walks to name a unit for UNIT_* events),
// falling back to the single-token resolver only for "mouseover" (which the
// keyword list omits). Going through that chokepoint means custom tokens other
// loaded mods register there — e.g. awesome_wotlk's "nameplateN" — resolve here
// too, so the token returned is exactly what the game would call the unit.

#include "Game.h"
#include "Offsets.h"
#include "unit/Resolve.h"

#include <cstdint>

namespace Unit::TokenFromGUID {

namespace {

using HexString2Guid_t = uint64_t(__cdecl *)(const char *s);

int __cdecl Script_UnitTokenFromGUID(void *L) {
    // Modern raises on a nil / non-string GUID (e.g. UnitGUID of an absent unit),
    // so match that rather than returning nil. A valid-but-unmapped GUID still
    // returns nil below.
    if (!Game::Lua::IsString(L, 1)) {
        Game::Lua::Error(L, "Usage: local unitToken = UnitTokenFromGUID(unitGUID)");
        return 0;
    }
    const char *guidStr = Game::Lua::ToString(L, 1);
    if (guidStr == nullptr)
        return 0;

    const uint64_t guid =
        reinterpret_cast<HexString2Guid_t>(Offsets::FUN_HEXSTRING_TO_GUID)(guidStr);
    if (guid == 0)
        return 0; // nil — empty / zero GUID maps to no unit

    const uint32_t pair[2] = {static_cast<uint32_t>(guid),
                              static_cast<uint32_t>(guid >> 32)};

    // Authoritative keyword list first (includes injected tokens like
    // "nameplateN"); the single-token resolver as a fallback catches "mouseover".
    int count = 0;
    const char *const *tokens = Unit::GuidToTokens(pair, &count);
    const char *token =
        (tokens != nullptr && count > 0) ? tokens[0] : Unit::GuidToToken(pair);
    if (token == nullptr)
        return 0; // nil — GUID isn't any currently-tokened unit

    Game::Lua::PushString(L, token);
    return 1;
}

void RegisterLuaFunctions() {
    Game::Lua::RegisterGlobalFunction("UnitTokenFromGUID", &Script_UnitTokenFromGUID);
}

const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace

} // namespace Unit::TokenFromGUID
