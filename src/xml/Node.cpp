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

#include "xml/Node.h"

#include "Offsets.h"

#include <cstdint>
#include <cstring>

namespace Xml {

namespace {

template <typename T> T At(const void *p, unsigned off) {
    return *reinterpret_cast<const T *>(static_cast<const uint8_t *>(p) + off);
}

} // namespace

const void *FirstChild(const void *node) { return At<const void *>(node, Offsets::OFF_XML_NODE_CHILD); }

const void *NextSibling(const void *node) { return At<const void *>(node, Offsets::OFF_XML_NODE_SIBLING); }

const char *Tag(const void *node) { return At<const char *>(node, Offsets::OFF_XML_NODE_TAG); }

bool TagIs(const void *node, const char *tag) {
    const char *t = Tag(node);
    return t != nullptr && _stricmp(t, tag) == 0;
}

const void *Child(const void *node, const char *tag) {
    for (const void *c = FirstChild(node); c != nullptr; c = NextSibling(c))
        if (TagIs(c, tag))
            return c;
    return nullptr;
}

const char *Attr(const void *node, const char *name) {
    return reinterpret_cast<const char *(__thiscall *)(const void *, const char *)>(
        Offsets::FUN_XML_NODE_GET_ATTRIBUTE)(node, name);
}

const char *Body(const void *node) { return At<const char *>(node, Offsets::OFF_XML_NODE_BODY); }

const void *Template(const char *name) {
    const void *entry = reinterpret_cast<const void *(__thiscall *)(const void *, const char *)>(
        Offsets::FUN_STORM_HASH_LOOKUP)(reinterpret_cast<const void *>(Offsets::VAR_XML_TEMPLATE_OBJECT), name);
    return entry != nullptr ? At<const void *>(entry, Offsets::OFF_XML_TEMPLATE_NODE_DEF) : nullptr;
}

void Warn(void *status, const char *fmt, const char *a, const char *b, const char *c) {
    if (status == nullptr)
        return;
    using Add_t = void(__cdecl *)(void *status, int level, const char *fmt, ...);
    const void *vtable = At<const void *>(status, 0);
    At<Add_t>(vtable, Offsets::VTBL_XML_STATUS_ADD)(status, Offsets::XML_STATUS_WARNING, fmt, a, b, c);
}

} // namespace Xml
