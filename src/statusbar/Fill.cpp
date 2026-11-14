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

// The StatusBar fill: `bar:SetReverseFill` / `GetReverseFill`,
// `SetFillStyle` / `GetFillStyle` and `Enum.StatusBarFillStyle`, the XML
// `reverseFill` and `fillStyle` attributes, `SetColorFill`, and
// `SetStatusBarDesaturated` / `IsStatusBarDesaturated`
// (SimpleStatusBarAPIDocumentation.lua; the port of ClassicAPI's
// src/statusbar/Fill.cpp and BarTexture.cpp).
//
// CSimpleStatusBar::OnLayerUpdate (0x00961B80) stretches the whole bar image
// over the filled part: it moves the texture's four points in from the bar's
// right (top, when vertical) by the empty part, sets no texture coordinates,
// and hides the texture at 0. A bar with a fill mode gets the same layout with
// the filled part placed by the mode:
//
//     Standard            [0, v] from the left (bottom)
//     Reverse / reverse   [1 - v, 1]
//     Center              [(1 - v) / 2, (1 + v) / 2]
//     StandardNoRangeFill as Standard, full for an empty range unless min
//                         and max are both 0
//
// where v is the bar's GetAnimValue. A bar in the default mode runs the
// engine's update unchanged.
//
// The modes live in a table by bar, for every StatusBar however it was made
// (nameplates build theirs directly, and CGSimpleHealthBar derives from the
// class), and leave it when the bar dies (statusbar/Lifetime).
//
// The update hook first advances the bar's ease (statusbar/Display), whose
// fraction GetAnimValue answers, so an easing bar keeps its fill mode.
//
// SetRotatesTexture is the engine's, made safe for a bar with no texture
// (the engine's dereferences it): the flag is stored, and the bar lays out
// again.

#include "Game.h"
#include "Offsets.h"
#include "statusbar/Display.h"
#include "statusbar/Lifetime.h"
#include "statusbar/Methods.h"
#include "ui/FrameObject.h"
#include "xml/Node.h"

#include <cstdint>
#include <unordered_map>

