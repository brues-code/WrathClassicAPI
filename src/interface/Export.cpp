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

// Two developer-console commands (the `~` console, available when the client
// is launched with `-console`) that dump Blizzard's own data out of the
// mounted MPQ archives onto disk, relative to the client's working directory:
//
//   ExportInterfaceFiles code  -> .lua/.xml/.toc/.xsd -> BlizzardInterfaceCode\
//   ExportInterfaceFiles art   -> .blp/.tga           -> BlizzardInterfaceArt\
//   ExportDBCFiles             -> .dbc                 -> DBFilesClient\
//
// Each writes a "wrote N file(s)" line back to the console on completion.
//
// How it works: enumerate the mounted archives' `(listfile)` (the flat,
// full-path file index) under a path prefix via FUN_MPQ_ENUM_FILES, collect
// matching paths (deduped across archives), read each via FUN_FILE_READ, and
// write it to disk with Win32. The Interface export INCLUDES
// `Interface\AddOns\` — the listfile only indexes MPQ-baked files, so those
// are Blizzard's own UI addons (Blizzard_AuctionUI, ...), part of the stock
// UI source. Loose on-disk addons live in no archive's listfile, so they're
// never enumerated.
//
// ExportDBCFiles additionally UNIONS the listfile with a `.text` scan for the
// DBC path-getter pattern (`mov eax, &"DBFilesClient\X.dbc"; ret`) — the
// authoritative "what does this build load" list, catching DBCs the listfile
// doesn't index. 3.3.5 uses the same `B8 imm32 C3` getter shape as 1.12.
//
// Registered from the login-screen (glue) bootstrap so the commands exist at
// the login `~` console — the point of an export tool is to run it before
// entering the world. Console commands are process-global, so one registration
// covers the login screen and in-world alike; the registrar dedups by name.

#include "Game.h"
#include "Offsets.h"
#include "addons/EngineIO.h"

#include <windows.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <unordered_set>
#include <vector>

namespace Interface::Export {

namespace {

// MPQ listfile enumerator + its callback — all __cdecl, path on the stack.
using MpqEnumCb_t = int(__cdecl *)(const char *fullPath, void *userParam);
using MpqEnumFiles_t = void(__cdecl *)(const char *pathPrefix, MpqEnumCb_t cb,
                                       void *userParam);

// Console command registrar + its handler, and console output.
using CommandHandler_t = int(__cdecl *)(void *unused, const char *args);
using RegisterCommand_t = int(__cdecl *)(const char *name,
                                         CommandHandler_t handler, int category,
                                         const char *help);
using ConsoleWrite_t = void(__cdecl *)(const char *line, int category);

void ConsoleWrite(const char *line) {
    reinterpret_cast<ConsoleWrite_t>(
        static_cast<uintptr_t>(Offsets::FUN_CONSOLE_WRITE))(line, 0);
}

// Case-insensitive ASCII equality.
bool EqualsCI(const char *a, const char *b) {
    for (;; ++a, ++b) {
        char ca = *a, cb = *b;
        if (ca >= 'A' && ca <= 'Z') ca = static_cast<char>(ca + ('a' - 'A'));
        if (cb >= 'A' && cb <= 'Z') cb = static_cast<char>(cb + ('a' - 'A'));
        if (ca != cb) return false;
        if (ca == '\0') return true;
    }
}

bool HasExtension(const char *name, const char *const *exts, int count) {
    const char *dot = nullptr;
    for (const char *p = name; *p; ++p)
        if (*p == '.') dot = p;
    if (dot == nullptr) return false;
    for (int i = 0; i < count; ++i)
        if (EqualsCI(dot + 1, exts[i])) return true;
    return false;
}

std::string LowerCopy(const char *s) {
    std::string out;
    for (const char *p = s; *p; ++p) {
        char c = *p;
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c + ('a' - 'A'));
        out.push_back(c);
    }
    return out;
}

// Create every parent directory of `relPath` (backslash-separated). Existing
// dirs are a no-op.
void EnsureParentDirs(const std::string &relPath) {
    std::string accum;
    for (char c : relPath) {
        if (c == '\\' || c == '/') {
            if (!accum.empty())
                CreateDirectoryA(accum.c_str(), nullptr);
            accum.push_back('\\');
        } else {
            accum.push_back(c);
        }
    }
}

bool WriteFileToDisk(const std::string &relPath, const void *data,
                     unsigned int size) {
    EnsureParentDirs(relPath);
    HANDLE h = CreateFileA(relPath.c_str(), GENERIC_WRITE, 0, nullptr,
                           CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE)
        return false;
    bool ok = true;
    if (size != 0) {
        DWORD written = 0;
        ok = WriteFile(h, data, size, &written, nullptr) != 0 && written == size;
    }
    CloseHandle(h);
    return ok;
}

const char *const kCodeExts[] = {"lua", "xml", "toc", "xsd"};
const char *const kArtExts[] = {"blp", "tga"};

// Collector for the enumeration callback. File-static because the enumerator
// hands the callback only the path (+ our userParam); enumeration is
// synchronous and single-threaded, so a file-static sink is safe.
struct Collector {
    const char *const *exts = nullptr;
    int extCount = 0;
    std::vector<std::string> files;
    std::unordered_set<std::string> seen; // lowercased keys — dedup across archives

