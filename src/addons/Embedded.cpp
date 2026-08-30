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
//      `Interface\AddOns\!!!WrathClassicAPI\`, we choose disk vs embedded ONCE
//      (see DecideSource) and serve accordingly, handing back a Storm buffer
//      the caller's normal `SMemFree` reclaims. Precedence:
//        * a `.wrathclassicapi-dev` marker in the on-disk folder → disk wins
//          unconditionally (explicit developer override, so a dev edits the
//          on-disk addon against a *released* DLL);
//        * otherwise the newer `## Version:` wins (embedded is stamped from the
//          release tag; the "DEV" sentinel sorts below every real release), so
//          non-devs always get the DLL's copy — even with a stale one on disk —
//          while a locally-built DEV dll defers to a DEV disk copy.
//
//   3. We PRE-hook the login disk scan `FUN_ADDON_DISK_SCAN`: BEFORE the
//      engine walks `Interface\AddOns\`, we call the TOC parser
//      (`FUN_TOC_PARSER`) with `"!!!WrathClassicAPI"`. The scan then appends
//      every disk addon AFTER ours, so the embedded entry sits at the HEAD of
//      the load-order list and loads FIRST — before any addon that consumes
//      its globals (the load pass walks head->tail; a post-scan registration
//      would load LAST). It reads the TOC via our hooked reader and honors
//      `## DefaultState: enabled` so it loads without a UI toggle. The parser
//      is dedup-safe: a disk copy the scan also finds early-outs. We then hide
//      the entry from the character-select AddOns list (an implementation-
//      detail library, not a user-toggleable addon) via the engine's own
//      name->entry lookup.

#include "embedded_wrathclassicapi.h"

