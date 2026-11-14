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

// The end of a StatusBar, for the modules that keep state by bar. Every
// StatusBar dies through one of the class's two destructor paths, whichever
// way it was made (Offsets.h, FUN_STATUSBAR_DELETING_DESTRUCTOR); this module
// owns their hooks, since a target carries one detour.
//
// Declare a file-scope
//   static const StatusBar::Lifetime::OnDestroyed _destroyed{&Forget};
namespace StatusBar::Lifetime {

struct OnDestroyed {
    using Fn = void (*)(const void *bar);
    explicit OnDestroyed(Fn fn);
    Fn fn;
    OnDestroyed *next;
};

} // namespace StatusBar::Lifetime
