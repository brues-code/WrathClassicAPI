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

// A mixin carries methods and default fields that <KeyValues> and the
// element's scripts build on, so every mixin of an element must be in place
// before any of its nodes load. The loaders load a template by re-entering
// themselves for the template node, and a template's KeyValues are set when
// that inner load returns; mixing in per node would put an element's own
// mixin after its template's KeyValues and overwrite their defaults. So the
// outermost load of an element applies the whole chain once, before the
// engine loads anything (Xml::FrameLoad for frames, Actor::LoadXml for
// actors).

#include "xml/Mixin.h"

#include "Game.h"
#include "baselib/Mixin.h"
#include "xml/Node.h"

namespace Xml::Mixin {

namespace {

// Deeper than any real chain; stops a template that inherits itself.
constexpr int kMaxDepth = 32;

bool HasMixin(const void *node) {
    const char *mixins = Attr(node, "mixin");
    return mixins != nullptr && *mixins != '\0';
}

bool InChainAt(const void *node, int depth) {
    if (depth > kMaxDepth)
        return false;
    if (HasMixin(node))
        return true;
    bool found = false;
    ForEachName(Attr(node, "inherits"), [&](const char *name) {
        if (!found)
            if (const void *t = Template(name))
                found = InChainAt(t, depth + 1);
    });
    return found;
}

void ApplyAt(void *L, int index, const void *node, void *status, const char *owner, int depth) {
    if (depth > kMaxDepth)
        return;
    ForEachName(Attr(node, "inherits"), [&](const char *name) {
        if (const void *t = Template(name))
            ApplyAt(L, index, t, status, owner, depth + 1);
    });
    ForEachName(Attr(node, "mixin"), [&](const char *name) {
        Game::Lua::GetGlobal(L, name); // [mixin]
        if (Game::Lua::Type(L, -1) == Game::Lua::TYPE_TABLE)
            BaseLib::Mixin::Into(L, index, Game::Lua::GetTop(L));
        else
            Warn(status, "%s: Couldn't find mixin %s", owner, name);
        Game::Lua::SetTop(L, -2);
    });
}

} // namespace

bool InChain(const void *node) { return InChainAt(node, 0); }

void ApplyChain(void *L, int index, const void *node, void *status, const char *owner) {
    ApplyAt(L, index, node, status, owner, 0);
}

} // namespace Xml::Mixin
