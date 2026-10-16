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

namespace Addons::TocFile {

// Reads the TOC at `tocPath` through the engine's file reader (FUN_FILE_READ —
// MPQ or loose file, and through our hook on it, so an embedded or rewritten
// TOC reads as the engine sees it) and reports whether the `## key:` directive
// (`key` without the "##" or colon, e.g. "LoadSavedVariablesFirst") has a
// nonzero leading integer — WoW's numeric TOC flag. Lines match as the engine's
// own TOC parser matches them. False when the file or directive is absent.
bool ReadFlag(const char *tocPath, const char *key);

} // namespace Addons::TocFile
