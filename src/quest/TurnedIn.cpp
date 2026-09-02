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

// `QUEST_TURNED_IN(questID, xpReward, moneyReward)` — fires when a quest turn-in
// is confirmed by the server. We hook the quest-complete processor (FUN_006D0E10),
// which the SMSG_QUESTGIVER_QUEST_COMPLETE handler runs with the reward amounts
// the player actually receives — the server's XP/money rates are already applied,
// so on a custom-rate server this reports the real granted XP, not the base quest
// value. `moneyReward` is in copper; both are `0` where not applicable (e.g.
// xpReward at max level).

#include "Game.h"
#include "Offsets.h"
#include "event/Custom.h"

#include <cstdint>

namespace Quest::TurnedIn {

namespace {

constexpr const char *kEventName = "QUEST_TURNED_IN";

const Event::Custom::AutoReserve _reserve{kEventName};

// `__cdecl(uint questID, void *scratch, int *reward, char process)`.
using QuestComplete_t = void(__cdecl *)(uint32_t questID, void *scratch,
                                        const int32_t *reward, char process);
QuestComplete_t QuestComplete_o = nullptr;

void __cdecl QuestComplete_h(uint32_t questID, void *scratch, const int32_t *reward,
                             char process) {
    QuestComplete_o(questID, scratch, reward, process);
    // `process` != 0 is the reward-granting pass (== 0 is the buffer-free pass).
    // reward[0] = XP, reward[1] = money — both with server rates already applied.
    if (process != 0 && reward != nullptr && questID != 0)
        Event::Custom::Fire(Event::Custom::Lookup(kEventName), "%u%u%u", questID,
                            static_cast<uint32_t>(reward[0]),
                            static_cast<uint32_t>(reward[1]));
}

const Game::HookAutoRegister _hook{
    Offsets::FUN_QUEST_COMPLETE_PROCESS,
    reinterpret_cast<void *>(&QuestComplete_h),
    reinterpret_cast<void **>(&QuestComplete_o)};

} // namespace

} // namespace Quest::TurnedIn
