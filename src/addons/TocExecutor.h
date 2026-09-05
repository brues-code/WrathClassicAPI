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

// The "engine is about to run a TOC's file list" seam
// (`Offsets::FUN_TOC_EXECUTOR`), shared by the features that need it.
//
// A hook target may carry only one detour, so TocExecutor.cpp owns the single
// hook and calls each observer below in a FIXED order — the order is a real
// constraint, not a preference, so it lives in one readable place rather than
// behind a priority number. Each observer is declared by its own module.

namespace Addons::TocExecutor {

// Called before the engine executes `tocPath`'s file list. `addonName` is the
// engine's addon name argument (null for the FrameXML / glue callers).
//
// Observers run in the order listed in TocExecutor.cpp. An observer that
// inspects the Lua stack must come before any observer that runs Lua.
void RunObservers(const char *tocPath, const char *addonName);

} // namespace Addons::TocExecutor
