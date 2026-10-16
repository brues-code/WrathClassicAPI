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

#include "addons/TocFile.h"

#include "Offsets.h"
#include "addons/EngineIO.h"
#include "addons/Toc.h"

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace Addons::TocFile {

namespace {

// Finds `key` the way the engine's TOC parser (LoadAddOnInfo, 0x005F86A0)
// matches its own directives: after a UTF-8 BOM, a line starting "##", any
// spaces, then the key case-insensitively, immediately followed by ':'.
// Returns whether the value's leading integer (after spaces) is nonzero; false
// when the key is absent.
bool FlagIn(const char *buf, size_t size, const char *key) {
    const size_t klen = std::strlen(key);
    const char *end = buf + size;
    const char *start = buf;
    if (size >= 3 && static_cast<uint8_t>(buf[0]) == 0xEF &&
        static_cast<uint8_t>(buf[1]) == 0xBB && static_cast<uint8_t>(buf[2]) == 0xBF)
        start += 3;
    for (const char *line = start; line < end;) {
        const char *eol = line;
        while (eol < end && *eol != '\r' && *eol != '\n')
            ++eol;
        const char *p = line;
        if (eol - p >= 2 && p[0] == '#' && p[1] == '#') {
            p += 2;
            while (p < eol && *p == ' ')
                ++p;
            if (static_cast<size_t>(eol - p) > klen && Toc::EqCI(p, klen, key) && p[klen] == ':') {
                p += klen + 1;
                while (p < eol && (*p == ' ' || *p == '\t'))
                    ++p;
                long val = 0;
                for (; p < eol && *p >= '0' && *p <= '9'; ++p)
                    val = val * 10 + (*p - '0');
                return val != 0;
            }
        }
        line = eol + 1;
    }
    return false;
}

} // namespace

bool ReadFlag(const char *tocPath, const char *key) {
    auto FileRead = reinterpret_cast<EngineIO::FileReadFn>(
        static_cast<uintptr_t>(Offsets::FUN_FILE_READ));
    void *buf = nullptr;
    size_t size = 0;
    if (tocPath == nullptr || FileRead(0, tocPath, &buf, &size, 1, 1, 0) == 0 || buf == nullptr)
        return false;
    const bool set = FlagIn(static_cast<const char *>(buf), size, key);
    reinterpret_cast<EngineIO::SMemFreeFn>(static_cast<uintptr_t>(Offsets::FUN_STORM_SMEM_FREE))(
        buf, __FILE__, __LINE__, 0);
    return set;
}

} // namespace Addons::TocFile
