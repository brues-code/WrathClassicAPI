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
#include "xml/KeyValues.h"
#include "xml/Node.h"

#include <cstdint>
#include <cstdlib>

namespace Xml::Templates {

namespace {

using StormLookup_t = const uint8_t *(__thiscall *)(const void *table, const char *name);

template <typename T> T ReadAt(const uint8_t *p, unsigned off) {
    return *reinterpret_cast<const T *>(p + off);
}

// Registry node for `name`, or null if no template of that name is registered
// (case-insensitive, hashed — the same lookup `inherits=` uses).
const uint8_t *LookupRegistryNode(const char *name) {
    return reinterpret_cast<StormLookup_t>(Offsets::FUN_STORM_HASH_LOOKUP)(
        reinterpret_cast<const void *>(Offsets::VAR_XML_TEMPLATE_OBJECT), name);
}

using Xml::Attr;
using Xml::FirstChild;
using Xml::TagIs;

// The statically-declared width/height from a `<Size>` child (0 if none).
// Handles the inline `<Size x= y=>` form and the `<Size><AbsDimension x= y=>`
// (or RelDimension) child form. Reports the RAW declared values — retail's
// GetTemplateInfo returns the declared value, so unlike the engine's own Size
// parser (FUN_00815740) we deliberately skip the UI-scale conversion.
void ReadSize(const void *node, double *outW, double *outH) {
    *outW = 0.0;
    *outH = 0.0;
    const void *size = Xml::Child(node, "Size");
    if (size == nullptr)
        return;
    const char *sx = Attr(size, "x");
    const char *sy = Attr(size, "y");
    const void *dim = FirstChild(size);
    if (dim != nullptr && (TagIs(dim, "AbsDimension") || TagIs(dim, "RelDimension"))) {
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
            const char *type = (def != nullptr) ? Xml::Tag(def) : nullptr;

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
    const void *node = (name != nullptr) ? Xml::Template(name) : nullptr;
    if (node == nullptr)
        return 0; // nil — no such template

    Game::Lua::NewTable(L); // XMLTemplateInfo

    const char *type = Xml::Tag(node);
    Game::Lua::SetFieldString(L, "type", type != nullptr ? type : "");

    double w = 0.0, h = 0.0;
    ReadSize(node, &w, &h);
    Game::Lua::SetFieldNumber(L, "width", w);
    Game::Lua::SetFieldNumber(L, "height", h);

    // keyValues: the template's own <KeyValues> entries, the strings as written.
    Game::Lua::PushString(L, "keyValues");
    Xml::KeyValues::PushInfo(L, node);
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