#include "Game.h"
#include "Offsets.h"
#include "addons/EngineIO.h"

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace Addons::Embedded {

namespace {

constexpr const char *kAddonName = "!!!WrathClassicAPI";
constexpr const char *kAddonTocFile = "!!!WrathClassicAPI.toc";

// Developer marker. When this file exists in the on-disk addon folder, the
// disk copy wins unconditionally (see DiskHasDevMarker / DecideSource). It is
// gitignored, excluded from the embed, and the release ships only the DLL — so
// it only ever exists in a developer working tree.
constexpr const char *kDevMarkerFile = ".wrathclassicapi-dev";

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

// Engine file reader + Storm allocator/free — shapes centralized in EngineIO.h
// (ABI-critical: all __stdcall).
using Addons::EngineIO::FileReadFn;
using Addons::EngineIO::SMemAllocFn;
using Addons::EngineIO::SMemFreeFn;

FileReadFn FileRead_o = nullptr;

// Extract the trimmed value of the `## Version:` line from a TOC buffer into
// `out`. Returns true on success. Line-oriented, case-insensitive on the key,
// matching the engine's `## Key: Value` TOC format — same scan ClassicAPI's
// AddOns::Toc::FindValue does, inlined here since this is its only user.
bool ExtractTocVersion(const char *content, size_t size, char *out,
                       size_t outSize) {
    if (outSize == 0) return false;
    out[0] = '\0';
    static const char kDirective[] = "## Version:";
    const size_t dlen = sizeof(kDirective) - 1;
    for (size_t i = 0; i + dlen <= size; ++i) {
        const bool atLineStart = (i == 0) || content[i - 1] == '\n';
        if (!atLineStart || _strnicmp(content + i, kDirective, dlen) != 0)
            continue;
        const char *p = content + i + dlen;
        const char *end = content + size;
        while (p < end && (*p == ' ' || *p == '\t')) ++p;
        const char *v = p;
        while (p < end && *p != '\r' && *p != '\n') ++p;
        while (p > v && (p[-1] == ' ' || p[-1] == '\t')) --p;
        size_t n = static_cast<size_t>(p - v);
        if (n >= outSize) n = outSize - 1;
        std::memcpy(out, v, n);
        out[n] = '\0';
        return n > 0;
    }
    return false;
}

// Returns -1/0/+1 for a < b / a == b / a > b. "DEV" is the local-build
// sentinel: two DEV builds are equal, but a DEV build sorts BELOW any real
// release. Otherwise the strings are walked as dot-separated numeric semver
// components (`1.2` < `1.10`).
int CompareVersions(const char *a, const char *b) {
    const bool aDev = std::strcmp(a, "DEV") == 0;
    const bool bDev = std::strcmp(b, "DEV") == 0;
    if (aDev && bDev) return 0;
    if (aDev) return -1;
    if (bDev) return 1;
    while (*a || *b) {
        int va = 0, vb = 0;
        while (*a >= '0' && *a <= '9') { va = va * 10 + (*a - '0'); ++a; }
        while (*b >= '0' && *b <= '9') { vb = vb * 10 + (*b - '0'); ++b; }
        if (va != vb) return va < vb ? -1 : 1;
        if (*a == '.') ++a;
        if (*b == '.') ++b;
        if (!*a && !*b) break;
        if (!*a) return -1;
        if (!*b) return 1;
    }
    return 0;
}

// Pre-extracted embedded TOC version, populated lazily on first DecideSource.
char g_embeddedVersion[64] = "";

void EnsureEmbeddedVersionExtracted() {
    if (g_embeddedVersion[0] != '\0') return;
    for (size_t i = 0; i < WrathClassicAPIFiles::kFileCount; ++i) {
        if (PathEqualsCI(kAddonTocFile, WrathClassicAPIFiles::kFiles[i].path)) {
            ExtractTocVersion(
                reinterpret_cast<const char *>(WrathClassicAPIFiles::kFiles[i].data),
                WrathClassicAPIFiles::kFiles[i].size, g_embeddedVersion,
                sizeof(g_embeddedVersion));
            return;
        }
    }
}

// True iff the on-disk addon folder contains the `.wrathclassicapi-dev` marker.
// Read via the ORIGINAL FileRead (bypassing our embed hook), so it reflects a
// real file on disk / in an MPQ — never an embedded copy. That makes it a clean
// "is this a developer working tree" signal.
bool DiskHasDevMarker() {
    char fullPath[256];
    std::snprintf(fullPath, sizeof(fullPath), "%s%s", kAddonPathPrefix,
                  kDevMarkerFile);
    void *buf = nullptr;
    size_t size = 0;
    const int ok = FileRead_o(0, fullPath, &buf, &size, 1, 1, 0);
    if (ok == 0 || buf == nullptr) return false;
    auto SMemFree = reinterpret_cast<SMemFreeFn>(Offsets::FUN_STORM_SMEM_FREE);
    SMemFree(buf, __FILE__, __LINE__, 0);
    return true;
}

// Which source serves every read for this addon, decided once on the first
// matching file read.
enum class Source { Undecided, Disk, Embedded };
Source g_source = Source::Undecided;

// Decide disk vs embedded. The `.wrathclassicapi-dev` marker forces disk
// unconditionally (explicit developer override). Otherwise read the on-disk
// TOC via the ORIGINAL FileRead (bypassing our hook), parse its version,
// compare to the embedded version, and cache the newer. No disk TOC at all →
// embedded wins by default.
void DecideSource() {
    if (g_source != Source::Undecided) return;
    EnsureEmbeddedVersionExtracted();

    if (DiskHasDevMarker()) {
        g_source = Source::Disk;
        return;
    }

    char fullPath[256];
    std::snprintf(fullPath, sizeof(fullPath), "%s%s", kAddonPathPrefix,
                  kAddonTocFile);
    void *diskBuf = nullptr;
    size_t diskSize = 0;
    const int ok = FileRead_o(0, fullPath, &diskBuf, &diskSize, 1, 1, 0);
    if (ok == 0 || diskBuf == nullptr) {
        g_source = Source::Embedded;
        return;
    }

    char diskVersion[64] = "";
    ExtractTocVersion(static_cast<const char *>(diskBuf), diskSize, diskVersion,
                      sizeof(diskVersion));
    auto SMemFree = reinterpret_cast<SMemFreeFn>(Offsets::FUN_STORM_SMEM_FREE);
    SMemFree(diskBuf, __FILE__, __LINE__, 0);

    // Missing/unparseable disk version → assume older than anything we ship.
    if (diskVersion[0] == '\0') {
        g_source = Source::Embedded;
        return;
    }
    const int cmp = CompareVersions(g_embeddedVersion, diskVersion);
    g_source = (cmp > 0) ? Source::Embedded : Source::Disk;
}

int __stdcall FileRead_h(int unused, const char *path, void **outBuf,
                         size_t *outSize, size_t extraBytes, int flag1,
                         int flag2) {
    const char *suffix = StripAddonPrefix(path);
    if (suffix == nullptr) {
        // Not our addon — passthrough.
        return FileRead_o(unused, path, outBuf, outSize, extraBytes, flag1, flag2);
    }

    DecideSource();

    if (g_source == Source::Disk) {
        // Disk is dev-marked or at least as new — serve it. If disk lacks this
        // specific file (embedded may carry files disk doesn't), fall through.
        const int diskResult =
            FileRead_o(unused, path, outBuf, outSize, extraBytes, flag1, flag2);
        if (diskResult != 0) return diskResult;
    }

    // Source::Embedded, or Source::Disk with this file missing.
    const auto *entry = LookupEmbedded(suffix);
    if (entry == nullptr) {
        // Not in the embedded set either — let the engine try disk once more.
        return FileRead_o(unused, path, outBuf, outSize, extraBytes, flag1, flag2);
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

// `FUN_ADDON_DISK_SCAN` — `void __cdecl(void)`, the login disk scan called from
// FUN_ADDON_INIT after registry setup. PRE-hooked: register the embedded addon
// BEFORE the scan runs so it lands at the HEAD of the load-order list (the scan
// appends disk addons after it) and therefore loads FIRST — before any addon
// that consumes its globals (e.g. EventRegistry). A post-scan registration
// would append at the tail and load last. Dedup-safe, so a disk copy the scan
// also finds is a no-op. Registering here (not post-INIT) is the load-order fix.
using DiskScanFn = void(__cdecl *)();
DiskScanFn DiskScan_o = nullptr;

void __cdecl DiskScan_h() {
    RegisterAddonToc(kAddonName);
    HideEmbeddedEntry();
    DiskScan_o();
}

// `FUN_ADDON_LOADADDON` — `uint32 __cdecl(char *name, uint32 flags, int *ctx)`.
// PRE-hooked to mark the embedded addon SECURE right before it loads. The
// engine derives the taint stamped on an addon's Lua chunks/closures from the
// entry's security level (entry+0x24) at the top of this call; a nonzero level
// taints our library closures, and the engine then BLOCKS them from protected
// actions (e.g. the game menu on Escape). Setting it to 0 loads our code
// untainted — the same path Blizzard's own secure addons take. Applied here
// (not once at registration) so it lands right before each load — login and
// every /reload — and can't be raced by any security re-resolution.
using LoadAddOnFn = uint32_t(__cdecl *)(char *name, uint32_t flags, int *ctx);
LoadAddOnFn LoadAddOn_o = nullptr;

// True while OUR addon's LoadAddOn call is on the stack — the window in which
// the fatal-error hook below swallows the digest-mismatch error. Main-thread
// only (all addon loading is), so a plain bool suffices; still marked volatile
// against the detour boundary. No re-entrancy concern: the flag only guards
// our own (dependency-free) load.
volatile bool g_loadingEmbedded = false;

uint32_t __cdecl LoadAddOn_h(char *name, uint32_t flags, int *ctx) {
    const bool ours = name != nullptr && std::strcmp(name, kAddonName) == 0;
    if (ours) {
        auto Lookup = reinterpret_cast<AddonLookupFn>(Offsets::FUN_ADDON_HASH_LOOKUP);
        uint8_t *entry =
            Lookup(reinterpret_cast<void *>(Offsets::VAR_ADDON_NAME_HASH), name);
        if (entry != nullptr)
            *reinterpret_cast<uint32_t *>(
                entry + Offsets::OFF_ADDON_ENTRY_SECURITY) = 0;
        g_loadingEmbedded = true;
    }
    const uint32_t result = LoadAddOn_o(name, flags, ctx);
    if (ours)
        g_loadingEmbedded = false;
    return result;
}

// Swallow the SECURE-addon digest-mismatch fatal (code 10, "interface files
// are corrupt") while OUR addon loads: the engine digest-verifies secure
// entries against a signed digest at entry+0x1D2, which a parser-registered
// embedded addon can never carry. Scoped to the load window via the flag so
// genuine corruption elsewhere still terminates. Every other code passes
// through untouched.
using FatalErrorFn = void(__cdecl *)(uint32_t code);
FatalErrorFn FatalError_o = nullptr;

void __cdecl FatalError_h(uint32_t code) {
    if (code == 10 && g_loadingEmbedded)
        return;
    FatalError_o(code);
}

const Game::HookAutoRegister _hookFileRead{
    Offsets::FUN_FILE_READ, reinterpret_cast<void *>(&FileRead_h),
    reinterpret_cast<void **>(&FileRead_o)};

const Game::HookAutoRegister _hookDiskScan{
    Offsets::FUN_ADDON_DISK_SCAN, reinterpret_cast<void *>(&DiskScan_h),
    reinterpret_cast<void **>(&DiskScan_o)};

const Game::HookAutoRegister _hookLoadAddOn{
    Offsets::FUN_ADDON_LOADADDON, reinterpret_cast<void *>(&LoadAddOn_h),
    reinterpret_cast<void **>(&LoadAddOn_o)};

const Game::HookAutoRegister _hookFatalError{
    Offsets::FUN_FATAL_ERROR, reinterpret_cast<void *>(&FatalError_h),
    reinterpret_cast<void **>(&FatalError_o)};

} // namespace

} // namespace Addons::Embedded