    void Add(const char *path) {
        if (seen.insert(LowerCopy(path)).second)
            files.emplace_back(path);
    }
};

int __cdecl CollectCb(const char *fullPath, void *userParam) {
    auto *c = static_cast<Collector *>(userParam);
    if (c != nullptr && fullPath != nullptr &&
        HasExtension(fullPath, c->exts, c->extCount))
        c->Add(fullPath);
    return 1; // 0 would stop enumeration
}

// Read each collected source path from the MPQs and write it under `dstRoot`,
// re-rooted by stripping the leading `prefixLen` chars ("Interface\").
// Returns the number written.
int WriteFiles(const std::vector<std::string> &files, size_t prefixLen,
               const char *dstRoot) {
    auto FileRead = reinterpret_cast<Addons::EngineIO::FileReadFn>(
        static_cast<uintptr_t>(Offsets::FUN_FILE_READ));
    auto SMemFree = reinterpret_cast<Addons::EngineIO::SMemFreeFn>(
        static_cast<uintptr_t>(Offsets::FUN_STORM_SMEM_FREE));

    int written = 0;
    for (const std::string &src : files) {
        void *buf = nullptr;
        size_t size = 0;
        if (FileRead(0, src.c_str(), &buf, &size, 0, 1, 0) == 0 || buf == nullptr)
            continue;

        // Re-root: "Interface\Sub\Foo.ext" -> "<dstRoot>\Sub\Foo.ext".
        std::string dst = dstRoot;
        dst.push_back('\\');
        dst.append(src, prefixLen, std::string::npos);

        if (WriteFileToDisk(dst, buf, static_cast<unsigned int>(size)))
            ++written;

        SMemFree(buf, __FILE__, __LINE__, 0);
    }
    return written;
}

// Enumerate every MPQ file under `Interface\` matching `exts`, read each, and
// write it under `dstRoot`. Returns the number written.
int ExportTree(const char *const *exts, int extCount, const char *dstRoot) {
    static const char kPrefix[] = "Interface\\";
    Collector collector;
    collector.exts = exts;
    collector.extCount = extCount;

    reinterpret_cast<MpqEnumFiles_t>(
        static_cast<uintptr_t>(Offsets::FUN_MPQ_ENUM_FILES))(kPrefix, &CollectCb,
                                                             &collector);

    return WriteFiles(collector.files, sizeof(kPrefix) - 1, dstRoot);
}

// --- DBC path-getter scan -------------------------------------------------
//
// The DBC loaders aren't driven by any iterable name table — each has a
// one-instruction path-getter `mov eax, &"DBFilesClient\X.dbc"; ret`
// (`B8 imm32 C3`), verified in this build (e.g. FUN_008A65D0 returns
// &"DBFilesClient\Spell.dbc"). Scanning `.text` for that pattern recovers the
// authoritative set of DBCs the client loads, including ones the MPQ listfile
// doesn't index. Section bounds from the PE headers (list_segments):
//   .text  0x00401000..0x009DE3FF  — where the getters live
//   .rdata 0x009DF000..0x00AB57FF  — where the getter strings live
// Bounding the string pointer to .rdata (compiler puts string literals there)
// keeps the suffix check off unmapped memory and out of the inter-section gaps.

const char *const kDbcExts[] = {"dbc"};

constexpr uintptr_t kTextStart = 0x00401000;
constexpr uintptr_t kTextEnd = 0x009DE400;
constexpr uintptr_t kRdataStart = 0x009DF000;
constexpr uintptr_t kRdataEnd = 0x00AB5800;

// True if `s` (bounded to .rdata, never reading past it) names a ".dbc" file.
bool LooksLikeDbcString(const uint8_t *s) {
    const uint8_t *limit = reinterpret_cast<const uint8_t *>(kRdataEnd);
    size_t n = 0;
    while (n < 260 && s + n < limit && s[n] != '\0')
        ++n;
    if (s + n >= limit || s[n] != '\0' || n < 4) // not terminated, or too short
        return false;
    return EqualsCI(reinterpret_cast<const char *>(s + n - 4), ".dbc");
}

// Scan .text for the DBC path-getter pattern and add each referenced ".dbc"
// path to the collector. Synchronous; reads only mapped .text / .rdata.
void ScanPathGetters(Collector &c) {
    const uint8_t *p = reinterpret_cast<const uint8_t *>(kTextStart);
    const uint8_t *end = reinterpret_cast<const uint8_t *>(kTextEnd) - 6;
    for (; p <= end; ++p) {
        if (p[0] != 0xB8 || p[5] != 0xC3) // mov eax, imm32 ; ret
            continue;
        uint32_t imm;
        std::memcpy(&imm, p + 1, sizeof(imm));
        if (imm < kRdataStart || imm >= kRdataEnd)
            continue;
        const uint8_t *s = reinterpret_cast<const uint8_t *>(imm);
        if (LooksLikeDbcString(s))
            c.Add(reinterpret_cast<const char *>(s));
    }
}

// Union the MPQ (listfile) under DBFilesClient\ with the path-getter scan
// (deduped case-insensitively), read each, and write under `dstRoot`.
int ExportDBC(const char *dstRoot) {
    static const char kPrefix[] = "DBFilesClient\\";
    Collector collector;
    collector.exts = kDbcExts;
    collector.extCount = 1;

    reinterpret_cast<MpqEnumFiles_t>(
        static_cast<uintptr_t>(Offsets::FUN_MPQ_ENUM_FILES))(kPrefix, &CollectCb,
                                                             &collector);
    ScanPathGetters(collector);

    return WriteFiles(collector.files, sizeof(kPrefix) - 1, dstRoot);
}

enum Mode { MODE_CODE, MODE_ART };

// Case-insensitive match of the first whitespace-delimited token of `s`.
bool FirstTokenEquals(const char *s, const char *token) {
    size_t i = 0;
    for (; token[i] != '\0'; ++i) {
        char a = s[i];
        if (a >= 'A' && a <= 'Z') a = static_cast<char>(a + ('a' - 'A'));
        if (a != token[i]) return false;
    }
    const char end = s[i];
    return end == '\0' || end == ' ' || end == '\t';
}

int __cdecl Console_ExportInterfaceFiles(void * /*unused*/, const char *args) {
    const char *p = (args != nullptr) ? args : "";
    while (*p == ' ' || *p == '\t') ++p;

    Mode mode;
    if (FirstTokenEquals(p, "art"))
        mode = MODE_ART;
    else if (FirstTokenEquals(p, "code"))
        mode = MODE_CODE;
    else {
        ConsoleWrite("Usage: ExportInterfaceFiles art|code");
        return 1;
    }

    const char *dstRoot =
        (mode == MODE_ART) ? "BlizzardInterfaceArt" : "BlizzardInterfaceCode";
    const int written = ExportTree((mode == MODE_ART) ? kArtExts : kCodeExts,
                                   (mode == MODE_ART) ? 2 : 4, dstRoot);

    char msg[160];
    std::snprintf(msg, sizeof(msg),
                  "ExportInterfaceFiles: wrote %d %s file(s) to %s\\", written,
                  (mode == MODE_ART) ? "art" : "code", dstRoot);
    ConsoleWrite(msg);
    return 1;
}

int __cdecl Console_ExportDBCFiles(void * /*unused*/, const char * /*args*/) {
    const int written = ExportDBC("DBFilesClient");
    char msg[96];
    std::snprintf(msg, sizeof(msg),
                  "ExportDBCFiles: wrote %d .dbc file(s) to DBFilesClient\\",
                  written);
    ConsoleWrite(msg);
    return 1;
}

void RegisterCmd(const char *name, CommandHandler_t handler, const char *help) {
    reinterpret_cast<RegisterCommand_t>(
        static_cast<uintptr_t>(Offsets::FUN_CONSOLE_REGISTER_COMMAND))(
        name, handler, Offsets::CONSOLE_CATEGORY_DEBUG, help);
}

// Registered from the login-screen (glue) bootstrap so the commands exist at
// the login `~` console — the point of an export tool is to run it before
// entering the world. Console commands are process-global, so this one
// registration covers the login screen and in-world alike. Names and help are
// stored by pointer, so they must be static — literals satisfy that.
void Register() {
    RegisterCmd("ExportInterfaceFiles", &Console_ExportInterfaceFiles,
                "Extracts Blizzard's UI files from the MPQs to disk. "
                "Usage: ExportInterfaceFiles art|code");
    RegisterCmd("ExportDBCFiles", &Console_ExportDBCFiles,
                "Extracts the client's .dbc tables from the MPQs to disk. "
                "Usage: ExportDBCFiles");
}

const Game::GlueModuleAutoRegister _autoreg{&Register};

} // namespace

} // namespace Interface::Export
