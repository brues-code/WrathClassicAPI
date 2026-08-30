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

#include "tick/FrameTick.h"

#include "Game.h"
#include "Offsets.h"

namespace Tick::FrameTick {

namespace {

AutoSubscribe *g_subs = nullptr;

using FireOnUpdate_t = void(__cdecl *)(int a1, int a2, int a3, int a4);
FireOnUpdate_t FireOnUpdate_o = nullptr;

void __cdecl FireOnUpdate_h(int a1, int a2, int a3, int a4) {
    // Engine per-frame OnUpdate dispatch first, subscribers after — user
    // OnUpdate handlers see "the world has progressed" before downstream
    // consumers (C_Timer callbacks, coalesced events) run.
    FireOnUpdate_o(a1, a2, a3, a4);
    for (auto *node = g_subs; node != nullptr; node = node->next)
        node->cb();
}

const Game::HookAutoRegister _hookreg{
    Offsets::FUN_FRAMESCRIPT_FIRE_ON_UPDATE,
    reinterpret_cast<void *>(&FireOnUpdate_h),
    reinterpret_cast<void **>(&FireOnUpdate_o)};

} // namespace

AutoSubscribe::AutoSubscribe(Callback cb) : cb(cb), next(g_subs) {
    g_subs = this;
}

} // namespace Tick::FrameTick
