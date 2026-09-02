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

// `QUEST_REMOVED(questID)` — fires when a quest leaves the player's quest log,
// for any reason (turned in, abandoned, auto-failed). We hook the engine's
// quest-log rebuild (FUN_005E6940, which reruns on every quest-log change and
// fires QUEST_LOG_UPDATE), and diff the freshly-rebuilt log against a snapshot of
// the previous one: any questID that was present and now isn't was removed.
//
// The log's display array carries category headers interleaved with quests
// (isHeader = 1 for a header); we collect only the quest entries. The snapshot is
// process-static and survives /reload (the log rebuilds to the same set, so no
// spurious removals fire).

#include "Game.h"
#include "Offsets.h"
#include "event/Custom.h"

#include <cstdint>

namespace Quest::Removed {

namespace {

constexpr const char *kEventName = "QUEST_REMOVED";

const Event::Custom::AutoReserve _reserve{kEventName};

// The quest log holds at most 25 quests; leave headroom.
constexpr int kMaxQuests = 32;
uint32_t g_snapshot[kMaxQuests];
int g_snapshotCount = 0;

using QuestLogRebuild_t = void(__cdecl *)(uint32_t param);
QuestLogRebuild_t QuestLogRebuild_o = nullptr;

// Collect the current log's quest questIDs (skipping category headers) into
// `out`; returns the count written.
int ReadLogQuests(uint32_t *out, int maxOut) {
    const int count = *reinterpret_cast<const int *>(
        static_cast<uintptr_t>(Offsets::VAR_QUESTLOG_ENTRY_COUNT));
    const auto *base = reinterpret_cast<const uint8_t *>(
        static_cast<uintptr_t>(Offsets::VAR_QUESTLOG_ENTRIES));
    int n = 0;
    for (int i = 0; i < count && n < maxOut; ++i) {
        const uint8_t *entry = base + i * Offsets::QUESTLOG_ENTRY_STRIDE;
        if (*reinterpret_cast<const uint32_t *>(
                entry + Offsets::OFF_QUESTLOG_ENTRY_IS_HEADER) != 0)
            continue; // category header, not a quest
        const uint32_t questID = *reinterpret_cast<const uint32_t *>(
            entry + Offsets::OFF_QUESTLOG_ENTRY_QUEST_ID);
        if (questID != 0)
            out[n++] = questID;
    }
    return n;
}

bool Contains(const uint32_t *arr, int count, uint32_t q) {
    for (int i = 0; i < count; ++i)
        if (arr[i] == q)
            return true;
    return false;
}

void __cdecl QuestLogRebuild_h(uint32_t param) {
    QuestLogRebuild_o(param);

    uint32_t current[kMaxQuests];
    const int currentCount = ReadLogQuests(current, kMaxQuests);

    // Any quest in the previous snapshot that's no longer present was removed.
    for (int i = 0; i < g_snapshotCount; ++i) {
        if (!Contains(current, currentCount, g_snapshot[i]))
            Event::Custom::Fire(Event::Custom::Lookup(kEventName), "%u", g_snapshot[i]);
    }

    for (int i = 0; i < currentCount; ++i)
        g_snapshot[i] = current[i];
    g_snapshotCount = currentCount;
}

const Game::HookAutoRegister _hook{
    Offsets::FUN_QUESTLOG_REBUILD,
    reinterpret_cast<void *>(&QuestLogRebuild_h),
    reinterpret_cast<void **>(&QuestLogRebuild_o)};

} // namespace

} // namespace Quest::Removed
