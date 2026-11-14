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

#include "statusbar/Lifetime.h"

#include "Game.h"
#include "Offsets.h"

#include <cstdint>

namespace StatusBar::Lifetime {

namespace {

OnDestroyed *g_head = nullptr;

void Notify(const void *bar) {
    for (OnDestroyed *s = g_head; s != nullptr; s = s->next)
        s->fn(bar);
}

using DeletingDestructor_t = void *(__fastcall *)(void *bar, void *, uint32_t flags);
using Destructor_t = void(__fastcall *)(void *bar, void *);
DeletingDestructor_t g_deletingDestructor = nullptr;
Destructor_t g_destructor = nullptr;

void *__fastcall DeletingDestructor_h(void *bar, void *, uint32_t flags) {
    Notify(bar);
    return g_deletingDestructor(bar, nullptr, flags);
}

void __fastcall Destructor_h(void *bar, void *) {
    Notify(bar);
    g_destructor(bar, nullptr);
}

const Game::HookAutoRegister _deletingDestructorHook{Offsets::FUN_STATUSBAR_DELETING_DESTRUCTOR,
                                                     reinterpret_cast<void *>(&DeletingDestructor_h),
                                                     reinterpret_cast<void **>(&g_deletingDestructor)};
const Game::HookAutoRegister _destructorHook{Offsets::FUN_STATUSBAR_DESTRUCTOR, reinterpret_cast<void *>(&Destructor_h),
                                             reinterpret_cast<void **>(&g_destructor)};

} // namespace

OnDestroyed::OnDestroyed(Fn f) : fn(f), next(g_head) { g_head = this; }

} // namespace StatusBar::Lifetime
