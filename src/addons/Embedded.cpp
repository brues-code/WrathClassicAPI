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

// Embedded `!!!WrathClassicAPI` addon fallback. When the user doesn't have
// the addon installed on disk, this module makes the engine load it anyway —
// without writing anything to the filesystem. Mirrors ClassicAPI's
// `src/addons/Embedded.cpp` (1.12), re-derived for 3.3.5 offsets/ABIs.
//
// How:
//
//   1. The CMake build embeds every file under `AddOns/!!!WrathClassicAPI/`
//      into a generated header (`embedded_wrathclassicapi.h`) as a byte
//      array per file plus a `{path, data, size}` manifest.
//
//   2. We hook the engine's file reader `FUN_FILE_READ`. For any path under
//      `Interface\AddOns\!!!WrathClassicAPI\`, the DISK copy wins when
//      present (so the on-disk addon stays editable without rebuilding the
//      DLL); only on a disk miss do we hand back a Storm buffer holding the
//      embedded content. The caller's normal `SMemFree` reclaims it.
//
//   3. We post-hook `FUN_ADDON_INIT`. After the engine's own
//      `Interface\AddOns\` scan finishes, we call the TOC parser
//      (`FUN_TOC_PARSER`) with `"!!!WrathClassicAPI"`. It reads the TOC via
//      our hooked reader, registers the addon at the HEAD of the load list
//      (so it loads before dependents), and honors `## DefaultState:
//      enabled` so it loads without a UI toggle. The parser is dedup-safe:
//      if the user already has the addon on disk, the engine's scan
//      registered it and our call early-outs — so "only if not on disk" is
//      free. We then hide the entry from the character-select AddOns list
//      (it's an implementation-detail library, not a user-toggleable addon)
//      by setting its filter byte via the engine's own name->entry lookup.

#include "embedded_wrathclassicapi.h"

