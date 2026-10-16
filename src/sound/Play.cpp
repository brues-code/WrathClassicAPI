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

// `PlaySound(soundKitID | soundName)`, `C_Sound.PlaySound`,
// `C_Sound.PlaySoundWithOptions`, `C_Sound.IsPlaying`,
// `C_Sound.GetSoundScaledVolume` and the `SOUNDKIT_FINISHED` event. Ported
// from ClassicAPI's src/sound/Play.cpp.
//
// The stock PlaySound already plays a SoundKitID or a kit name through
// SI2::PlayUISound, but returns nothing. Every form here goes through
// SI2::PlaySoundKit with PlayUISound's settings (UI priority, no reverb, no
// second copy of a kit while one plays), which also hands back the started
// sound's id. That id is the handle: SESound ids come from a counter that only
// goes up, so a handle never names a later sound. A live handle is looked up
// with SE2::GetSoundFromID, which returns null once the sound is released.
//
// SOUNDKIT_FINISHED is polled on the frame tick for the sounds that asked for
// it. The engine's own finished callback runs inside FMOD's channel callback
// under its sound-list lock, which is no place to call into Lua.

#include "Game.h"
#include "Offsets.h"
#include "event/Custom.h"
#include "tick/FrameTick.h"

#include <cstdint>
#include <cstring>
#include <vector>

