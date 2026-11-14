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

// HTEXTURE handles as the engine's own non-region users load them
// (Minimap:SetMaskTexture 0x00583860, CGCooldown::SetCooldown 0x005ECD70):
// linear, clamped, no mips, never on the UI atlas.

#include <cstdint>

namespace Texture {

// `path` as a texture of its own: one reference, or null when the file does
// not load (the engine would hand back its solid green placeholder).
// `cacheKeyBits` are or'ed into the flags word the texture cache keys on.
void *LoadFile(const char *path, uint32_t cacheKeyBits = 0);

// A solid texture of one BGRA colour (TextureCreateSolid): one reference.
void *CreateSolid(uint32_t bgra);

// Drops one reference; null is ignored.
void ReleaseHandle(void *handle);

} // namespace Texture
