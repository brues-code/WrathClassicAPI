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

#include "texture/Lifetime.h"

#include "Game.h"
#include "Offsets.h"

#include <cstdint>

namespace Texture::Lifetime {

namespace {

OnDestroyed *g_head = nullptr;

using Destructor_t = void *(__fastcall *)(void *texture, void *, uint8_t flags);
Destructor_t g_destructor = nullptr;

void *__fastcall Destructor_h(void *texture, void *, uint8_t flags) {
    if (texture != nullptr)
        for (OnDestroyed *s = g_head; s != nullptr; s = s->next)
            s->fn(texture);
    return g_destructor(texture, nullptr, flags);
}

const Game::HookAutoRegister _destructorHook{Offsets::FUN_SIMPLETEXTURE_DTOR, reinterpret_cast<void *>(&Destructor_h),
                                             reinterpret_cast<void **>(&g_destructor)};

} // namespace

OnDestroyed::OnDestroyed(Fn f) : fn(f), next(g_head) { g_head = this; }

} // namespace Texture::Lifetime
