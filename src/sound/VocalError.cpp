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

// `C_Sound.PlayVocalErrorSound(vocalErrorSoundID)` and `Enum.Vocalerrorsounds`
// — the player character's spoken complaint for an error ("my bags are full",
// "I have no ammo") in their race's and sex's voice. Ported from ClassicAPI's
// src/sound/VocalError.cpp.
//
// The client already has the player: SI2::PlayVocalErrorSound, which the UI
// error path calls. Its table is built at login from VocalUISounds.dbc for the
// character's race and sex (SI2::InitVocalErrorSounds), so this module only
// range-checks the id and calls it. Like the client's own error speech it
// honors the "Sound_EnableErrorSpeech" setting and doesn't speak over itself.

#include "Game.h"
#include "Offsets.h"

namespace Sound::VocalError {

namespace {

using PlayVocalErrorSound_t = void(__cdecl *)(int error);

// Modern `Enum.Vocalerrorsounds`; the values are VocalUISounds.dbc's own enum
// column.
const Game::Lua::EnumIntegerEntry kVocalErrorEntries[] = {
    {"Inventoryfull", 0},
    {"Outofammo", 1},
    {"NoequipLevel", 2},
    {"NoequipEver", 3},
    {"BoundNodrop", 4},
    {"Itemcooling", 5},
    {"Cantdrinkmore", 6},
    {"Canteatmore", 7},
    {"Cantinvite", 8},
    {"Inviteebusy", 9},
    {"Targettoofar", 10},
    {"Invalidtarget", 11},
    {"Spellcooling", 12},
    {"CantlearnLevel", 13},
    {"Locked", 14},
    {"Nomana", 15},
    {"Notwhiledead", 16},
    {"Cantloot", 17},
    {"Cantcreate", 18},
    {"Declinegroup", 19},
    {"Alreadyingroup", 20},
    {"Alreadyinguild", 21},
    {"Cantaffordbankslot", 22},
    {"Toomanybankslots", 23},
    {"CanteatMoving", 24},
    {"Notabag", 25},
    {"Cantputbag", 26},
    {"Wrongslot", 27},
    {"Ammoonlyinbag", 28},
    {"Bagfull", 29},
    {"Itemmaxcount", 30},
    {"CantlootDidntkill", 31},
    {"CantlootWrongfacing", 32},
    {"CantlootLocked", 33},
    {"CantlootNotstandingObsolete", 34},
    {"CantlootToofar", 35},
    {"Cantattackrongdirection", 36},
    {"CantattackNotstandingObsolete", 37},
    {"CantattackNotarget", 38},
    {"Notenoughgold", 39},
    {"Notenoughmoney", 40},
    {"Cantequip2HSkill", 41},
    {"Cantequip2Hequipped", 42},
    {"Cantequip2HNoskill", 43},
    {"Notequippable", 44},
    {"Genericnotarget", 45},
    {"CantcastOutofrange", 46},
    {"Potioncooling", 47},
    {"Proficiencyneeded", 48},
    {"Mustequippitem", 49},
    {"Abilitycooling", 50},
    {"Cantuseitem", 51},
    {"Chestinuse", 52},
    {"FoodcoolingObsolete", 53},
    {"CanttaxiNomoney", 54},
    {"Cantuselocked", 55},
    {"Noequipslotavailable", 56},
    {"Cantusetoofar", 57},
    {"Cantswap", 58},
    {"CanttradeSoulbound", 59},
    {"Cantflyhere", 60},
    {"Itemlocked", 61},
    {"Guildpermissions", 62},
    {"Norage", 63},
    {"Noenergy", 64},
    {"Noessence", 65},
    {"Invaliditemtarget", 66},
    {"ExhaustedObsolete", 67},
};

static_assert(sizeof(kVocalErrorEntries) / sizeof(kVocalErrorEntries[0]) ==
                  Offsets::VOCAL_ERROR_SOUND_COUNT,
              "one enum entry per VocalUISounds id");

// C_Sound.PlayVocalErrorSound(vocalErrorSoundID)
int __cdecl Script_PlayVocalErrorSound(void *L) {
    if (Game::Lua::Type(L, 1) != Game::Lua::TYPE_NUMBER)
        return Game::Lua::Error(L, "Usage: C_Sound.PlayVocalErrorSound(vocalErrorSoundID)");
    const int error = static_cast<int>(Game::Lua::ToNumber(L, 1));
    if (error >= 0 && error < Offsets::VOCAL_ERROR_SOUND_COUNT)
        reinterpret_cast<PlayVocalErrorSound_t>(Offsets::FUN_SI2_PLAY_VOCAL_ERROR_SOUND)(error);
    return 0;
}

void RegisterLuaFunctions() {
    Game::Lua::RegisterTableFunction("C_Sound", "PlayVocalErrorSound",
                                     &Script_PlayVocalErrorSound);
    Game::Lua::RegisterIntegerEnum("Enum", "Vocalerrorsounds", kVocalErrorEntries,
                                   static_cast<int>(sizeof(kVocalErrorEntries) /
                                                    sizeof(kVocalErrorEntries[0])));
}

const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace

} // namespace Sound::VocalError
