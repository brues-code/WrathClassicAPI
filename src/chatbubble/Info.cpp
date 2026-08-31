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

// `C_ChatBubbles.GetAllChatBubbles([includeForbidden])` — returns an
// array of the currently-active chat-bubble Frame objects. Addons use it
// to recolor / translate / reposition speech bubbles by reading the
// bubble's FontString region.
//
// 3.3.5 chat bubbles are `CGChatBubbleFrame`s — full frames created purely
// in C++ (never via `CreateFrame`), so they have no name and no Lua
// wrapper until something pushes them. We push each via the shared
// `UI::FrameObject::Push`, which delegates to the engine's own
// `FrameScript_Object::ScriptRegister` to lazily build the canonical
// wrapper. The returned frames respond to real frame methods — the
// FontString holding the spoken text (bubble + 0x2A4, created with the
// bubble as its parent) shows up in `bubble:GetRegions()`, so the modern
// "iterate GetRegions, find the FontString, read GetText()" idiom works
// unchanged.
//
// The engine keeps active bubbles on an intrusive Storm TSList whose head
// is `VAR_CHAT_BUBBLE_LIST_HEAD`; each node's forward link sits at
// `OFF_CHAT_BUBBLE_NEXT_LINK` and the list terminator carries the low-bit
// sentinel. We mirror the engine's own walks (FUN_0056D050 / FUN_0056CF80
// / FUN_0056C7A0) verbatim. A bubble whose owner unit has despawned is
// pruned on the engine's next update tick, so a just-orphaned bubble can
// linger one frame — harmless, and addons that care filter on
// `bubble:IsShown()` (a real method here) anyway.
//
// `includeForbidden` is accepted and ignored: 3.3.5 has no forbidden
// frames, so every bubble is returned regardless.

#include "Game.h"
#include "Offsets.h"
#include "ui/FrameObject.h"

#include <cstdint>

namespace ChatBubble::Info {

namespace {

int __cdecl Script_GetAllChatBubbles(void *L) {
    Game::Lua::NewTable(L); // result array — [t]

    uintptr_t node = *reinterpret_cast<const uintptr_t *>(
        static_cast<uintptr_t>(Offsets::VAR_CHAT_BUBBLE_LIST_HEAD));
    int index = 1;
    while (node != 0 && (node & 1) == 0) {
        UI::FrameObject::Push(L, reinterpret_cast<void *>(node)); // [t, wrapper]
        Game::Lua::RawSetI(L, -2, index++);                       // t[index]=wrapper; pops
        node = *reinterpret_cast<const uintptr_t *>(
            node + Offsets::OFF_CHAT_BUBBLE_NEXT_LINK);
    }
    return 1;
}

void RegisterLuaFunctions() {
    Game::Lua::RegisterTableFunction("C_ChatBubbles", "GetAllChatBubbles",
                                     &Script_GetAllChatBubbles);
}

const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace

} // namespace ChatBubble::Info
