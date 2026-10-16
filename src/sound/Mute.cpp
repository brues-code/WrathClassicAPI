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

// `MuteSoundFile(path)` / `UnmuteSoundFile(path)` — stop one sound file from
// playing — and `C_Sound.GetRecentSoundFiles()`, which reports what played
// lately so a caller can find the path to mute. Ported from ClassicAPI's
// src/sound/Mute.cpp.
//
// Blizzard's pair takes a file id or a path; only the path is meaningful
// here, since file ids are a later scheme. The path is the engine's own, e.g.
// "Sound\Creature\Illidan\BLACK_Illidan_04.wav".
//
// One hook on SE2::CreateWaveData, where every file sound opens (both
// SESound::Create2D and Create3D call it). A muted path returns 0 — "didn't
// open" — which every caller already handles: SI2::PlaySoundKit reports it
// as no file (16) and plays nothing. Suppressing the open rather than muting
// the opened sound means nothing is decoded for a sound nobody hears.
//
// The recent list is always on: the hook exists for muting anyway, and one
// bounded copy is nothing next to opening, decoding and starting a sound.
// Muted entries stay in the list, flagged, so a muted sound can still be
// found to un-mute.

#include "Game.h"
#include "Offsets.h"

#include <cstdint>
#include <cstring>
#include <string>
#include <unordered_set>

