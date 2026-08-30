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

// Retail-like `/reload`: pick up new addon folders and new files without
// restarting the client. Ported from ClassicAPI's `src/addons/Rescan.cpp`,
// re-derived for 3.3.5 offsets/ABIs. Three engine limitations block this on a
// stock client, each fixed with the engine's own machinery:
//
//   1. FILE VISIBILITY. Every relative-path read resolves through a loose-file
//      hash index built ONCE at boot; files created after boot — a new addon's
//      TOC, a new .lua in an existing addon, a freshly written SavedVariables
//      file — are invisible until restart. The boot root indexer
//      (`FUN_VFS_INDEX_ROOT`) is dedup-safe, so we re-run it per /reload on the
//      game-dir root (which covers both `Interface\AddOns` and `WTF\Account`).
//      New files in EXISTING addons then need nothing more — the per-addon
//      loader re-reads the TOC from disk each load pass, so once visible they
//      load. (3.3.5's file layer is push-style — no find-open/close handle
//      pair — so we replay the whole root walk rather than a targeted subtree;
//      a narrower subtree reindex would need to replicate the engine's private
//      walk-context struct, which isn't worth the fragility.)
//
//   2. REGISTRY MEMBERSHIP. The addon registry is built once at login; new
//      folders are never walked. We replay the login scan's disk walk verbatim
//      (`FUN_ADDON_SCAN_DISK_DIRS` with the engine's own per-directory
//      callback, which feeds the dedup-safe TOC parser), so new folders
//      register as normal entries — then mirror the two registry structures
//      the scan's other passes fill: the reverse-LoadWith lists and the flat
//      `GetNumAddOns` display array.
//
//   3. METADATA IMMUTABILITY. The parser's dedup guard makes a registered
//      entry's `##` metadata read-once. The engine's own complete per-entry
//      destructor (`FUN_ADDON_ENTRY_DESTROY` — frees every owned allocation and
//      self-unlinks from both the list and the hash) makes evict + re-register
//      safe: destroy the entry so the dedup guard MISSES, re-feed the name to
//      the parser, and it rebuilds from the edited TOC. Gated on an actual `##`
//      change (hash of the TOC's `##` lines only — file-reference edits load
//      natively and never trigger the evict path). A TOC that no longer reads
//      (folder deleted) evicts without re-registering.
//
// Runs from `ModuleAutoRegister` — WrathClassicAPI's in-game bootstrap
// (`UIBindings::Initialize`), which fires BEFORE FrameXML.toc / addons load on
// both login and every /reload. The engine then loads new entries natively.
// The window is also what makes eviction safe: the unload pass already ran
// (SavedVariables written from the OLD entry, loaded bytes cleared) and the
// load pass hasn't — nothing holds entry state.
//
// Excluded from the evict path: `!!!WrathClassicAPI` (embedded; `Addons::
// Embedded` owns its entry) and `## Secure:` / SMSG-managed entries (the parser
// cannot restore packet-delivered state).
//
// NOTE the surgical discipline (ClassicAPI's hard-won lesson): never run the
// login-only teardown+rescan (`FUN_ADDON_INIT`) mid-/reload — it corrupts the
// registry. This module never runs bulk teardown; the only entry mutation
// beyond what a login scan produces is the per-entry evict above, which is (a)
// the engine's own complete destructor, (b) always preceded by the
// reverse-LoadWith scrub (the one reference the dtor can't clean), and (c)
// always followed by the same fix-ups a new registration gets.

#include "Game.h"
#include "Offsets.h"
#include "addons/EngineIO.h"
#include "addons/Registry.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <unordered_map>
#include <vector>

