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

// The later-client XML features on frames, through one hook on
// CSimpleFrame::LoadXML, the base every frame type's loader calls first and
// the call that loads each `inherits` template (see Offsets.h):
//
//   mixin="A, B"            the frame's outermost load mixes in its whole
//                           chain before the engine loads anything (Xml::Mixin)
//   <KeyValues>             each node's entries, once the node has loaded
//   <OnX method="Name"/>    each node's handlers set to self.Name, once the
//                           engine's own <Scripts> pass has run
//
// Each node's part runs when that node's load returns, so the templates'
// nodes go first and the frame's own win, and all of it runs before OnLoad
// (PostLoadXML). A frame is given its Lua table here only when one of its
// nodes uses a feature: the engine's own lazy registration (UI::FrameObject).

#include "Game.h"
#include "Offsets.h"
#include "ui/FrameObject.h"
#include "xml/KeyValues.h"
#include "xml/Method.h"
#include "xml/Mixin.h"
#include "xml/Node.h"

#include <algorithm>
#include <cstdint>
#include <vector>

namespace Xml::FrameLoad {

namespace {

// FrameScript_Object::ScriptIx / ScriptData (LoadXML_Scripts 0x0048FEF0).
struct ScriptIx {
    int32_t luaRef;
    const char *taint;
};
struct ScriptData {
    const char *wrapper;
};

template <typename T> T VirtualAt(void *object, unsigned slot) {
    return *reinterpret_cast<T *>(*static_cast<uint8_t **>(object) + slot);
}

const char *FrameName(void *frame) {
    const char *name = VirtualAt<const char *(__thiscall *)(void *)>(frame, Offsets::VTBL_FRAME_GET_NAME)(frame);
    return name != nullptr ? name : "<unnamed>";
}

bool HasMethod(const void *scripts) {
    if (scripts == nullptr)
        return false;
    for (const void *c = FirstChild(scripts); c != nullptr; c = NextSibling(c))
        if (const char *m = Attr(c, "method"); m != nullptr && *m != '\0')
            return true;
    return false;
}

// The handlers of `scripts` that name a method, set to self[method] on the
// frame at stack index `index`.
void BindMethods(void *L, int index, void *frame, const void *scripts, void *status) {
    using GetScript_t = ScriptIx *(__thiscall *)(void *, const char *, ScriptData *);
    using Unref_t = void(__cdecl *)(void *, int, int);
    for (const void *c = FirstChild(scripts); c != nullptr; c = NextSibling(c)) {
        const char *method = Attr(c, "method");
        if (method == nullptr || *method == '\0')
            continue;
        ScriptData data{"return function(self) %s end"};
        ScriptIx *slot = VirtualAt<GetScript_t>(frame, Offsets::VTBL_FRAME_GET_SCRIPT_BY_NAME)(frame, Tag(c), &data);
        if (slot == nullptr)
            continue; // the engine's pass has warned about the element
        const int ref = Method::Ref(L, index, method, status, FrameName(frame), Tag(c));
        if (slot->luaRef > 0)
            reinterpret_cast<Unref_t>(Offsets::LUAL_UNREF)(L, Game::Lua::REGISTRY_INDEX, slot->luaRef);
        slot->luaRef = ref;
        slot->taint = nullptr;
    }
}

// Frames whose load is in progress; nesting is the inherits chain.
std::vector<void *> g_loading;

using Load_t = void(__fastcall *)(void *layout, void *edx, const void *node, void *status);
Load_t g_orig = nullptr;

void __fastcall LoadXML(void *layout, void *edx, const void *node, void *status) {
    if (layout == nullptr || node == nullptr) {
        g_orig(layout, edx, node, status);
        return;
    }
    void *frame = static_cast<uint8_t *>(layout) - Offsets::OFF_FRAME_LAYOUT;
    void *L = Game::Lua::State();
    const bool outer = std::find(g_loading.begin(), g_loading.end(), frame) == g_loading.end();
    if (outer && L != nullptr && Mixin::InChain(node)) {
        const int top = Game::Lua::GetTop(L);
        UI::FrameObject::Push(L, frame);
        Mixin::ApplyChain(L, top + 1, node, status, FrameName(frame));
        Game::Lua::SetTop(L, top);
    }

    g_loading.push_back(frame);
    g_orig(layout, edx, node, status);
    g_loading.pop_back();

    const void *scripts = Child(node, "Scripts");
    const bool keyValues = Child(node, "KeyValues") != nullptr;
    const bool methods = HasMethod(scripts);
    if (L == nullptr || (!keyValues && !methods))
        return;
    const int top = Game::Lua::GetTop(L);
    UI::FrameObject::Push(L, frame);
    if (keyValues)
        KeyValues::Apply(L, top + 1, node, status, FrameName(frame));
    if (methods)
        BindMethods(L, top + 1, frame, scripts, status);
    Game::Lua::SetTop(L, top);
}

const Game::HookAutoRegister _hook{Offsets::FUN_SIMPLEFRAME_LOAD_XML, reinterpret_cast<void *>(&LoadXML),
                                   reinterpret_cast<void **>(&g_orig)};

} // namespace

} // namespace Xml::FrameLoad
