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

#include "item/Arg.h"

#include "Game.h"
#include "Offsets.h"
#include "item/ID.h"
#include "item/Location.h"

#include <cstdlib>
#include <cstring>

namespace Item::Arg {

namespace {

using StringToItemID_t = unsigned(__cdecl *)(const char *s);

// Item link OR item name (of a cached item) -> itemID via the engine's own
// resolver; 0 when the string is neither. See FUN_ITEM_STRING_TO_ID.
int EngineStringToItemID(const char *s) {
    return static_cast<int>(
        reinterpret_cast<StringToItemID_t>(Offsets::FUN_ITEM_STRING_TO_ID)(s));
}

// "0x..." item-GUID string -> the instance's itemID, or 0 if it doesn't resolve
// to a held item. Reuses Item::Location's GUID branch (HexString2Guid +
// ObjMgr::Get with the ITEM mask), then reads the itemID off the CGItem.
int GuidStringToItemID(void *L, int idx) {
    return Item::ID::FromCGItem(Item::Location::Resolve(L, idx));
}

} // namespace

Resolved Resolve(void *L, int idx) {
    Resolved out{};
    // Key on the concrete Lua type, NOT lua_isnumber: in Lua 5.1 a hex string
    // (an item GUID, "0x...") coerces to a number, so IsNumber would swallow a
    // GUID arg here and truncate it. A genuine number is an itemID; every string
    // form (GUID / link / numeric / name) resolves below.
    if (Game::Lua::Type(L, idx) == Game::Lua::TYPE_NUMBER) {
        out.itemID = static_cast<int>(Game::Lua::ToNumber(L, idx));
        return out;
    }
    if (Game::Lua::Type(L, idx) != Game::Lua::TYPE_STRING) {
        return out;
    }
    const char *s = Game::Lua::ToString(L, idx);
    if (s == nullptr || *s == '\0') {
        return out;
    }

    // Item GUID ("0x...") — a specific held instance; resolve to its itemID.
    if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
        out.itemID = GuidStringToItemID(L, idx);
        return out;
    }

    // Item link / bare "item:N..." — parse the itemID after "item:", plus the
    // random-enchant fields. 3.3.5 item string is
    //   item:id:enchant:gem1:gem2:gem3:gem4:suffix:seed[:level]
    // so `suffix` is the 6th colon-separated field after the id and `seed` the
    // 7th; a bare "item:N" leaves both 0.
    if (const char *m = std::strstr(s, "item:")) {
        m += 5;
        out.itemID = std::atoi(m);
        int colons = 0;
        for (const char *p = m; *p != '\0' && *p != '|'; ++p) {
            if (*p != ':')
                continue;
            ++colons;
            if (colons == 6) {
                out.suffix = std::atoi(p + 1);
            } else if (colons == 7) {
                out.seed = std::atoi(p + 1);
                break;
            }
        }
        return out;
    }

    // Bare numeric string.
    const int numeric = std::atoi(s);
    if (numeric > 0) {
        out.itemID = numeric;
        return out;
    }

    // Item name (of a cached item) -> itemID via the engine's name index;
    // otherwise expose the raw string as a name for name-match callers.
    const int byName = EngineStringToItemID(s);
    if (byName > 0) {
        out.itemID = byName;
    } else {
        out.name = s;
    }
    return out;
}

int ResolveItemID(void *L, int idx) {
    return Resolve(L, idx).itemID;
}

} // namespace Item::Arg
