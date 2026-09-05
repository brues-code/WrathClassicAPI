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

namespace AddOns::GetAddOnLocalTable {

// Capture the addon's per-addon namespace table before its files run. Driven by
// the shared TOC-executor seam (src/addons/TocExecutor.cpp), which calls this
// FIRST because it identifies the table by reading the Lua stack top. Leaves the
// Lua stack as it found it. A no-op for the FrameXML / glue callers.
void OnTocExecute(const char *tocPath, const char *addonName);

} // namespace AddOns::GetAddOnLocalTable
