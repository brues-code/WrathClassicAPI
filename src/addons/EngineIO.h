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

#pragma once

#include <cstddef>

// One home for the engine file-I/O and Storm-allocator function-pointer types
// the addon modules call. These are ABI-critical: FUN_FILE_READ and the Storm
// allocator are all `__stdcall` (callee cleans the stack), and declaring one
// `__cdecl` by mistake drifts ESP a few bytes per call and kills the process
// with no crash log. Centralized so the convention cannot drift between the
// embedded-addon and rescan modules.
namespace Addons::EngineIO {

// FUN_FILE_READ (0x00424E80) — __stdcall, callee cleans 28 bytes (RET 0x1C).
// arg0 is an optional archive handle (0 = merged VFS); outSize may be null.
using FileReadFn = int(__stdcall *)(int unused, const char *path, void **outBuf,
                                    size_t *outSize, size_t extraBytes,
                                    int flag1, int flag2);

// FUN_STORM_SMEM_ALLOC / FUN_STORM_SMEM_FREE — __stdcall, RET 0x10. A buffer
// FUN_FILE_READ hands out is freed with SMemFreeFn; a buffer we allocate with
// SMemAllocFn is freed cleanly by the engine's own SMemFree in turn.
using SMemAllocFn = void *(__stdcall *)(size_t size, const char *file, int line,
                                        int flags);
using SMemFreeFn = int(__stdcall *)(void *buf, const char *file, int line,
                                    int flags);

// FUN_FILE_EXISTS (0x00424B10) — __stdcall, callee cleans 8 bytes (RET 8).
// Nonzero when `path` exists; `mode` is 1 on the addon / SavedVariables paths.
using FileExistsFn = int(__stdcall *)(const char *path, int mode);

} // namespace Addons::EngineIO
