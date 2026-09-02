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

// `C_XMLUtil.*` — introspect the engine's virtual XML frame templates (the
// `<Frame virtual="true">` etc. that `inherits=` targets).
//
//   GetTemplates()          -> { { name, type }, ... } over every registered template
//   GetTemplateInfo("name") -> { type, width, height, keyValues, inherits } or nil
//   DoesTemplateExist("name") -> bool
//
// The engine keeps virtual templates in a Storm hash table (see the Virtual XML
// templates block in Offsets.h). The by-name calls go through the engine's own
// hash lookup — the same path `inherits=` resolves through — and GetTemplates
// walks the bucket array directly. Fonts are a separate registry and are (as on
// retail) not included; non-virtual named frames take a different instantiate
// path and never enter this table. Contents reflect the currently-loaded XML and
// are rebuilt on `/reload`.

#include "Game.h"
#include "Offsets.h"

#include <cstdint>
#include <cstdlib>

namespace Xml::Templates {

namespace {

// --- engine helpers (both __thiscall; see Offsets.h) --------------------------
using StormLookup_t = const uint8_t *(__thiscall *)(const void *table, const char *name);
using GetAttr_t = const char *(__thiscall *)(const void *node, const char *name);

template <typename T> T ReadAt(const uint8_t *p, unsigned off) {
    return *reinterpret_cast<const T *>(p + off);
}

// ASCII case-insensitive equality — for XML tag names ("Size", "AbsDimension"),
// which are always ASCII, this matches the engine's own SStrCmpI without a
// second engine offset or a CRT dependency.
bool TagIEquals(const char *a, const char *b) {
    if (a == nullptr || b == nullptr)
        return false;
    for (;; ++a, ++b) {
        unsigned char ca = static_cast<unsigned char>(*a);
        unsigned char cb = static_cast<unsigned char>(*b);
        if (ca >= 'A' && ca <= 'Z')
            ca += 'a' - 'A';
        if (cb >= 'A' && cb <= 'Z')
            cb += 'a' - 'A';
        if (ca != cb)
            return false;
        if (ca == '\0')
            return true;
    }
}

// --- template registry access -------------------------------------------------

// Registry node for `name`, or null if no template of that name is registered
// (case-insensitive, hashed — the same lookup `inherits=` uses).
const uint8_t *LookupRegistryNode(const char *name) {
    return reinterpret_cast<StormLookup_t>(Offsets::FUN_STORM_HASH_LOOKUP)(
        reinterpret_cast<const void *>(Offsets::VAR_XML_TEMPLATE_OBJECT), name);
}

// Parsed definition (XML element) node for a template, or null if unregistered.
const uint8_t *LookupTemplateDef(const char *name) {
    const uint8_t *node = LookupRegistryNode(name);
    if (node == nullptr)
        return nullptr;
    return ReadAt<const uint8_t *>(node, Offsets::OFF_XML_TEMPLATE_NODE_DEF);
}

// --- XML DOM node helpers -----------------------------------------------------

const uint8_t *FirstChild(const uint8_t *node) {
    return ReadAt<const uint8_t *>(node, Offsets::OFF_XML_NODE_CHILD);
}
const uint8_t *NextSibling(const uint8_t *node) {
    return ReadAt<const uint8_t *>(node, Offsets::OFF_XML_NODE_SIBLING);
}
const char *NodeTag(const uint8_t *node) {
    return ReadAt<const char *>(node, Offsets::OFF_XML_NODE_TAG);
}
const char *Attr(const uint8_t *node, const char *name) {
    return reinterpret_cast<GetAttr_t>(Offsets::FUN_XML_NODE_GET_ATTRIBUTE)(node, name);
}

// First direct child of `node` whose element tag matches `tag`, or null. The DOM
// sibling chain is plain null-terminated (only Storm hash buckets use the
// low-bit sentinel), so a null check ends the walk.
const uint8_t *FindChild(const uint8_t *node, const char *tag) {
    for (const uint8_t *c = FirstChild(node); c != nullptr; c = NextSibling(c))
        if (TagIEquals(NodeTag(c), tag))
            return c;
    return nullptr;
}

// The statically-declared width/height from a `<Size>` child (0 if none).
// Handles the inline `<Size x= y=>` form and the `<Size><AbsDimension x= y=>`
// (or RelDimension) child form. Reports the RAW declared values — retail's
// GetTemplateInfo returns the declared value, so unlike the engine's own Size
// parser (FUN_00815740) we deliberately skip the UI-scale conversion.
void ReadSize(const uint8_t *node, double *outW, double *outH) {
    *outW = 0.0;
    *outH = 0.0;
    const uint8_t *size = FindChild(node, "Size");
    if (size == nullptr)
        return;
    const char *sx = Attr(size, "x");
    const char *sy = Attr(size, "y");
    const uint8_t *dim = FirstChild(size);
    if (dim != nullptr &&
        (TagIEquals(NodeTag(dim), "AbsDimension") || TagIEquals(NodeTag(dim), "RelDimension"))) {
        const char *dx = Attr(dim, "x");
        const char *dy = Attr(dim, "y");
        if (dx != nullptr && *dx != '\0')
            sx = dx;
        if (dy != nullptr && *dy != '\0')
            sy = dy;
    }
    if (sx != nullptr && *sx != '\0')
        *outW = std::strtod(sx, nullptr);
    if (sy != nullptr && *sy != '\0')
        *outH = std::strtod(sy, nullptr);
}

// --- Lua entry points ---------------------------------------------------------

// `C_XMLUtil.GetTemplates()` -> XMLTemplateListInfo[] of { name, type }.
int __cdecl Script_GetTemplates(void *L) {
    Game::Lua::NewTable(L); // the result array

    const uint32_t mask = *reinterpret_cast<const uint32_t *>(Offsets::VAR_XML_TEMPLATE_MASK);
    if (mask == 0xFFFFFFFFu)
        return 1; // no template has ever registered — empty array

    const uint8_t *base =
        *reinterpret_cast<const uint8_t *const *>(Offsets::VAR_XML_TEMPLATE_TABLE);
    if (base == nullptr)
        return 1;

    int outIdx = 0;
    for (uint32_t b = 0; b <= mask; ++b) {
        const uint8_t *bucket = base + b * Offsets::XML_TEMPLATE_BUCKET_STRIDE;
        const int linkOff = ReadAt<int>(bucket, Offsets::OFF_XML_TEMPLATE_BUCKET_LINKOFF);
        const uint8_t *node = ReadAt<const uint8_t *>(bucket, Offsets::OFF_XML_TEMPLATE_BUCKET_HEAD);

        // Walk the intrusive chain; the tail sentinel has its low bit set
        // (mirrors the engine's own traversal in FUN_0055F4D0).
        while (node != nullptr && (reinterpret_cast<uintptr_t>(node) & 1) == 0) {
            const char *name = ReadAt<const char *>(node, Offsets::OFF_XML_TEMPLATE_NODE_NAME);
            const uint8_t *def = ReadAt<const uint8_t *>(node, Offsets::OFF_XML_TEMPLATE_NODE_DEF);
            const char *type = (def != nullptr) ? NodeTag(def) : nullptr;

            ++outIdx;
            Game::Lua::PushNumber(L, static_cast<double>(outIdx));
            Game::Lua::NewTable(L);
            Game::Lua::SetFieldString(L, "name", name != nullptr ? name : "");
            Game::Lua::SetFieldString(L, "type", type != nullptr ? type : "");
            Game::Lua::SetTable(L, -3);

            node = *reinterpret_cast<const uint8_t *const *>(node + linkOff + 4);
        }
    }
    return 1;
}

// `C_XMLUtil.GetTemplateInfo(name)` -> XMLTemplateInfo table, or nil if the
// template doesn't exist.
int __cdecl Script_GetTemplateInfo(void *L) {
    if (!Game::Lua::IsString(L, 1)) {
        Game::Lua::Error(L, "Usage: C_XMLUtil.GetTemplateInfo(\"name\")");
        return 0;
    }
    const char *name = Game::Lua::ToString(L, 1);
    const uint8_t *node = (name != nullptr) ? LookupTemplateDef(name) : nullptr;
    if (node == nullptr)
        return 0; // nil — no such template

    Game::Lua::NewTable(L); // XMLTemplateInfo

    const char *type = NodeTag(node);
    Game::Lua::SetFieldString(L, "type", type != nullptr ? type : "");

    double w = 0.0, h = 0.0;
    ReadSize(node, &w, &h);
    Game::Lua::SetFieldNumber(L, "width", w);
    Game::Lua::SetFieldNumber(L, "height", h);

    // keyValues: vanilla's XML schema has no <KeyValues> element, so this is
    // always an empty table (parity with retail's "empty if none defined").
    Game::Lua::PushString(L, "keyValues");
    Game::Lua::NewTable(L);
    Game::Lua::SetTable(L, -3);

    // inherits: the comma-delimited `inherits=` attribute, or nil (field left
    // unset) when the template inherits nothing.
    const char *inherits = Attr(node, "inherits");
    if (inherits != nullptr && *inherits != '\0')
        Game::Lua::SetFieldString(L, "inherits", inherits);

    // sourceLocation is intentionally omitted (nil): 3.3.5 records no file/line
    // for XML nodes.
    return 1;
}

// `C_XMLUtil.DoesTemplateExist(name)` -> bool.
int __cdecl Script_DoesTemplateExist(void *L) {
    if (!Game::Lua::IsString(L, 1)) {
        Game::Lua::Error(L, "Usage: C_XMLUtil.DoesTemplateExist(\"name\")");
        return 0;
    }
    const char *name = Game::Lua::ToString(L, 1);
    const uint8_t *node = (name != nullptr) ? LookupRegistryNode(name) : nullptr;
    Game::Lua::PushBool(L, node != nullptr);
    return 1;
}

void RegisterLuaFunctions() {
    Game::Lua::RegisterTableFunction("C_XMLUtil", "GetTemplates", &Script_GetTemplates);
    Game::Lua::RegisterTableFunction("C_XMLUtil", "GetTemplateInfo", &Script_GetTemplateInfo);
    Game::Lua::RegisterTableFunction("C_XMLUtil", "DoesTemplateExist", &Script_DoesTemplateExist);
}

const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace

} // namespace Xml::Templates
