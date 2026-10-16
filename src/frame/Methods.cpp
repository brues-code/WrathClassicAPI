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

#include "frame/Methods.h"

#include "Offsets.h"

namespace Frame::Methods {

namespace {

using Walker_t = void(__cdecl *)(void *L);
using RegisterFrameMethods_t = void(__cdecl *)(void *L, const void *table, int count);

AutoRegister *g_head = nullptr;
Walker_t g_walkerOriginal = nullptr;

void __cdecl Walker_h(void *L) {
    g_walkerOriginal(L);
    auto registerFn = reinterpret_cast<RegisterFrameMethods_t>(Offsets::FUN_REGISTER_FRAME_METHODS);
    for (auto *node = g_head; node != nullptr; node = node->next)
        registerFn(L, node->table, node->count);
}

const Game::HookAutoRegister _walkerHook{
    Offsets::FUN_FRAME_METHODS_WALKER, reinterpret_cast<void *>(&Walker_h),
    reinterpret_cast<void **>(&g_walkerOriginal)};

} // namespace

AutoRegister::AutoRegister(const Entry *t, int n) : table(t), count(n), next(g_head) {
    g_head = this;
}

} // namespace Frame::Methods
