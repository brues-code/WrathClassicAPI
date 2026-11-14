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

// `Texture:SetDesaturation(amount)` / `GetDesaturation()`, with the engine's
// `SetDesaturated` and `IsDesaturated` (0x0048C9A0, 0x0048CA30) replaced to
// share the amount (TextureAPIDocumentation.lua).
//
// The engine desaturates by the texture's pixel shader: the stock Desaturate
// shader or the UI one, and the UI batch binds one shader per draw with no
// per-texture constants. So an amount between 0 and 1 is drawn as the texture
// plus a desaturated copy of it over it, the copy's alpha scaled by the amount:
// alpha blending mixes the two as colour * (1 - amount) + grey * amount. 0 is
// the UI shader alone and 1 the Desaturate shader alone, as SetDesaturated
// sets them.
//
//   - The copy is queued by the texture's own draw (QueueTexture): the same
//     renderable, corners and texcoords, the texture's colours with the alpha
//     scaled, and the Desaturate shader. It reads the texture's live state on
//     every batch rebuild, and its colours follow every colour change
//     (OnColorChanged), which a fade makes without a rebuild.
//   - A rebuilt batch is sorted by renderable, blend and shader address, so the
//     copy could sort under its texture. The sort hook keeps each partly
//     desaturated texture's two entries together, the copy second.
//   - The stock shader drops the vertex colour's RGB (as SetDesaturated(true)
//     does), so the grey part is untinted. Opaque pixels mix exactly; a pixel
//     with partial alpha lets a little more background through.
//   - Additive and modulating blends cannot mix by alpha: such a texture is
//     drawn desaturated from 0.5 up.
//   - A new image file sets the shader back to the UI one (SetTexture), so it
//     clears the amount too.

#include "texture/Desaturation.h"

#include "Game.h"
#include "Offsets.h"
#include "texture/Lifetime.h"
#include "texture/Methods.h"
#include "ui/FrameObject.h"

#include <cmath>
#include <cstdint>
#include <unordered_map>

