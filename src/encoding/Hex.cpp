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

#include "Game.h"

#include <cstdint>
#include <vector>

namespace Encoding::Hex {

namespace {

// `C_EncodingUtil.EncodeHex(data) -> string` — lowercase hex pair per
// input byte. Empty input maps to empty string. Output is binary-safe
// (no embedded NULs) and `2 * len(data)` bytes long.
int __cdecl Script_EncodeHex(void *L) {
    if (!Game::Lua::IsString(L, 1))
        return Game::Lua::Error(L, "Usage: C_EncodingUtil.EncodeHex(data)");
    const char *src = Game::Lua::ToString(L, 1);
    const unsigned int len = Game::Lua::StrLen(L, 1);
    if (src == nullptr || len == 0) {
        Game::Lua::PushString(L, "");
        return 1;
    }

    static const char kHex[] = "0123456789abcdef";
    std::vector<char> out(static_cast<std::size_t>(len) * 2);
    for (unsigned int i = 0; i < len; ++i) {
        const uint8_t b = static_cast<uint8_t>(src[i]);
        out[i * 2 + 0] = kHex[b >> 4];
        out[i * 2 + 1] = kHex[b & 0x0F];
    }
    Game::Lua::PushLString(L, out.data(), len * 2);
    return 1;
}

// `C_EncodingUtil.DecodeHex(hex) -> string | nil` — inverse of
// `EncodeHex`. Accepts either case (`abcdef` / `ABCDEF`). Returns nil
// on odd-length input or any non-hex character — reject bad input
// rather than silently dropping characters.
int __cdecl Script_DecodeHex(void *L) {
    if (!Game::Lua::IsString(L, 1))
        return Game::Lua::Error(L, "Usage: C_EncodingUtil.DecodeHex(hex)");
    const char *src = Game::Lua::ToString(L, 1);
    const unsigned int len = Game::Lua::StrLen(L, 1);
    if (src == nullptr || len == 0) {
        Game::Lua::PushString(L, "");
        return 1;
    }
    if ((len & 1) != 0)
        return 0;

    auto nibble = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return 10 + (c - 'a');
        if (c >= 'A' && c <= 'F') return 10 + (c - 'A');
        return -1;
    };

    std::vector<char> out(len / 2);
    for (unsigned int i = 0; i < len; i += 2) {
        const int hi = nibble(src[i]);
        const int lo = nibble(src[i + 1]);
        if (hi < 0 || lo < 0)
            return 0;
        out[i / 2] = static_cast<char>((hi << 4) | lo);
    }
    Game::Lua::PushLString(L, out.data(), len / 2);
    return 1;
}

void RegisterLuaFunctions() {
    Game::Lua::RegisterTableFunction("C_EncodingUtil", "EncodeHex",
                                     &Script_EncodeHex);
    Game::Lua::RegisterTableFunction("C_EncodingUtil", "DecodeHex",
                                     &Script_DecodeHex);
}

const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace

} // namespace Encoding::Hex