namespace Sound::Mute {

namespace {

constexpr size_t kMaxPath = 260;
constexpr int kRecentSlots = 64;

struct Recent {
    char path[kMaxPath];
    uint32_t seq;    // insertion order; 0 = slot never used
    uint32_t timeMs; // OsGetAsyncTimeMs, GetTime()'s clock
    bool muted;
};

Recent g_recent[kRecentSlots];
uint32_t g_seq = 0;

// Muted paths, normalized. Empty in the common case.
std::unordered_set<std::string> g_muted;

char Fold(char c) {
    if (c == '/')
        return '\\';
    if (c >= 'A' && c <= 'Z')
        return static_cast<char>(c + ('a' - 'A'));
    return c;
}

// Lowercase with '/' folded to '\', so a caller may use either separator and
// any case — the engine's own path compares are case-insensitive.
std::string Normalize(const char *path) {
    std::string out;
    for (const char *p = path; *p != '\0'; ++p)
        out.push_back(Fold(*p));
    return out;
}

bool IsMuted(const char *path) {
    return !g_muted.empty() && g_muted.find(Normalize(path)) != g_muted.end();
}

bool SamePath(const char *a, const char *b) {
    for (;; ++a, ++b) {
        const char ca = Fold(*a);
        if (ca != Fold(*b))
            return false;
        if (ca == '\0')
            return true;
    }
}

uint32_t NowMs() {
    using OsGetAsyncTimeMs_t = uint32_t(__cdecl *)();
    return reinterpret_cast<OsGetAsyncTimeMs_t>(Offsets::FUN_OS_GET_ASYNC_TIME_MS)();
}

// Notes a path as played. A path that plays again refreshes its slot rather
// than taking a new one, so a repeating footstep can't push the rest out: the
// list holds the last 64 DIFFERENT files.
void Record(const char *path, bool muted) {
    const uint32_t now = NowMs();
    int oldest = 0;
    for (int i = 0; i < kRecentSlots; ++i) {
        Recent &r = g_recent[i];
        if (r.seq != 0 && SamePath(r.path, path)) {
            r.seq = ++g_seq;
            r.timeMs = now;
            r.muted = muted;
            return;
        }
        if (r.seq < g_recent[oldest].seq)
            oldest = i; // an unused slot (seq 0) wins outright
    }
    Recent &r = g_recent[oldest];
    const size_t n = std::strlen(path);
    const size_t copy = n < kMaxPath ? n : kMaxPath - 1;
    std::memcpy(r.path, path, copy);
    r.path[copy] = '\0';
    r.seq = ++g_seq;
    r.timeMs = now;
    r.muted = muted;
}

using CreateWaveData_t = int(__cdecl *)(void *system, const char *path, uint32_t mode, void *sound,
                                        void *loadSoundGroup, void *soundGroup, bool randomStart,
                                        int priority, int reverb, bool streaming,
                                        uint32_t decodeBufferSize, int software,
                                        float coneInsideAngle, float coneOutsideAngle,
                                        float coneOutsideVolume, const void *coneOrientation);
CreateWaveData_t g_createWaveDataOriginal = nullptr;

int __cdecl CreateWaveData_h(void *system, const char *path, uint32_t mode, void *sound,
                             void *loadSoundGroup, void *soundGroup, bool randomStart, int priority,
                             int reverb, bool streaming, uint32_t decodeBufferSize, int software,
                             float coneInsideAngle, float coneOutsideAngle,
                             float coneOutsideVolume, const void *coneOrientation) {
    if (path != nullptr && *path != '\0') {
        const bool muted = IsMuted(path);
        Record(path, muted);
        if (muted)
            return 0;
    }
    return g_createWaveDataOriginal(system, path, mode, sound, loadSoundGroup, soundGroup,
                                    randomStart, priority, reverb, streaming, decodeBufferSize,
                                    software, coneInsideAngle, coneOutsideAngle,
                                    coneOutsideVolume, coneOrientation);
}

const Game::HookAutoRegister _hook{Offsets::FUN_SE2_CREATE_WAVE_DATA,
                                   reinterpret_cast<void *>(&CreateWaveData_h),
                                   reinterpret_cast<void **>(&g_createWaveDataOriginal)};

const char *PathArg(void *L) {
    if (Game::Lua::Type(L, 1) != Game::Lua::TYPE_STRING)
        return nullptr;
    const char *path = Game::Lua::ToString(L, 1);
    return path != nullptr && *path != '\0' ? path : nullptr;
}

// MuteSoundFile(path) -> true when the path is now muted.
int __cdecl Script_MuteSoundFile(void *L) {
    const char *path = PathArg(L);
    if (path != nullptr)
        g_muted.insert(Normalize(path));
    Game::Lua::PushBool(L, path != nullptr);
    return 1;
}

// UnmuteSoundFile(path) -> true when the path had been muted.
int __cdecl Script_UnmuteSoundFile(void *L) {
    const char *path = PathArg(L);
    Game::Lua::PushBool(L, path != nullptr && g_muted.erase(Normalize(path)) != 0);
    return 1;
}

// C_Sound.GetRecentSoundFiles() -> { { file, time, muted }, ... }, newest
// first. `time` is milliseconds on GetTime()'s clock.
int __cdecl Script_GetRecentSoundFiles(void *L) {
    int order[kRecentSlots];
    int used = 0;
    for (int i = 0; i < kRecentSlots; ++i)
        if (g_recent[i].seq != 0)
            order[used++] = i;
    for (int i = 0; i < used; ++i) {
        int best = i;
        for (int j = i + 1; j < used; ++j)
            if (g_recent[order[j]].seq > g_recent[order[best]].seq)
                best = j;
        const int t = order[i];
        order[i] = order[best];
        order[best] = t;
    }

    Game::Lua::NewTable(L);
    for (int i = 0; i < used; ++i) {
        const Recent &r = g_recent[order[i]];
        Game::Lua::NewTable(L);
        Game::Lua::SetFieldString(L, "file", r.path);
        Game::Lua::SetFieldNumber(L, "time", static_cast<double>(r.timeMs));
        Game::Lua::SetFieldBool(L, "muted", r.muted);
        Game::Lua::RawSetI(L, -2, i + 1);
    }
    return 1;
}

// Mutes last for the Lua session, like Blizzard's.
void PrepareForReload() { g_muted.clear(); }

const Game::ReloadAutoRegister _reload{&PrepareForReload};

void RegisterLuaFunctions() {
    Game::Lua::RegisterGlobalFunction("MuteSoundFile", &Script_MuteSoundFile);
    Game::Lua::RegisterGlobalFunction("UnmuteSoundFile", &Script_UnmuteSoundFile);
    Game::Lua::RegisterTableFunction("C_Sound", "GetRecentSoundFiles",
                                     &Script_GetRecentSoundFiles);
}

const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace

} // namespace Sound::Mute