namespace Sound::Play {

namespace {

using PlaySoundKit_t = int(__cdecl *)(int soundKitID, const void *position, void *object,
                                      void *properties, uint32_t advancedID,
                                      uint32_t *outSoundID, int checkDistance, int keepPlaying);
using LookupSoundKitID_t = int(__cdecl *)(const char *name);
using PropertiesReset_t = void(__fastcall *)(void *properties);
using GetSoundFromID_t = void *(__cdecl *)(uint32_t id, int *created);
using SoundIsPlaying_t = bool(__fastcall *)(void *sound);
using SoundDetach_t = void(__fastcall *)(void *sound);
using GetScaledVolume_t = float(__fastcall *)(void *internal);
using SMemFree_t = int(__stdcall *)(void *ptr, const char *file, int line, int flags);

template <typename T> void Write(void *base, uintptr_t offset, T value) {
    *reinterpret_cast<T *>(static_cast<uint8_t *>(base) + offset) = value;
}

// ---- Live sound lookup -----------------------------------------------------

// Runs `fn(sound)` on the live SESound for `id` and returns true, or returns
// false when the sound has been released. Frees the SESound the lookup made
// for this call, the way the engine's own callers do.
template <typename Fn> bool WithSound(uint32_t id, Fn fn) {
    if (id == 0)
        return false;
    int created = 0;
    void *sound =
        reinterpret_cast<GetSoundFromID_t>(Offsets::FUN_SE2_GET_SOUND_FROM_ID)(id, &created);
    if (sound == nullptr)
        return false;
    fn(sound);
    if (created != 0) {
        reinterpret_cast<SoundDetach_t>(Offsets::FUN_SESOUND_DETACH)(sound);
        reinterpret_cast<SMemFree_t>(Offsets::FUN_STORM_SMEM_FREE)(sound, nullptr, 0, 0);
    }
    return true;
}

bool IsPlaying(uint32_t id) {
    bool playing = false;
    WithSound(id, [&](void *sound) {
        playing = reinterpret_cast<SoundIsPlaying_t>(Offsets::FUN_SESOUND_IS_PLAYING)(sound);
    });
    return playing;
}

// ---- SOUNDKIT_FINISHED -----------------------------------------------------

constexpr const char *kEventName = "SOUNDKIT_FINISHED";

const Event::Custom::AutoReserve _reserve{kEventName};

// Only sounds played with runFinishCallback, so almost always empty.
std::vector<uint32_t> g_tracked;

void OnFrame() {
    if (g_tracked.empty())
        return;
    const int eventID = Event::Custom::Lookup(kEventName);
    size_t keep = 0;
    for (const uint32_t id : g_tracked) {
        if (IsPlaying(id)) {
            g_tracked[keep++] = id;
            continue;
        }
        Event::Custom::Fire(eventID, "%d", static_cast<int>(id));
    }
    g_tracked.resize(keep);
}

const Tick::FrameTick::AutoSubscribe _tickSub{&OnFrame};

void PrepareForReload() { g_tracked.clear(); }

const Game::ReloadAutoRegister _reload{&PrepareForReload};

// ---- Playing ---------------------------------------------------------------

struct PlayOptions {
    int group = Offsets::SOUND_GROUP_SFX;
    float volumeScale = 1.0f;
    bool runFinishCallback = false;
};

// Starts `soundKitID` and pushes `willPlay, soundHandle`.
int Play(void *L, int soundKitID, const PlayOptions &opts) {
    uint32_t id = 0;
    int result = -1;
    if (soundKitID > 0) {
        alignas(4) uint8_t props[Offsets::SOUND_KIT_PROPERTIES_SIZE];
        reinterpret_cast<PropertiesReset_t>(Offsets::FUN_SOUND_KIT_PROPERTIES_RESET)(props);
        Write<int32_t>(props, Offsets::OFF_SOUND_KIT_PROPS_GROUP, opts.group);
        Write<float>(props, Offsets::OFF_SOUND_KIT_PROPS_VOLUME_SCALE, opts.volumeScale);
        Write<int32_t>(props, Offsets::OFF_SOUND_KIT_PROPS_PRIORITY, Offsets::SOUND_UI_PRIORITY);
        Write<int32_t>(props, Offsets::OFF_SOUND_KIT_PROPS_SUPPRESSOR_MODE,
                       Offsets::SOUND_SUPPRESSOR_NO_DUPLICATES);
        Write<int32_t>(props, Offsets::OFF_SOUND_KIT_PROPS_REVERB, 0);
        result = reinterpret_cast<PlaySoundKit_t>(Offsets::FUN_SI2_PLAY_SOUND_KIT)(
            soundKitID, nullptr, nullptr, props, 0, &id, 1, 0);
    }
    const bool started = result == 0 && id != 0;
    if (started && opts.runFinishCallback)
        g_tracked.push_back(id);

    Game::Lua::PushBool(L, started);
    if (started)
        Game::Lua::PushNumber(L, static_cast<double>(id));
    else
        Game::Lua::PushNil(L);
    return 2;
}

// Modern channel names: "SFX", "Music", "Ambience", "Dialog", "Master".
// Dialog has no group of its own here and plays as SFX; Master plays under the
// master volume only. Anything else is SFX.
int GroupForChannel(void *L, int idx) {
    if (Game::Lua::Type(L, idx) != Game::Lua::TYPE_STRING)
        return Offsets::SOUND_GROUP_SFX;
    const char *name = Game::Lua::ToString(L, idx);
    if (_stricmp(name, "Music") == 0)
        return Offsets::SOUND_GROUP_MUSIC;
    if (_stricmp(name, "Ambience") == 0)
        return Offsets::SOUND_GROUP_AMBIENCE;
    if (_stricmp(name, "Master") == 0)
        return Offsets::SOUND_GROUP_SCRIPT_SOUND;
    return Offsets::SOUND_GROUP_SFX;
}

// PlaySound(soundKitID | soundName [, channel [, forceNoDuplicates
//           [, runFinishCallback]]]) -> willPlay, soundHandle
//
// Keys on the Lua type: a number is a SoundKitID, a string is a kit name
// ("igMainMenuOpen"). forceNoDuplicates is accepted and ignored: a kit never
// starts a second copy while one plays, as with the stock PlaySound.
int PlayFromArgs(void *L, const char *usage) {
    int soundKitID = 0;
    if (Game::Lua::Type(L, 1) == Game::Lua::TYPE_NUMBER)
        soundKitID = static_cast<int>(Game::Lua::ToNumber(L, 1));
    else if (Game::Lua::Type(L, 1) == Game::Lua::TYPE_STRING)
        soundKitID = reinterpret_cast<LookupSoundKitID_t>(Offsets::FUN_SI2_LOOKUP_SOUND_KIT_ID)(
            Game::Lua::ToString(L, 1));
    else
        return Game::Lua::Error(L, usage);

    PlayOptions opts;
    opts.group = GroupForChannel(L, 2);
    opts.runFinishCallback = Game::Lua::ToBoolean(L, 4) != 0;
    return Play(L, soundKitID, opts);
}

int __cdecl Script_PlaySound(void *L) {
    return PlayFromArgs(L, "Usage: PlaySound(soundKitID [, channel [, forceNoDuplicates "
                           "[, runFinishCallback]]])");
}

int __cdecl Script_C_Sound_PlaySound(void *L) {
    return PlayFromArgs(L, "Usage: C_Sound.PlaySound(soundKitID [, channel "
                           "[, forceNoDuplicates [, runFinishCallback]]])");
}

// C_Sound.PlaySoundWithOptions(params) -> success, soundHandle
//
// Honors soundKitID, volumeOverride (a scale on the kit's own volume) and
// runFinishCallback; accepts and ignores uiSoundSubType, forceNoDuplicates
// and overridePriority.
int __cdecl Script_PlaySoundWithOptions(void *L) {
    if (Game::Lua::Type(L, 1) != Game::Lua::TYPE_TABLE)
        return Game::Lua::Error(L, "Usage: C_Sound.PlaySoundWithOptions(params)");

    Game::Lua::GetField(L, 1, "soundKitID");
    const int soundKitID = Game::Lua::Type(L, -1) == Game::Lua::TYPE_NUMBER
                               ? static_cast<int>(Game::Lua::ToNumber(L, -1))
                               : 0;
    Game::Lua::GetField(L, 1, "volumeOverride");
    const bool hasVolume = Game::Lua::Type(L, -1) == Game::Lua::TYPE_NUMBER;
    const double volume = hasVolume ? Game::Lua::ToNumber(L, -1) : 1.0;
    Game::Lua::GetField(L, 1, "runFinishCallback");
    const bool runFinishCallback = Game::Lua::ToBoolean(L, -1) != 0;
    Game::Lua::SetTop(L, 1);

    PlayOptions opts;
    opts.volumeScale = volume < 0.0 ? 0.0f : static_cast<float>(volume);
    opts.runFinishCallback = runFinishCallback;
    return Play(L, soundKitID, opts);
}

uint32_t HandleArg(void *L) {
    if (Game::Lua::Type(L, 1) != Game::Lua::TYPE_NUMBER)
        return 0;
    const double v = Game::Lua::ToNumber(L, 1);
    return v > 0.0 ? static_cast<uint32_t>(v) : 0;
}

// C_Sound.IsPlaying(soundHandle) -> bool
int __cdecl Script_IsPlaying(void *L) {
    Game::Lua::PushBool(L, IsPlaying(HandleArg(L)));
    return 1;
}

// C_Sound.GetSoundScaledVolume(soundHandle) -> volume, or nil once released
int __cdecl Script_GetSoundScaledVolume(void *L) {
    float volume = 0.0f;
    const bool alive = WithSound(HandleArg(L), [&](void *sound) {
        void *internal = *static_cast<void **>(sound);
        if (internal != nullptr)
            volume = reinterpret_cast<GetScaledVolume_t>(
                Offsets::FUN_SESOUND_INTERNAL_GET_SCALED_VOLUME)(internal);
    });
    if (alive)
        Game::Lua::PushNumber(L, static_cast<double>(volume));
    else
        Game::Lua::PushNil(L);
    return 1;
}

void RegisterLuaFunctions() {
    Game::Lua::RegisterGlobalFunction("PlaySound", &Script_PlaySound);
    Game::Lua::RegisterTableFunction("C_Sound", "PlaySound", &Script_C_Sound_PlaySound);
    Game::Lua::RegisterTableFunction("C_Sound", "PlaySoundWithOptions",
                                     &Script_PlaySoundWithOptions);
    Game::Lua::RegisterTableFunction("C_Sound", "IsPlaying", &Script_IsPlaying);
    Game::Lua::RegisterTableFunction("C_Sound", "GetSoundScaledVolume",
                                     &Script_GetSoundScaledVolume);
}

const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace

} // namespace Sound::Play