namespace Addons::Rescan {

namespace {

// ── Engine entry points (3.3.5 ABIs; see Offsets.h) ──
using ScanDiskDirs_t = int(__cdecl *)(const char *basePath, const void *pattern,
                                      void *callback, void *userParam,
                                      int includeHidden);
using EntryDestroy_t = void(__stdcall *)(void *entry);
using HashLookup_t = uint8_t *(__thiscall *)(void *nameHash, const char *name);
using DescGrow_t = void(__thiscall *)(void *desc, uint32_t newCap);
using QuantumCalc_t = uint32_t(__thiscall *)(void *desc, uint32_t needed);
using Qsort_t = void(__cdecl *)(void *base, uint32_t num, uint32_t width,
                                void *compare);
using IndexRoot_t = void(__cdecl *)(const char *basePath);
using IsDir_t = int(__cdecl *)(const char *path);

// The engine's ubiquitous growable-array descriptor.
struct Desc {
    uint32_t cap;
    uint32_t count;
    uint32_t *data;
    uint32_t quantum;
};

// `FUN_TOC_PARSER` takes its `const char *name` in EAX (see Embedded.cpp).
void CallTocParser(const char *name) {
    const uint32_t fn = Offsets::FUN_TOC_PARSER;
    __asm {
        mov eax, name
        call fn
    }
}

// Append one dword to a descriptor, mirroring the engine's inline
// grow-and-append: round the needed cap up to the quantum — computed by the
// engine's own quantum calc when the desc has none — grow via the site's grow
// instantiation (which reallocs `data` and writes `cap`), then append.
void DescAppend(Desc *desc, uint32_t value, DescGrow_t grow) {
    uint32_t needed = desc->count + 1;
    if (desc->cap < needed) {
        uint32_t quantum = desc->quantum;
        if (quantum == 0) {
            auto calc =
                reinterpret_cast<QuantumCalc_t>(Offsets::FUN_DESC_QUANTUM_CALC);
            quantum = calc(desc, needed);
        }
        if (quantum != 0 && needed % quantum != 0)
            needed += quantum - needed % quantum;
        grow(desc, needed);
    }
    desc->data[desc->count++] = value;
}

// By-name registry lookup via the engine's own hash resolver. Unlike 1.12,
// 3.3.5's `FUN_ADDON_HASH_LOOKUP` returns the entry base directly (no desc
// offset to subtract).
uintptr_t ResolveEntryByName(const char *name) {
    auto lookup = reinterpret_cast<HashLookup_t>(Offsets::FUN_ADDON_HASH_LOOKUP);
    return reinterpret_cast<uintptr_t>(
        lookup(reinterpret_cast<void *>(Offsets::VAR_ADDON_NAME_HASH), name));
}

// ── Step 1: loose-file index refresh ──────────────────────────────────

// Re-run the boot root indexer on the game-dir root. Dedup-safe (the indexer
// skips every file already present), so this only registers genuinely new
// files. Base choice mirrors the boot walk: the recorded game dir if it opens
// as a directory, else ".". Keys are relative to the base, which is why we
// index the whole root (a subtree would need base-relative keys the engine's
// private walk-context computes).
void RefreshLooseFileIndex() {
    auto isDir = reinterpret_cast<IsDir_t>(Offsets::FUN_VFS_IS_DIR);
    auto indexRoot = reinterpret_cast<IndexRoot_t>(Offsets::FUN_VFS_INDEX_ROOT);
    const char *base = reinterpret_cast<const char *>(Offsets::VAR_VFS_BASE_PATH);
    if (!isDir(base))
        base = ".";
    indexRoot(base);
}

// ── Step 1.5: `##` metadata refresh (evict + re-register on change) ──

// The embedded addon's name — its entry is owned by `Addons::Embedded` (head
// re-link + filter byte) and never refreshed here.
constexpr const char *kEmbeddedAddon = "!!!WrathClassicAPI";

// name → FNV-1a hash of the addon's `##` lines, as last parsed. Per-process on
// purpose: the registry persists across /reload, and a stale hash after a full
// re-login scan only causes one harmless re-parse of already-current metadata.
std::unordered_map<std::string, uint32_t> g_metaHash;

// Read `<name>`'s base TOC exactly as the parser does — through the (hooked)
// FUN_FILE_READ. Caller frees `*buf` via Storm.
bool ReadToc(const char *name, void **buf, size_t *size) {
    char path[260];
    std::snprintf(path, sizeof(path), "Interface\\AddOns\\%s\\%s.toc", name, name);
    auto read = reinterpret_cast<Addons::EngineIO::FileReadFn>(
        static_cast<uintptr_t>(Offsets::FUN_FILE_READ));
    *buf = nullptr;
    *size = 0;
    return read(0, path, buf, size, 1, 1, 0) != 0 && *buf != nullptr;
}

// FNV-1a over the TOC's `##` lines only. File-reference lines are excluded on
// purpose — the load pass re-reads those every /reload anyway. Line terminators
// are excluded (CRLF↔LF is not a metadata change); a per-line separator keeps
// adjacent lines from concatenating. The BOM skip mirrors the parser.
uint32_t HashTocMetadata(const char *buf, size_t size) {
    uint32_t h = 2166136261u;
    size_t i = 0;
    if (size >= 3 && static_cast<uint8_t>(buf[0]) == 0xEF &&
        static_cast<uint8_t>(buf[1]) == 0xBB &&
        static_cast<uint8_t>(buf[2]) == 0xBF)
        i = 3;
    while (i < size) {
        size_t end = i;
        while (end < size && buf[end] != '\n')
            ++end;
        size_t stop = end;
        while (stop > i && buf[stop - 1] == '\r')
            --stop;
        if (stop - i >= 2 && buf[i] == '#' && buf[i + 1] == '#') {
            for (size_t k = i; k < stop; ++k) {
                h ^= static_cast<uint8_t>(buf[k]);
                h *= 16777619u;
            }
            h ^= '\n';
            h *= 16777619u;
        }
        i = end + 1;
    }
    return h;
}

// True when the entry never takes the evict path: the embedded addon and
// SMSG-managed secure entries.
bool IsRefreshExempt(uintptr_t entry, const char *name) {
    return name == nullptr || std::strcmp(name, kEmbeddedAddon) == 0 ||
           *reinterpret_cast<const uint8_t *>(
               entry + Offsets::OFF_ADDON_ENTRY_SECURE) != 0;
}

// Read + hash `<name>`'s `##` lines. False when the TOC doesn't read.
bool TryHashToc(const char *name, uint32_t *outHash) {
    void *buf = nullptr;
    size_t size = 0;
    if (!ReadToc(name, &buf, &size))
        return false;
    *outHash = HashTocMetadata(static_cast<const char *>(buf), size);
    reinterpret_cast<Addons::EngineIO::SMemFreeFn>(
        static_cast<uintptr_t>(Offsets::FUN_STORM_SMEM_FREE))(buf, __FILE__,
                                                              __LINE__, 0);
    return true;
}

// Remove every pointer to `victim` from the OTHER entries' reverse-LoadWith
// lists — the one reference the destructor cannot clean. Without this, the
// loader's post-ADDON_LOADED loop dereferences freed memory.
void ScrubReverseLoadWith(uintptr_t victim) {
    ForEachEntry([victim](uintptr_t entry) {
        if (entry == victim)
            return;
        auto *desc = reinterpret_cast<Desc *>(
            entry + Offsets::OFF_ADDON_REVLOADWITH_DESC);
        uint32_t w = 0;
        for (uint32_t r = 0; r < desc->count; ++r)
            if (desc->data[r] != static_cast<uint32_t>(victim))
                desc->data[w++] = desc->data[r];
        desc->count = w;
    });
}

// The refresh pass. Walks the registry comparing each entry's current `##` hash
// against the last-parsed one; on change (or an unreadable TOC — deleted
// folder), evicts via the engine's destructor and, for changes, re-feeds the
// name to the parser. Victims are collected during the walk and processed after
// — the destructor unlinks the entry the iterator stands on. Returns true when
// anything was evicted.
bool RefreshChangedMetadata(std::vector<uintptr_t> &refreshed) {
    struct Victim {
        std::string name;
        uint32_t newHash;
        bool reRegister; // false: TOC unreadable — evict only
    };
    std::vector<Victim> victims;
    ForEachEntry([&victims](uintptr_t entry) {
        const char *name = *reinterpret_cast<const char *const *>(
            entry + Offsets::OFF_ADDON_ENTRY_NAME_PTR);
        if (IsRefreshExempt(entry, name))
            return;
        uint32_t h = 0;
        if (!TryHashToc(name, &h)) {
            victims.push_back({name, 0, false});
            return;
        }
        auto it = g_metaHash.find(name);
        if (it == g_metaHash.end())
            g_metaHash.emplace(name, h); // first sighting — seed only
        else if (it->second != h)
            victims.push_back({name, h, true});
    });

    bool anyEvicted = false;
    auto destroy =
        reinterpret_cast<EntryDestroy_t>(Offsets::FUN_ADDON_ENTRY_DESTROY);
    for (const Victim &v : victims) {
        const uintptr_t entry = ResolveEntryByName(v.name.c_str());
        if (entry == 0)
            continue;
        ScrubReverseLoadWith(entry);
        destroy(reinterpret_cast<void *>(entry));
        anyEvicted = true;
        if (!v.reRegister) {
            g_metaHash.erase(v.name);
            continue;
        }
        CallTocParser(v.name.c_str());
        const uintptr_t fresh = ResolveEntryByName(v.name.c_str());
        if (fresh != 0) {
            refreshed.push_back(fresh);
            g_metaHash[v.name] = v.newHash;
        } else {
            // Evicted but the parse didn't restore it (TOC became unreadable
            // between hash and parse) — treat as deleted.
            g_metaHash.erase(v.name);
        }
    }
    return anyEvicted;
}

// ── Step 2: registry rescan + mirrored fix-ups ────────────────────────

// The scan tail loop's reverse-LoadWith build, restricted to pairs involving a
// newly registered entry (login already linked old→old pairs; a pair is missing
// exactly when one side is new).
void FixupReverseLoadWith(const std::vector<uintptr_t> &added) {
    auto isNew = [&added](uintptr_t e) {
        return std::find(added.begin(), added.end(), e) != added.end();
    };
    auto grow =
        reinterpret_cast<DescGrow_t>(Offsets::FUN_ADDON_REVLOADWITH_GROW);
    ForEachEntry([&](uintptr_t entry) {
        const bool entryIsNew = isNew(entry);
        const uint32_t count = *reinterpret_cast<const uint32_t *>(
            entry + Offsets::OFF_ADDON_LOADWITH_COUNT);
        auto names = *reinterpret_cast<const char *const *const *>(
            entry + Offsets::OFF_ADDON_LOADWITH_ARRAY);
        for (uint32_t i = 0; i < count; ++i) {
            const uintptr_t target = ResolveEntryByName(names[i]);
            if (target == 0 || (!entryIsNew && !isNew(target)))
                continue;
            DescAppend(reinterpret_cast<Desc *>(
                           target + Offsets::OFF_ADDON_REVLOADWITH_DESC),
                       static_cast<uint32_t>(entry), grow);
        }
    });
}

// Rebuild the `GetNumAddOns`/`GetAddOnInfo(i)` name-pointer array from the
// linked list, skipping filtered entries (keeps `!!!WrathClassicAPI` hidden),
// then sort with the engine's own comparator to restore alphabetical order.
void RebuildDisplayArray() {
    auto desc = reinterpret_cast<Desc *>(
        static_cast<uintptr_t>(Offsets::VAR_ADDON_ARRAY_CAP));
    auto grow = reinterpret_cast<DescGrow_t>(Offsets::FUN_ADDON_ARRAY_GROW);
    desc->count = 0;
    ForEachEntry([&](uintptr_t entry) {
        if (*reinterpret_cast<const uint8_t *>(
                entry + Offsets::OFF_ADDON_ENTRY_FILTER_OUT) != 0)
            return;
        DescAppend(desc,
                   *reinterpret_cast<const uint32_t *>(
                       entry + Offsets::OFF_ADDON_ENTRY_NAME_PTR),
                   grow);
    });
    auto qsort = reinterpret_cast<Qsort_t>(Offsets::FUN_CRT_QSORT);
    qsort(desc->data, desc->count, 4,
          reinterpret_cast<void *>(Offsets::FUN_ADDON_NAME_COMPARE));
}

void Run() {
    // Registry populated (login scan ran) and loose index built — both always
    // true by the first in-world bootstrap, but these are the states the steps
    // below mutate, so gate explicitly.
    if (*reinterpret_cast<const uint8_t *>(
            static_cast<uintptr_t>(Offsets::VAR_ADDON_INITIALIZED)) == 0 ||
        *reinterpret_cast<const uint8_t *>(
            static_cast<uintptr_t>(Offsets::VAR_VFS_INDEX_READY)) == 0)
        return;

    RefreshLooseFileIndex();

    // Metadata refresh runs BEFORE the snapshot: a re-registered entry is alive
    // by snapshot time, so it lands in `before` and can never double-count in
    // `added`.
    std::vector<uintptr_t> refreshed;
    const bool evicted = RefreshChangedMetadata(refreshed);

    std::vector<uintptr_t> before;
    ForEachEntry([&before](uintptr_t entry) { before.push_back(entry); });

    // Replay login scan walk verbatim: engine walker + engine callback + engine
    // parser. The parser's dedup guard makes this a hash lookup per already-
    // registered addon; new folders register as complete, normal entries.
    auto scan =
        reinterpret_cast<ScanDiskDirs_t>(Offsets::FUN_ADDON_SCAN_DISK_DIRS);
    scan(reinterpret_cast<const char *>(Offsets::VAR_ADDON_PATH_PREFIX),
         reinterpret_cast<const void *>(Offsets::VAR_ADDON_SCAN_PATTERN),
         reinterpret_cast<void *>(Offsets::FUN_ADDON_DISK_DIR_CB),
         /*userParam=*/nullptr, /*includeHidden=*/0);

    std::vector<uintptr_t> added;
    ForEachEntry([&](uintptr_t entry) {
        if (std::find(before.begin(), before.end(), entry) == before.end())
            added.push_back(entry);
    });

    // Seed the metadata map for entries this reload registered, so the next
    // reload diffs a `##` edit against what was actually parsed.
    for (uintptr_t entry : added) {
        const char *name = *reinterpret_cast<const char *const *>(
            entry + Offsets::OFF_ADDON_ENTRY_NAME_PTR);
        uint32_t h = 0;
        if (!IsRefreshExempt(entry, name) && TryHashToc(name, &h))
            g_metaHash[name] = h;
    }

    if (added.empty() && refreshed.empty() && !evicted)
        return;

    // Refreshed entries need the same link fix-ups a new folder gets.
    std::vector<uintptr_t> linked = added;
    linked.insert(linked.end(), refreshed.begin(), refreshed.end());
    if (!linked.empty())
        FixupReverseLoadWith(linked);

    // After any eviction the display array still holds a freed name pointer.
    RebuildDisplayArray();
}

const Game::ModuleAutoRegister _autoreg{&Run};

} // namespace

} // namespace Addons::Rescan