namespace Texture::Desaturation {

namespace {

namespace Lua = Game::Lua;

template <typename T> T &At(void *base, uintptr_t offset) {
    return *reinterpret_cast<T *>(static_cast<uint8_t *>(base) + offset);
}
template <typename T> const T &At(const void *base, uintptr_t offset) {
    return *reinterpret_cast<const T *>(static_cast<const uint8_t *>(base) + offset);
}

// A texture between 0 and 1. One at 0 or 1 has no entry: its shader says it.
struct Partial {
    double amount = 0.0;
    uint32_t colors[4] = {}; // the copy's, BGRA; the batch keeps the pointer
};

std::unordered_map<const void *, Partial> g_partial;

void Forget(const void *texture) { g_partial.erase(texture); }

const Texture::Lifetime::OnDestroyed _destroyed{&Forget};

void ClearAll() { g_partial.clear(); }

const Game::ReloadAutoRegister _reload{&ClearAll};

// ---- engine calls -------------------------------------------------------------------

void *Shader(int mode) { return reinterpret_cast<void *(__cdecl *)(int)>(Offsets::FUN_IMAGE_MODE_PIXEL_SHADER)(mode); }

void SetShader(void *texture, void *shader) {
    reinterpret_cast<void(__fastcall *)(void *, void *, void *)>(Offsets::FUN_SIMPLETEXTURE_SET_SHADER)(texture, nullptr,
                                                                                                     shader);
}

void RegionChanged(void *texture) {
    reinterpret_cast<void(__fastcall *)(void *)>(Offsets::FUN_REGION_ON_REGION_CHANGED)(texture);
}

// The copy's colours: the texture's, each alpha scaled by the amount.
void Refresh(const void *texture, Partial &p) {
    const uint32_t count = At<uint32_t>(texture, Offsets::OFF_REGION_COLOR_COUNT);
    const uint32_t *colors = &At<uint32_t>(texture, Offsets::OFF_REGION_COLORS);
    for (int i = 0; i < 4; ++i) {
        const uint32_t c = count == 0 ? 0xFFFFFFFFu : colors[count > 1 ? i : 0];
        const auto alpha = static_cast<uint32_t>((c >> 24) * p.amount + 0.5);
        p.colors[i] = (c & 0x00FFFFFFu) | (alpha << 24);
    }
}

// ---- hooks ----------------------------------------------------------------------------

using QueueTexture_t = void(__fastcall *)(void *batch, void *, void *texture);
QueueTexture_t g_queueTexture = nullptr;

constexpr uint16_t kQuadIndices[6] = {0, 1, 2, 2, 1, 3};

void __fastcall QueueTexture_h(void *batch, void *, void *texture) {
    const auto it = g_partial.empty() ? g_partial.end() : g_partial.find(texture);
    void *desaturate = it != g_partial.end() ? Shader(Offsets::IMAGE_MODE_DESATURATE) : nullptr;
    if (desaturate == nullptr) {
        g_queueTexture(batch, nullptr, texture);
        return;
    }
    Partial &p = it->second;
    if (At<int>(texture, Offsets::OFF_SIMPLETEXTURE_BLEND) > Offsets::GXBLEND_ALPHA) {
        void *&shader = At<void *>(texture, Offsets::OFF_SIMPLETEXTURE_SHADER);
        void *const saved = shader;
        if (p.amount >= 0.5)
            shader = desaturate;
        g_queueTexture(batch, nullptr, texture);
        shader = saved;
        return;
    }
    const uint32_t before = At<uint32_t>(batch, Offsets::OFF_RENDER_BATCH_COUNT);
    g_queueTexture(batch, nullptr, texture);
    if (At<uint32_t>(batch, Offsets::OFF_RENDER_BATCH_COUNT) == before)
        return;
    Refresh(texture, p);
    using Queue_t = void(__fastcall *)(void *batch, void *, void *handle, int blend, int posCount, const void *pos,
                                       const void *texCoord, int colorCount, const void *color, int idxCount,
                                       const void *indices, void *shader);
    reinterpret_cast<Queue_t>(Offsets::FUN_RENDERBATCH_QUEUE)(
        batch, nullptr, At<void *>(texture, Offsets::OFF_SIMPLETEXTURE_HTEXTURE), Offsets::GXBLEND_ALPHA, 4,
        &At<uint8_t>(texture, Offsets::OFF_SIMPLETEXTURE_CORNERS),
        &At<uint8_t>(texture, Offsets::OFF_SIMPLETEXTURE_TEXCOORDS), 4, p.colors, 6, kQuadIndices, desaturate);
}

const Game::HookAutoRegister _queueTextureHook{Offsets::FUN_RENDERBATCH_QUEUE_TEXTURE,
                                               reinterpret_cast<void *>(&QueueTexture_h),
                                               reinterpret_cast<void **>(&g_queueTexture)};

using Sort_t = int(__cdecl *)(const void *, const void *);
Sort_t g_sort = nullptr;

int EffectiveBlend(const void *entry) {
    const int blend = At<int>(entry, Offsets::OFF_RENDER_ENTRY_BLEND);
    return At<const void *>(entry, Offsets::OFF_RENDER_ENTRY_COLORS) != nullptr && blend < Offsets::GXBLEND_ALPHA
               ? Offsets::GXBLEND_ALPHA
               : blend;
}

// The partly desaturated texture an entry draws, or 0. An entry's positions
// are its texture's corners.
uintptr_t Group(const void *entry) {
    const auto texture = reinterpret_cast<uintptr_t>(At<const void *>(entry, Offsets::OFF_RENDER_ENTRY_POSITIONS)) -
                         Offsets::OFF_SIMPLETEXTURE_CORNERS;
    return g_partial.find(reinterpret_cast<const void *>(texture)) != g_partial.end() ? texture : 0;
}

// The engine's order, then, among entries it would draw in one run: plain
// entries first, then each partly desaturated texture with its copy after it.
int __cdecl Sort_h(const void *a, const void *b) {
    if (g_partial.empty() ||
        At<const void *>(a, Offsets::OFF_RENDER_ENTRY_RENDERABLE) !=
            At<const void *>(b, Offsets::OFF_RENDER_ENTRY_RENDERABLE) ||
        EffectiveBlend(a) != EffectiveBlend(b))
        return g_sort(a, b);
    const uintptr_t ga = Group(a), gb = Group(b);
    if (ga != gb)
        return ga < gb ? -1 : 1;
    if (ga != 0) {
        void *desaturate = Shader(Offsets::IMAGE_MODE_DESATURATE);
        const bool ca = At<void *>(a, Offsets::OFF_RENDER_ENTRY_PIXEL_SHADER) == desaturate;
        const bool cb = At<void *>(b, Offsets::OFF_RENDER_ENTRY_PIXEL_SHADER) == desaturate;
        if (ca != cb)
            return ca ? 1 : -1;
    }
    return g_sort(a, b);
}

const Game::HookAutoRegister _sortHook{Offsets::FUN_RENDERBATCH_SORT_BY_TEXTURE, reinterpret_cast<void *>(&Sort_h),
                                       reinterpret_cast<void **>(&g_sort)};

using OnColorChanged_t = void(__fastcall *)(void *region, void *, int changed);
OnColorChanged_t g_onColorChanged = nullptr;

void __fastcall OnColorChanged_h(void *region, void *, int changed) {
    g_onColorChanged(region, nullptr, changed);
    if (g_partial.empty())
        return;
    if (const auto it = g_partial.find(region); it != g_partial.end())
        Refresh(region, it->second);
}

const Game::HookAutoRegister _onColorChangedHook{Offsets::FUN_REGION_ON_COLOR_CHANGED,
                                                 reinterpret_cast<void *>(&OnColorChanged_h),
                                                 reinterpret_cast<void **>(&g_onColorChanged)};

using SetTextureFile_t = int(__fastcall *)(void *texture, void *, const char *file, int wrapU, int wrapV, int filter,
                                           int mode);
SetTextureFile_t g_setTextureFile = nullptr;

int __fastcall SetTextureFile_h(void *texture, void *, const char *file, int wrapU, int wrapV, int filter, int mode) {
    void *const before = At<void *>(texture, Offsets::OFF_SIMPLETEXTURE_HTEXTURE);
    const int result = g_setTextureFile(texture, nullptr, file, wrapU, wrapV, filter, mode);
    if (file != nullptr && *file != '\0' && At<void *>(texture, Offsets::OFF_SIMPLETEXTURE_HTEXTURE) != before)
        g_partial.erase(texture);
    return result;
}

const Game::HookAutoRegister _setTextureFileHook{Offsets::FUN_SIMPLETEXTURE_SET_TEXTURE_FILE,
                                                 reinterpret_cast<void *>(&SetTextureFile_h),
                                                 reinterpret_cast<void **>(&g_setTextureFile)};

// ---- the methods ----------------------------------------------------------------------

int TypeId() {
    int &id = At<int>(reinterpret_cast<void *>(Offsets::VAR_SIMPLETEXTURE_OBJECT_TYPE), 0);
    if (id == 0)
        id = ++At<int>(reinterpret_cast<void *>(Offsets::VAR_SCRIPTOBJECT_TYPE_COUNTER), 0);
    return id;
}

void *Self(void *L) { return UI::FrameObject::ResolveSelf(L, TypeId()); }

const char *Name(void *texture) {
    const char *name = reinterpret_cast<const char *(__fastcall *)(void *, void *)>(
        At<const void *const *>(texture, 0)[Offsets::VTBL_FRAME_GET_NAME / 4])(texture, nullptr);
    return name != nullptr ? name : "<unnamed>";
}

int __cdecl Script_SetDesaturation(void *L) {
    void *texture = Self(L);
    if (texture == nullptr)
        return 0;
    if (!Lua::IsNumber(L, 2)) {
        Lua::Error(L, "Usage: %s:SetDesaturation(amount)", Name(texture));
        return 0;
    }
    Set(texture, Lua::ToNumber(L, 2));
    return 0;
}

int __cdecl Script_GetDesaturation(void *L) {
    void *texture = Self(L);
    if (texture == nullptr)
        return 0;
    Lua::PushNumber(L, Get(texture));
    return 1;
}

// Script_SetDesaturated: the flag defaults to true; 1 when the shader the flag
// asks for exists, else nil.
int __cdecl Script_SetDesaturated(void *L) {
    void *texture = Self(L);
    if (texture == nullptr)
        return 0;
    const bool on = reinterpret_cast<int(__cdecl *)(void *, int, int)>(Offsets::FUN_LUA_TO_BOOL)(L, 2, 1) != 0;
    Set(texture, on ? 1.0 : 0.0);
    if (Shader(on ? Offsets::IMAGE_MODE_DESATURATE : 0) != nullptr)
        Lua::PushNumber(L, 1.0);
    else
        Lua::PushNil(L);
    return 1;
}

// 1 while any of the colour is gone, else nil.
int __cdecl Script_IsDesaturated(void *L) {
    void *texture = Self(L);
    if (texture == nullptr)
        return 0;
    if (Get(texture) > 0.0)
        Lua::PushNumber(L, 1.0);
    else
        Lua::PushNil(L);
    return 1;
}

const Frame::Methods::Entry kMethods[] = {
    {"SetDesaturation", &Script_SetDesaturation},
    {"GetDesaturation", &Script_GetDesaturation},
    {"SetDesaturated", &Script_SetDesaturated},
    {"IsDesaturated", &Script_IsDesaturated},
};

const Texture::Methods::AutoRegister _methods{kMethods, static_cast<int>(sizeof(kMethods) / sizeof(kMethods[0]))};

} // namespace

double Get(const void *texture) {
    if (const auto it = g_partial.find(texture); it != g_partial.end())
        return it->second.amount;
    void *desaturate = Shader(Offsets::IMAGE_MODE_DESATURATE);
    return desaturate != nullptr && At<void *>(texture, Offsets::OFF_SIMPLETEXTURE_SHADER) == desaturate ? 1.0 : 0.0;
}

// As Script_SetDesaturated, for 0 and 1: the mode's shader, or none when the
// device has no such shader.
void Set(void *texture, double amount) {
    amount = amount > 0.0 ? (amount < 1.0 ? amount : 1.0) : 0.0;
    void *desaturate = Shader(Offsets::IMAGE_MODE_DESATURATE);
    if (desaturate == nullptr || amount == 0.0 || amount == 1.0) {
        g_partial.erase(texture);
        SetShader(texture, amount == 1.0 ? desaturate : Shader(0));
        RegionChanged(texture);
        return;
    }
    SetShader(texture, Shader(0));
    Partial &p = g_partial[texture];
    p.amount = amount;
    Refresh(texture, p);
    RegionChanged(texture);
}

} // namespace Texture::Desaturation