namespace StatusBar::Fill {

namespace {

template <typename T> T &At(void *base, uintptr_t offset) {
    return *reinterpret_cast<T *>(static_cast<uint8_t *>(base) + offset);
}

// Enum.StatusBarFillStyle.
enum FillStyle : int {
    STYLE_STANDARD = 0,
    STYLE_STANDARD_NO_RANGE_FILL = 1,
    STYLE_CENTER = 2,
    STYLE_REVERSE = 3,
};

const Game::Lua::EnumIntegerEntry kFillStyles[] = {
    {"Standard", STYLE_STANDARD},
    {"StandardNoRangeFill", STYLE_STANDARD_NO_RANGE_FILL},
    {"Center", STYLE_CENTER},
    {"Reverse", STYLE_REVERSE},
};

// The names XML's `fillStyle` takes, which SetFillStyle takes too.
const char *const kFillStyleNames[] = {"STANDARD", "STANDARD_NO_RANGE_FILL", "CENTER", "REVERSE"};

struct Mode {
    bool reverseFill = false;
    int fillStyle = STYLE_STANDARD;
    bool IsDefault() const { return !reverseFill && fillStyle == STYLE_STANDARD; }
};

// Every bar given a mode; dropped when the bar dies.
std::unordered_map<const void *, Mode> g_modes;

const Mode *Find(const void *bar) {
    const auto it = g_modes.find(bar);
    return it != g_modes.end() ? &it->second : nullptr;
}

uint32_t &Bits(void *bar) { return At<uint32_t>(bar, Offsets::OFF_STATUSBAR_BITS); }
void *BarTexture(void *bar) { return At<void *>(bar, Offsets::OFF_STATUSBAR_BAR_TEXTURE); }
void *LayoutOf(void *region) { return static_cast<uint8_t *>(region) + Offsets::OFF_REGION_LAYOUT; }

// Changes the bar's mode; it lays out again at its next update.
template <typename Change> void Modify(void *bar, Change change) {
    change(g_modes[bar]);
    Bits(bar) |= Offsets::STATUSBAR_CHANGED;
}

// ---- the layout ---------------------------------------------------------------------

using OnLayerUpdate_t = void(__fastcall *)(void *bar, void *, float elapsedSec);
OnLayerUpdate_t g_onLayerUpdate = nullptr;

void SetPoint(void *texture, int point, void *bar, float x, float y) {
    using SetPoint_t = void(__fastcall *)(void *layout, void *, int point, void *relative, int relativePoint, float x,
                                          float y, int doResize);
    reinterpret_cast<SetPoint_t>(Offsets::FUN_LAYOUT_SET_POINT)(LayoutOf(texture), nullptr, point, LayoutOf(bar),
                                                                point, x, y, 1);
}

// The bar's GetAnimValue, with StandardNoRangeFill's full bar for an empty range.
float FillFraction(void *bar, int fillStyle) {
    const float min = At<float>(bar, Offsets::OFF_STATUSBAR_MIN);
    const float max = At<float>(bar, Offsets::OFF_STATUSBAR_MAX);
    if (fillStyle == STYLE_STANDARD_NO_RANGE_FILL && !(max - min > 0.0f) && (min != 0.0f || max != 0.0f))
        return 1.0f;
    using GetAnimValue_t = float(__fastcall *)(void *bar, void *);
    return reinterpret_cast<GetAnimValue_t>(
        At<const void *const *>(bar, 0)[Offsets::VTBL_STATUSBAR_GET_ANIM_VALUE / 4])(bar, nullptr);
}

// CSimpleStatusBar::OnLayerUpdate's layout, the filled part placed by `mode`.
void LayOut(void *bar, void *texture, const Mode &mode) {
    using Region_t = void(__fastcall *)(void *region);
    const float v = FillFraction(bar, mode.fillStyle);
    if (!(v > 0.0f)) {
        At<uint8_t>(texture, Offsets::OFF_REGION_SHOWN_FLAGS) &= ~static_cast<uint8_t>(Offsets::REGION_SHOWN_BIT);
        reinterpret_cast<Region_t>(Offsets::FUN_REGION_HIDE_UPDATE)(texture);
        return;
    }
    float width = 0.0f, height = 0.0f;
    void *layout = LayoutOf(bar);
    using GetSize_t = void(__fastcall *)(void *layout, void *, float *width, float *height, int);
    reinterpret_cast<GetSize_t>(At<const void *const *>(layout, 0)[Offsets::VTBL_LAYOUT_GET_SIZE / 4])(
        layout, nullptr, &width, &height, 0);
    At<uint8_t>(texture, Offsets::OFF_REGION_SHOWN_FLAGS) |= static_cast<uint8_t>(Offsets::REGION_SHOWN_BIT);
    reinterpret_cast<Region_t>(Offsets::FUN_REGION_SHOW_UPDATE)(texture);

    // [s0, s1]: the filled part along the fill axis, from its start edge.
    float s0 = 0.0f, s1 = v;
    if (mode.fillStyle == STYLE_CENTER) {
        s0 = (1.0f - v) * 0.5f;
        s1 = (1.0f + v) * 0.5f;
    } else if (mode.reverseFill || mode.fillStyle == STYLE_REVERSE) {
        s0 = 1.0f - v;
        s1 = 1.0f;
    }
    float left = 0.0f, right = 0.0f, top = 0.0f, bottom = 0.0f;
    if (At<int32_t>(bar, Offsets::OFF_STATUSBAR_ORIENTATION) == 1) {
        bottom = s0 * height;
        top = -((1.0f - s1) * height);
    } else {
        left = s0 * width;
        right = -((1.0f - s1) * width);
    }
    SetPoint(texture, Offsets::FRAMEPOINT_TOPLEFT, bar, left, top);
    SetPoint(texture, Offsets::FRAMEPOINT_TOPRIGHT, bar, right, top);
    SetPoint(texture, Offsets::FRAMEPOINT_BOTTOMLEFT, bar, left, bottom);
    SetPoint(texture, Offsets::FRAMEPOINT_BOTTOMRIGHT, bar, right, bottom);
}

void __fastcall OnLayerUpdate_h(void *bar, void *, float elapsedSec) {
    StatusBar::Display::Advance(bar, elapsedSec);
    const Mode *mode = Find(bar);
    if (mode == nullptr || mode->IsDefault()) {
        g_onLayerUpdate(bar, nullptr, elapsedSec);
        return;
    }
    reinterpret_cast<OnLayerUpdate_t>(Offsets::FUN_SIMPLEFRAME_ON_LAYER_UPDATE)(bar, nullptr, elapsedSec);
    constexpr uint32_t kReady = Offsets::STATUSBAR_CHANGED | Offsets::STATUSBAR_RANGE_SET | Offsets::STATUSBAR_VALUE_SET;
    void *texture = BarTexture(bar);
    if ((Bits(bar) & kReady) != kReady || texture == nullptr)
        return;
    LayOut(bar, texture, *mode);
    Bits(bar) &= ~Offsets::STATUSBAR_CHANGED;
}

const Game::HookAutoRegister _onLayerUpdateHook{Offsets::FUN_STATUSBAR_ON_LAYER_UPDATE,
                                                reinterpret_cast<void *>(&OnLayerUpdate_h),
                                                reinterpret_cast<void **>(&g_onLayerUpdate)};

void Forget(const void *bar) { g_modes.erase(bar); }

const StatusBar::Lifetime::OnDestroyed _destroyed{&Forget};

// ---- XML ----------------------------------------------------------------------------

int StyleForName(const char *name) {
    for (int i = 0; i < static_cast<int>(sizeof(kFillStyleNames) / sizeof(kFillStyleNames[0])); ++i)
        if (_stricmp(name, kFillStyleNames[i]) == 0)
            return i;
    return -1;
}

using LoadXML_t = void(__fastcall *)(void *layout, void *, const void *node, void *status);
LoadXML_t g_loadXML = nullptr;

// The engine's loader, then `reverseFill` and `fillStyle`, each applied only
// when present and non-empty, so a template's stays unless the element names
// its own. An unknown fillStyle name changes nothing.
void __fastcall LoadXML_h(void *layout, void *, const void *node, void *status) {
    g_loadXML(layout, nullptr, node, status);
    if (node == nullptr)
        return;
    void *bar = static_cast<uint8_t *>(layout) - Offsets::OFF_FRAME_LAYOUT;
    if (const char *v = Xml::Attr(node, "reverseFill"); v != nullptr && *v != '\0') {
        const bool reverse = reinterpret_cast<int(__cdecl *)(const char *)>(Offsets::FUN_STRING_TO_BOOL)(v) != 0;
        Modify(bar, [reverse](Mode &m) { m.reverseFill = reverse; });
    }
    if (const char *v = Xml::Attr(node, "fillStyle"); v != nullptr && *v != '\0')
        if (const int style = StyleForName(v); style >= 0)
            Modify(bar, [style](Mode &m) { m.fillStyle = style; });
}

const Game::HookAutoRegister _loadXMLHook{Offsets::FUN_STATUSBAR_LOAD_XML, reinterpret_cast<void *>(&LoadXML_h),
                                          reinterpret_cast<void **>(&g_loadXML)};

// ---- the methods --------------------------------------------------------------------

int TypeId() {
    int &id = At<int>(reinterpret_cast<void *>(Offsets::VAR_STATUSBAR_OBJECT_TYPE), 0);
    if (id == 0)
        id = ++At<int>(reinterpret_cast<void *>(Offsets::VAR_SCRIPTOBJECT_TYPE_COUNTER), 0);
    return id;
}

void *Self(void *L) { return UI::FrameObject::ResolveSelf(L, TypeId()); }

const char *FrameName(void *frame) {
    const char *name = reinterpret_cast<const char *(__fastcall *)(void *, void *)>(
        At<const void *const *>(frame, 0)[Offsets::VTBL_FRAME_GET_NAME / 4])(frame, nullptr);
    return name != nullptr && *name != '\0' ? name : "<unnamed>";
}

// A flag argument as the engine's setters read one; none or nil is false.
bool Flag(void *L, int index) {
    return reinterpret_cast<int(__cdecl *)(void *, int, int)>(Offsets::FUN_LUA_TO_BOOL)(L, index, 0) != 0;
}

int __cdecl Script_SetReverseFill(void *L) {
    void *bar = Self(L);
    if (bar == nullptr)
        return 0;
    const bool reverse = Flag(L, 2);
    Modify(bar, [reverse](Mode &m) { m.reverseFill = reverse; });
    return 0;
}

int __cdecl Script_GetReverseFill(void *L) {
    void *bar = Self(L);
    if (bar == nullptr)
        return 0;
    const Mode *mode = Find(bar);
    Game::Lua::PushBool(L, mode != nullptr && mode->reverseFill);
    return 1;
}

// An Enum.StatusBarFillStyle value, or one of XML's names.
int __cdecl Script_SetFillStyle(void *L) {
    void *bar = Self(L);
    if (bar == nullptr)
        return 0;
    int style = -1;
    if (Game::Lua::Type(L, 2) == Game::Lua::TYPE_NUMBER) {
        const double n = Game::Lua::ToNumber(L, 2);
        if (n >= STYLE_STANDARD && n <= STYLE_REVERSE && n == static_cast<int>(n))
            style = static_cast<int>(n);
    } else if (Game::Lua::Type(L, 2) == Game::Lua::TYPE_STRING) {
        style = StyleForName(Game::Lua::ToString(L, 2));
    }
    if (style < 0) {
        Game::Lua::Error(L, "Usage: %s:SetFillStyle(fillStyle)", FrameName(bar));
        return 0;
    }
    Modify(bar, [style](Mode &m) { m.fillStyle = style; });
    return 0;
}

int __cdecl Script_GetFillStyle(void *L) {
    void *bar = Self(L);
    if (bar == nullptr)
        return 0;
    const Mode *mode = Find(bar);
    Game::Lua::PushNumber(L, mode != nullptr ? mode->fillStyle : STYLE_STANDARD);
    return 1;
}

// Script_SetRotatesTexture (0x009719D0), safe for a bar with no texture.
int __cdecl Script_SetRotatesTexture(void *L) {
    void *bar = Self(L);
    if (bar == nullptr)
        return 0;
    const bool rotates = Flag(L, 2);
    if (BarTexture(bar) != nullptr) {
        reinterpret_cast<void(__fastcall *)(void *, void *, int)>(Offsets::FUN_STATUSBAR_SET_ROTATES_TEXTURE)(
            bar, nullptr, rotates ? 1 : 0);
        return 0;
    }
    if (rotates)
        Bits(bar) |= Offsets::STATUSBAR_ROTATES_TEXTURE;
    else
        Bits(bar) &= ~Offsets::STATUSBAR_ROTATES_TEXTURE;
    Bits(bar) |= Offsets::STATUSBAR_CHANGED;
    return 0;
}

// FrameScript_GetColor's read (r, g, b clamped, a optional and 1 when
// missing), into the engine's colour.
uint32_t ReadColor(void *L, int first) {
    auto channel = [L](int index) {
        const float c = static_cast<float>(Game::Lua::ToNumber(L, index));
        return c < 0.0f ? 0.0f : (c >= 1.0f ? 1.0f : c);
    };
    const float a = Game::Lua::IsNumber(L, first + 3) ? channel(first + 3) : 1.0f;
    uint32_t color = 0;
    reinterpret_cast<void(__fastcall *)(uint32_t *, void *, float, float, float, float)>(Offsets::FUN_CIMVECTOR_SET)(
        &color, nullptr, a, channel(first), channel(first + 1), channel(first + 2));
    return color;
}

// A bar texture made as SetStatusBarTexture-by-path (0x00961AC0) makes one,
// on ARTWORK, less the file.
void *CreateBarTexture(void *bar) {
    using GetData_t = void *(__fastcall *)(void *heap, void *, int, const char *, int);
    using Constructor_t = void *(__fastcall *)(void *texture, void *, void *frame, uint32_t layer, int show);
    void *block = reinterpret_cast<GetData_t>(Offsets::FUN_DATA_ALLOCATOR_GET)(
        reinterpret_cast<void *>(Offsets::VAR_SIMPLETEXTURE_HEAP), nullptr, 0,
        reinterpret_cast<const char *>(Offsets::VAR_SIMPLETEXTURE_TYPE_NAME), -2);
    if (block == nullptr)
        return nullptr;
    void *texture = reinterpret_cast<Constructor_t>(Offsets::FUN_SIMPLETEXTURE_CONSTRUCTOR)(
        block, nullptr, nullptr, Offsets::DRAWLAYER_ARTWORK, 1);
    reinterpret_cast<void(__fastcall *)(void *, void *, void *, uint32_t)>(Offsets::FUN_STATUSBAR_SET_BAR_TEXTURE)(
        bar, nullptr, texture, Offsets::DRAWLAYER_ARTWORK);
    return texture;
}

// Fills the bar texture with a solid colour, making the texture when the bar
// has none.
int __cdecl Script_SetColorFill(void *L) {
    void *bar = Self(L);
    if (bar == nullptr)
        return 0;
    if (!Game::Lua::IsNumber(L, 2) || !Game::Lua::IsNumber(L, 3) || !Game::Lua::IsNumber(L, 4)) {
        Game::Lua::Error(L, "Usage: %s:SetColorFill(r, g, b [, a])", FrameName(bar));
        return 0;
    }
    const uint32_t color = ReadColor(L, 2);
    void *texture = BarTexture(bar);
    if (texture == nullptr)
        texture = CreateBarTexture(bar);
    if (texture != nullptr)
        reinterpret_cast<int(__fastcall *)(void *, void *, const uint32_t *)>(Offsets::FUN_SIMPLETEXTURE_SET_COLOR)(
            texture, nullptr, &color);
    return 0;
}

void *DesaturateShader() {
    return reinterpret_cast<void *(__cdecl *)(int)>(Offsets::FUN_IMAGE_MODE_PIXEL_SHADER)(
        Offsets::IMAGE_MODE_DESATURATE);
}

// Script_SetDesaturated (0x0048C9A0) on the bar texture; a bar with no
// texture does nothing.
int __cdecl Script_SetStatusBarDesaturated(void *L) {
    void *bar = Self(L);
    if (bar == nullptr)
        return 0;
    void *texture = BarTexture(bar);
    if (texture == nullptr)
        return 0;
    void *shader = Flag(L, 2) ? DesaturateShader()
                              : reinterpret_cast<void *(__cdecl *)(int)>(Offsets::FUN_IMAGE_MODE_PIXEL_SHADER)(0);
    reinterpret_cast<void(__fastcall *)(void *, void *, void *)>(Offsets::FUN_SIMPLETEXTURE_SET_SHADER)(texture, nullptr,
                                                                                                     shader);
    return 0;
}

// Script_IsDesaturated (0x0048CA30) on the bar texture; false with none.
int __cdecl Script_IsStatusBarDesaturated(void *L) {
    void *bar = Self(L);
    if (bar == nullptr)
        return 0;
    void *texture = BarTexture(bar);
    void *shader = DesaturateShader();
    Game::Lua::PushBool(L, texture != nullptr && shader != nullptr &&
                               At<void *>(texture, Offsets::OFF_SIMPLETEXTURE_SHADER) == shader);
    return 1;
}

// After the StatusBar's own, so SetRotatesTexture replaces the engine's.
const Frame::Methods::Entry kMethods[] = {
    {"SetReverseFill", &Script_SetReverseFill},
    {"GetReverseFill", &Script_GetReverseFill},
    {"SetFillStyle", &Script_SetFillStyle},
    {"GetFillStyle", &Script_GetFillStyle},
    {"SetRotatesTexture", &Script_SetRotatesTexture},
    {"SetColorFill", &Script_SetColorFill},
    {"SetStatusBarDesaturated", &Script_SetStatusBarDesaturated},
    {"IsStatusBarDesaturated", &Script_IsStatusBarDesaturated},
};

const StatusBar::Methods::AutoRegister _methods{kMethods,
                                                static_cast<int>(sizeof(kMethods) / sizeof(kMethods[0]))};

void RegisterLuaFunctions() {
    Game::Lua::RegisterIntegerEnum("Enum", "StatusBarFillStyle", kFillStyles,
                                   static_cast<int>(sizeof(kFillStyles) / sizeof(kFillStyles[0])));
}

const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace

} // namespace StatusBar::Fill
