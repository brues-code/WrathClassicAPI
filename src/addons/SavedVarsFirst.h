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

namespace Addons::SavedVarsFirst {

// Load the SavedVariables of an addon whose TOC sets
// `## LoadSavedVariablesFirst`, before its files run. Driven by the shared
// TOC-executor seam (src/addons/TocExecutor.cpp). Runs Lua, so it is called
// after every stack-inspecting observer. A no-op for any other TOC.
void OnTocExecute(const char *tocPath, const char *addonName);

} // namespace Addons::SavedVarsFirst
