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

#include "texture/Load.h"

#include "Offsets.h"

namespace Texture {

namespace {

using FlagsInit_t = uint32_t *(__fastcall *)(uint32_t *flags, void *edx, uint32_t, uint32_t,
                                             uint32_t, uint32_t, uint32_t, uint32_t, uint32_t,
                                             uint32_t, uint32_t, uint32_t);
using LoadByPath_t = void *(__cdecl *)(const char *path, uint32_t flags, void *desc,
                                       uint32_t mode);
using DescDtor_t = void(__fastcall *)(void *desc);
using CreateSolid_t = void *(__cdecl *)(const uint32_t &bgra);
using Release_t = void(__cdecl *)(void *handle);

} // namespace

void *LoadFile(const char *path, uint32_t cacheKeyBits) {
    if (path == nullptr || *path == '\0')
        return nullptr;
    // The load's CStatus, as the engine's callers build it on the stack.
    struct TexLoadDesc {
        void *vtbl;
        int32_t field4;
        void *self8;
        uintptr_t fieldC;
        int32_t errorLevel;
    } desc;
    desc.vtbl = reinterpret_cast<void *>(Offsets::PTR_TEXLOAD_DESC_VTBL);
    desc.field4 = 8;
    desc.self8 = &desc.self8;
    desc.fieldC = reinterpret_cast<uintptr_t>(&desc.self8) | 1u;
    desc.errorLevel = 0;
    uint32_t flagsOut = 0;
    const uint32_t flags = *reinterpret_cast<FlagsInit_t>(Offsets::FUN_TEXTURE_FLAGS_INIT)(
                               &flagsOut, nullptr, 1, 0, 0, 0, 0, 0, 1, 0, 0, 0) |
                           cacheKeyBits;
    void *handle =
        reinterpret_cast<LoadByPath_t>(Offsets::FUN_TEXTURE_LOAD_BY_PATH)(path, flags, &desc, 3);
    const bool failed = desc.errorLevel > 1;
    reinterpret_cast<DescDtor_t>(Offsets::FUN_TEXLOAD_DESC_DTOR)(&desc);
    if (failed) {
        ReleaseHandle(handle);
        return nullptr;
    }
    return handle;
}

void *CreateSolid(uint32_t bgra) {
    return reinterpret_cast<CreateSolid_t>(Offsets::FUN_TEXTURE_CREATE_SOLID)(bgra);
}

void ReleaseHandle(void *handle) {
    if (handle != nullptr)
        reinterpret_cast<Release_t>(Offsets::FUN_TEXTURE_RELEASE)(handle);
}

} // namespace Texture
