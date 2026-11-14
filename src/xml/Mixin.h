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

// `mixin="A, B"` on XML elements: each name is a global table whose fields
// are copied onto the element's Lua table, as `Mixin(object, A, B)` does.

namespace Xml::Mixin {

// True when `node` or a template in its inherits chain names a mixin.
bool InChain(const void *node);

// Mixes the mixins of `node`'s inherits chain into the table at the absolute
// stack index `index`: each template's (its own chain first), in inherits
// order, then the node's own. A name that is not a global table is skipped
// with a load warning naming `owner`.
void ApplyChain(void *L, int index, const void *node, void *status, const char *owner);

} // namespace Xml::Mixin
