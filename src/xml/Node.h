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

// The engine's parsed XML nodes (XMLNode) and the loader pieces the XML
// features share.

#include <cstring>

namespace Xml {

// Children are an intrusive, null-terminated list.
const void *FirstChild(const void *node);
const void *NextSibling(const void *node);
const char *Tag(const void *node);
// Case-insensitive, as the engine's loaders compare tags.
bool TagIs(const void *node, const char *tag);
// The first direct child with that tag, or null.
const void *Child(const void *node, const char *tag);
// The engine's attribute getter: null when the attribute is absent.
const char *Attr(const void *node, const char *name);
// The element's text (a script body), or null.
const char *Body(const void *node);

// The virtual template `name` resolves to through `inherits=`, or null. It
// reads the template table without taking the template's load lock.
const void *Template(const char *name);

// The engine's load-status sink at STATUS_WARNING: the log line the loaders
// write ("Frame %s: Unknown script element %s"). `status` may be null.
void Warn(void *status, const char *fmt, const char *a, const char *b = nullptr,
          const char *c = nullptr);

// Calls fn(name) for each name in a list separated by spaces and commas, the
// way the loaders read `inherits`.
template <typename Fn> void ForEachName(const char *list, Fn &&fn) {
    if (list == nullptr)
        return;
    char name[1024];
    for (const char *p = list; *p != '\0';) {
        while (*p == ' ' || *p == ',')
            ++p;
        size_t n = 0;
        while (p[n] != '\0' && p[n] != ' ' && p[n] != ',')
            ++n;
        if (n == 0)
            break;
        if (n < sizeof name) {
            std::memcpy(name, p, n);
            name[n] = '\0';
            fn(static_cast<const char *>(name));
        }
        p += n;
    }
}

} // namespace Xml
