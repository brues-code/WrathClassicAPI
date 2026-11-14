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

// A CSimpleTexture's desaturation amount, 0 (full colour) to 1 (the engine's
// SetDesaturated(true)), as Texture:SetDesaturation sets it.
namespace Texture::Desaturation {

double Get(const void *texture);

// Clamps `amount` to 0..1; NaN is 0.
void Set(void *texture, double amount);

} // namespace Texture::Desaturation
