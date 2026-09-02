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

// `QUEST_TURNED_IN(questID)` — fires when the player completes (turns in) a
// quest. We hook the engine's reward-choice send (FUN_0058CFA0, the
// "Complete Quest" click that dispatches CMSG_QUESTGIVER_CHOOSE_REWARD) and fire
// once the send is accepted, with the active quest-giver questID.
//
// The 3.3.5 client checks reward-item inventory space before this send and
// blocks it with a UI error otherwise, so a dispatched turn-in effectively
// always succeeds — firing here rather than on the server response is reliable
// in practice. Payload is `questID` only: the modern event's xpReward /
// moneyReward come from the server's completion packet, which 3.3.5 doesn't
// surface as an event (the client hasn't been told the reward amounts yet at
// this point).

#include "Game.h"
#include "Offsets.h"
#include "event/Custom.h"

#include <cstdint>

namespace Quest::TurnedIn {

namespace {

constexpr const char *kEventName = "QUEST_TURNED_IN";

const Event::Custom::AutoReserve _reserve{kEventName};

using SendChooseReward_t = void(__cdecl *)(uint32_t rewardChoice);
SendChooseReward_t SendChooseReward_o = nullptr;

void __cdecl SendChooseReward_h(uint32_t rewardChoice) {
    // The engine may clear the quest-giver state as part of the send, so read
    // the questID and the in-flight flag around the original call and fire only
    // on a genuine 0 -> 1 transition (this call actually dispatched the turn-in).
    const uint32_t questID = *reinterpret_cast<const uint32_t *>(
        Offsets::VAR_QUESTGIVER_ACTIVE_QUEST_ID);
    const int wasPending = *reinterpret_cast<const int *>(
        Offsets::VAR_QUESTGIVER_REQUEST_PENDING);

    SendChooseReward_o(rewardChoice);

    const int nowPending = *reinterpret_cast<const int *>(
        Offsets::VAR_QUESTGIVER_REQUEST_PENDING);
    if (wasPending == 0 && nowPending != 0 && questID != 0)
        Event::Custom::Fire(Event::Custom::Lookup(kEventName), "%u", questID);
}

const Game::HookAutoRegister _hook{
    Offsets::FUN_QUESTGIVER_SEND_CHOOSE_REWARD,
    reinterpret_cast<void *>(&SendChooseReward_h),
    reinterpret_cast<void **>(&SendChooseReward_o)};

} // namespace

} // namespace Quest::TurnedIn
