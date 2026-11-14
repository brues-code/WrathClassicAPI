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

// `<KeyValues>` sets fields on an element's Lua table at load time:
//
//     <Frame name="Foo">
//         <KeyValues>
//             <KeyValue key="layoutIndex" value="3" type="number"/>
//             <KeyValue key="tooltip" value="GameTooltip" type="global"/>
//         </KeyValues>
//     </Frame>
//
// Each entry is `object[key] = value`. `type` and `keyType` (default
// "string") are nil, boolean, number, string or global; "global" is
// `_G[text]`, a dotted path walked a field at a time
// ("ScrollControllerMixin.Directions.Decrease"), nil when a step is not a
// table. Booleans and numbers go through the loaders' own reads
// (StringToBOOL, SStrToFloat).
//
// Each node of an element applies its own entries when it loads, and the
// loaders load the templates' nodes first, so the element's own values win.
// All of it runs before OnLoad.

#include "xml/KeyValues.h"

#include "Game.h"
#include "Offsets.h"
#include "xml/Node.h"

#include <cstring>
#include <string>

namespace Xml::KeyValues {

namespace {

enum class Kind { Nil, Boolean, Number, String, Global, Invalid };

// A missing type is the schema's default, "string".
Kind KindOf(const char *type) {
    if (type == nullptr || _stricmp(type, "string") == 0)
        return Kind::String;
    if (_stricmp(type, "nil") == 0)
        return Kind::Nil;
    if (_stricmp(type, "boolean") == 0)
        return Kind::Boolean;
    if (_stricmp(type, "number") == 0)
        return Kind::Number;
    if (_stricmp(type, "global") == 0)
        return Kind::Global;
    return Kind::Invalid;
}

void PushGlobalPath(void *L, const char *path) {
    Game::Lua::PushValue(L, Game::Lua::GLOBALS_INDEX);
    for (const char *start = path;;) {
        if (Game::Lua::Type(L, -1) != Game::Lua::TYPE_TABLE) {
            Game::Lua::SetTop(L, -2);
            Game::Lua::PushNil(L);
            return;
        }
        const char *dot = std::strchr(start, '.');
        const std::string field = dot != nullptr ? std::string(start, dot) : std::string(start);
        Game::Lua::GetField(L, -1, field.c_str());
        Game::Lua::Remove(L, -2);
        if (dot == nullptr)
            return;
        start = dot + 1;
    }
}

void Push(void *L, Kind kind, const char *text) {
    switch (kind) {
    case Kind::Boolean:
        Game::Lua::PushBool(L, reinterpret_cast<int(__cdecl *)(const char *)>(Offsets::FUN_STRING_TO_BOOL)(text) != 0);
        break;
    case Kind::Number:
        Game::Lua::PushNumber(L, reinterpret_cast<float(__stdcall *)(const char *)>(Offsets::FUN_SSTR_TO_FLOAT)(text));
        break;
    case Kind::String:
        Game::Lua::PushString(L, text);
        break;
    case Kind::Global:
        PushGlobalPath(L, text);
        break;
    default:
        Game::Lua::PushNil(L);
        break;
    }
}

} // namespace

void Apply(void *L, int index, const void *node, void *status, const char *owner) {
    const void *keyValues = Child(node, "KeyValues");
    if (keyValues == nullptr)
        return;
    const int top = Game::Lua::GetTop(L);
    for (const void *e = FirstChild(keyValues); e != nullptr; e = NextSibling(e)) {
        if (!TagIs(e, "KeyValue")) {
            Warn(status, "%s: Unknown KeyValues element %s", owner, Tag(e));
            continue;
        }
        const char *key = Attr(e, "key");
        if (key == nullptr) {
            Warn(status, "%s: KeyValue element missing key", owner);
            continue;
        }
        const char *keyTypeName = Attr(e, "keyType");
        const char *typeName = Attr(e, "type");
        const Kind keyKind = KindOf(keyTypeName);
        const Kind kind = KindOf(typeName);
        if (keyKind == Kind::Invalid || kind == Kind::Invalid) {
            Warn(status, "%s: KeyValue %s has unknown type %s", owner, key,
                 keyKind == Kind::Invalid ? keyTypeName : typeName);
            continue;
        }
        const char *value = Attr(e, "value");
        if (value == nullptr && kind != Kind::Nil) {
            Warn(status, "%s: KeyValue %s missing value", owner, key);
            continue;
        }
        Push(L, keyKind, key); // [key]
        if (Game::Lua::Type(L, -1) == Game::Lua::TYPE_NIL) {
            Warn(status, "%s: KeyValue %s has a nil key", owner, key);
            Game::Lua::SetTop(L, top);
            continue;
        }
        Push(L, kind, value);          // [key, value]
        Game::Lua::SetTable(L, index); // object[key] = value
        Game::Lua::SetTop(L, top);
    }
}

void PushInfo(void *L, const void *node) {
    Game::Lua::NewTable(L); // [list]
    const void *keyValues = Child(node, "KeyValues");
    if (keyValues == nullptr)
        return;
    int n = 0;
    for (const void *e = FirstChild(keyValues); e != nullptr; e = NextSibling(e)) {
        const char *key = Attr(e, "key");
        if (!TagIs(e, "KeyValue") || key == nullptr)
            continue;
        const char *keyType = Attr(e, "keyType");
        const char *type = Attr(e, "type");
        const char *value = Attr(e, "value");
        Game::Lua::PushNumber(L, static_cast<double>(++n)); // [list, n]
        Game::Lua::NewTable(L);                             // [list, n, entry]
        Game::Lua::SetFieldString(L, "key", key);
        Game::Lua::SetFieldString(L, "keyType", keyType != nullptr ? keyType : "string");
        Game::Lua::SetFieldString(L, "type", type != nullptr ? type : "string");
        Game::Lua::SetFieldString(L, "value", value != nullptr ? value : "");
        Game::Lua::SetTable(L, -3); // list[n] = entry
    }
}

} // namespace Xml::KeyValues