#include "Game.h"
#include "Offsets.h"

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace Addons::Embedded {

namespace {

constexpr const char *kAddonName = "!!!WrathClassicAPI";

// `Interface\AddOns\!!!WrathClassicAPI\` — the prefix the engine builds for
// any of this addon's files. Matched case-insensitively (Windows paths are
// case-insensitive and the engine mixes `/` and `\`).
constexpr const char *kAddonPathPrefix = "Interface\\AddOns\\!!!WrathClassicAPI\\";

char NormalizeChar(char c) {
    if (c == '/') return '\\';
    if (c >= 'A' && c <= 'Z') return static_cast<char>(c + 32);
    return c;
}

bool PathEqualsCI(const char *a, const char *b) {
    while (*a && *b) {
        if (NormalizeChar(*a) != NormalizeChar(*b)) return false;
        ++a; ++b;
    }
    return *a == *b;
}

// Strip the `Interface\AddOns\!!!WrathClassicAPI\` prefix and return the
// suffix (e.g. `Util\Constants.lua`) on a match, NULL otherwise.
const char *StripAddonPrefix(const char *path) {
    if (path == nullptr) return nullptr;
    const char *p = path;
    const char *q = kAddonPathPrefix;
    while (*q) {
        if (*p == '\0') return nullptr;
        if (NormalizeChar(*p) != NormalizeChar(*q)) return nullptr;
        ++p; ++q;
    }
    return p;
}

const WrathClassicAPIFiles::File *LookupEmbedded(const char *suffix) {
    for (size_t i = 0; i < WrathClassicAPIFiles::kFileCount; ++i) {
        if (PathEqualsCI(suffix, WrathClassicAPIFiles::kFiles[i].path))
            return &WrathClassicAPIFiles::kFiles[i];
    }
    return nullptr;
}

// Engine file reader + Storm allocator — see the ABI notes on the offsets.
using FileReadFn = int(__stdcall *)(int unused, const char *path, void **outBuf,
                                    size_t *outSize, size_t extraBytes,
                                    int flag1, int flag2);
using SMemAllocFn = void *(__stdcall *)(size_t size, const char *file, int line,
                                        int flags);

FileReadFn FileRead_o = nullptr;

int __stdcall FileRead_h(int unused, const char *path, void **outBuf,
                         size_t *outSize, size_t extraBytes, int flag1,
                         int flag2) {
    const char *suffix = StripAddonPrefix(path);
    if (suffix == nullptr) {
        // Not our addon — passthrough.
        return FileRead_o(unused, path, outBuf, outSize, extraBytes, flag1, flag2);
    }

    // Disk copy wins when present, so a developer editing the on-disk addon
    // sees changes without rebuilding the DLL. Only synthesize the embedded
    // copy on a disk miss.
    const int diskResult =
        FileRead_o(unused, path, outBuf, outSize, extraBytes, flag1, flag2);
    if (diskResult != 0) return diskResult;

    const auto *entry = LookupEmbedded(suffix);
    if (entry == nullptr) {
        // Not in the embedded set either — genuine miss.
        return 0;
    }

    auto SMemAlloc = reinterpret_cast<SMemAllocFn>(Offsets::FUN_STORM_SMEM_ALLOC);
    const size_t totalSize = entry->size + extraBytes;
    void *buf = SMemAlloc(totalSize, __FILE__, __LINE__, 0);
    if (buf == nullptr) return 0;
    std::memcpy(buf, entry->data, entry->size);
    if (extraBytes > 0)
        std::memset(static_cast<uint8_t *>(buf) + entry->size, 0, extraBytes);
    if (outBuf != nullptr) *outBuf = buf;
    if (outSize != nullptr) *outSize = entry->size;  // caller may pass NULL
    return 1;
}

// `FUN_TOC_PARSER` takes its `const char *name` in EAX (a register-call the
// compiler emitted — no C calling convention expresses it), plain RET. This
// thunk loads EAX and calls through; MSVC treats the asm `call` as clobbering
// the caller-saved EAX/ECX/EDX, so no manual save/restore is needed.
void RegisterAddonToc(const char *name) {
    const uint32_t fn = Offsets::FUN_TOC_PARSER;
    __asm {
        mov eax, name
        call fn
    }
}

// Hide our registered entry from the character-select AddOns list. Fetches
// the entry via the engine's own name->entry hash lookup (`__thiscall`, hash
// table in ECX) — the same lookup `FUN_TOC_PARSER` uses — and sets its filter
// byte. Reusing the engine lookup avoids hand-walking the intrusive list.
// No-op if the entry isn't found (e.g. registration failed). Does not affect
// loading — only list visibility.
using AddonLookupFn = uint8_t *(__thiscall *)(void *nameHash, const char *name);

void HideEmbeddedEntry() {
    auto Lookup = reinterpret_cast<AddonLookupFn>(Offsets::FUN_ADDON_HASH_LOOKUP);
    uint8_t *entry =
        Lookup(reinterpret_cast<void *>(Offsets::VAR_ADDON_NAME_HASH), kAddonName);
    if (entry != nullptr)
        entry[Offsets::OFF_ADDON_ENTRY_FILTER_OUT] = 1;
}

// `FUN_ADDON_INIT` — `void __cdecl(char *basePath)`. Post-hooked: let the
// engine's `Interface\AddOns\` disk scan run first, then register the
// embedded addon and hide it from the list. Dedup-safe (see file header).
using AddonInitFn = void(__cdecl *)(char *basePath);
AddonInitFn AddonInit_o = nullptr;

void __cdecl AddonInit_h(char *basePath) {
    AddonInit_o(basePath);
    RegisterAddonToc(kAddonName);
    HideEmbeddedEntry();
}

const Game::HookAutoRegister _hookFileRead{
    Offsets::FUN_FILE_READ, reinterpret_cast<void *>(&FileRead_h),
    reinterpret_cast<void **>(&FileRead_o)};

const Game::HookAutoRegister _hookAddonInit{
    Offsets::FUN_ADDON_INIT, reinterpret_cast<void *>(&AddonInit_h),
    reinterpret_cast<void **>(&AddonInit_o)};

} // namespace

} // namespace Addons::Embedded
