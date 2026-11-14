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

// `<OnLoad method="OnLoad"/>` in <Scripts>: the handler is the element's own
// field `self[method]`, usually a function from its mixin (Xml::Mixin), which
// is in place before any of the element's nodes load.

namespace Xml::Method {

// The registry ref of the function `object[method]`, the object being the
// table at the absolute stack index `index`; 0, with a load warning naming
// `owner` and the handler `element`, when that field is not a function.
int Ref(void *L, int index, const char *method, void *status, const char *owner, const char *element);

} // namespace Xml::Method
