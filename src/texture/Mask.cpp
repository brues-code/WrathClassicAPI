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

// Texture masks — `Frame:CreateMaskTexture()`, `Texture:AddMaskTexture(mask)`,
// `RemoveMaskTexture([mask])`, `GetNumMaskTextures()`, `GetMaskTexture(index)`
// and `Texture:SetMask(path)`. A mask's alpha clips the texture to the mask's
// shape, sampled across the mask region's own rect (or, for SetMask, the
// texture's rect). The engine only masks the minimap (Minimap:SetMaskTexture).
//
// Mechanism (see the Offsets.h "Texture masking" block for the findings):
// UI textures don't draw themselves — each queues an entry into its frame's
// per-draw-layer CRenderBatch, and the batch renderer (FUN_RENDER_BATCH_DRAW)
// packs every entry into shared buffers and draws them in as few calls as it
// can. We hook the renderer. A batch with no masked entry goes straight
// through. Otherwise it is re-issued in order as sub-batches: each run of
// unmasked entries still draws batched, and each masked entry draws alone
// with its masks bound on texture units 1..N.
//
// Each mask unit samples via TEXGEN — the vertex's object-space position
// mapped through a per-unit texture matrix onto the mask's rect — combined as
// SELECTARG2(current) color / MODULATE alpha, so the mask only multiplies the
// base's alpha. Outside the mask's rect the UV leaves [0,1] and the clamped
// sampler reads the mask's transparent edge. This is the state set the
// engine's own spell-decal draw uses (FUN_007E3E80).
//
// Texgen is fixed-function, and the batch normally draws through the UI
// vertex shader and each texture's UI pixel shader, both of which bypass it.
// So a masked entry draws on the renderer's own fixed-function path: the
// vertex-shader slots are nulled for that one call, and the entry's pixel
// shader is cleared in a copy of the entry.
//
// Mask state is keyed by the CSimpleTexture pointer, which is also what a
// batch entry's positions pointer resolves to. A destructor co-hook drops a
// region's state (as base and as mask) when it dies, so pooled memory can
// never inherit a stale mask. An SEH latch turns the feature off on a draw fault.

#include "Game.h"
#include "Offsets.h"

#include <windows.h>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <iterator>
#include <string>
#include <unordered_map>
#include <vector>

namespace Texture::Mask {

namespace {

template <typename T> T &At(void *base, uintptr_t offset) {
    return *reinterpret_cast<T *>(static_cast<uint8_t *>(base) + offset);
}

bool g_enabled = true;

// SetMask(path): the loaded HTEXTURE (a reference we own) plus the path, so a
// repeat SetMask with the same path is a no-op.
struct PathMask {
    void *handle = nullptr;
    std::string path;
};
std::unordered_map<void *, PathMask> g_pathMasks;

// AddMaskTexture: base region -> mask regions, in Add order.
std::unordered_map<void *, std::vector<void *>> g_baseMasks;

// --- engine entry points -----------------------------------------------------

using BatchDraw_t = void(__cdecl *)(void *batch);
using Walker_t = void(__cdecl *)(void *L);
using RegisterFrameMethods_t = void(__cdecl *)(void *L, const void *table, int count);
// __thiscall(this, uint8_t flags) as a dummy-EDX __fastcall.
using Dtor_t = void *(__fastcall *)(void *self, void *edx, uint8_t flags);

using LayoutIsDirty_t = uint32_t(__fastcall *)(void *layout);
using LayoutResolve_t = void(__fastcall *)(void *layout, void *edx, int now);
using LayoutGetRect_t = int(__fastcall *)(void *layout, void *edx, float *outBLTR);
using HideUpdate_t = void(__fastcall *)(void *region);

using FlagsInit_t = uint32_t *(__fastcall *)(uint32_t *flags, void *edx, uint32_t, uint32_t,
                                             uint32_t, uint32_t, uint32_t, uint32_t, uint32_t,
                                             uint32_t, uint32_t, uint32_t);
using LoadByPath_t = void *(__cdecl *)(const char *path, uint32_t flags, void *desc,
                                       uint32_t mode);
using DescDtor_t = void(__fastcall *)(void *desc);
using Release_t = void(__cdecl *)(void *handle);
using GetRenderable_t = void *(__cdecl *)(void *handle, int force, int *);

using GxRsSetPtr_t = void(__fastcall *)(void *dev, void *edx, int state, void *value);
using GxRsSet_t = void(__cdecl *)(int state, int value);
using GxStateOp_t = void(__fastcall *)(void *dev);
using GxTexMtxPushLoad_t = void(__fastcall *)(void *dev, void *edx, int unit, const float *m16);

BatchDraw_t g_batchDrawOriginal = nullptr;
Walker_t g_textureWalkerOriginal = nullptr;
Walker_t g_frameWalkerOriginal = nullptr;
Dtor_t g_dtorOriginal = nullptr;

// --- textures and rects ------------------------------------------------------

void ReleaseHandle(void *handle) {
    if (handle != nullptr)
        reinterpret_cast<Release_t>(Offsets::FUN_TEXTURE_RELEASE)(handle);
}

// Loads `path` the way Minimap:SetMaskTexture does (clamped, no mips). Returns
// null when the file can't be loaded.
void *LoadByPath(const char *path) {
    struct TexLoadDesc {
        void *vtbl;
        int32_t field4;
        void *self8;
        uintptr_t fieldC;
        int32_t errorLevel;
    } desc;
    desc.vtbl = reinterpret_cast<void *>(Offsets::PTR_TEXLOAD_DESC_VTBL);
    desc.field4 = 8;
    desc.self8 = &desc.self8;
    desc.fieldC = reinterpret_cast<uintptr_t>(&desc.self8) | 1u;
    desc.errorLevel = 0;
    uint32_t flagsOut = 0;
    const uint32_t flags = *reinterpret_cast<FlagsInit_t>(Offsets::FUN_TEXTURE_FLAGS_INIT)(
        &flagsOut, nullptr, 1, 0, 0, 0, 0, 0, 1, 0, 0, 0);
    void *handle = reinterpret_cast<LoadByPath_t>(Offsets::FUN_TEXTURE_LOAD_BY_PATH)(
        path, flags, &desc, 3);
    const bool failed = desc.errorLevel > 1;
    reinterpret_cast<DescDtor_t>(Offsets::FUN_TEXLOAD_DESC_DTOR)(&desc);
    if (failed) {
        ReleaseHandle(handle);
        return nullptr;
    }
    return handle;
}

// A region's resolved rect {bottom, left, top, right}. Mirrors Script GetRect:
// a dirty layout is resolved now (flag 1 — the read-time path, which also
// works for a hidden mask region). Returns false if it still can't resolve.
bool RegionRect(void *region, float outBLTR[4]) {
    void *layout = static_cast<uint8_t *>(region) + Offsets::OFF_REGION_LAYOUT;
    if (reinterpret_cast<LayoutIsDirty_t>(Offsets::FUN_LAYOUT_IS_DIRTY)(layout) != 0)
        reinterpret_cast<LayoutResolve_t>(Offsets::FUN_LAYOUT_RESOLVE)(layout, nullptr, 1);
    return reinterpret_cast<LayoutGetRect_t>(Offsets::FUN_LAYOUT_GET_RECT)(layout, nullptr,
                                                                           outBLTR) != 0;
}

// Row-vector texture matrix taking an object-space vertex position (the corner
// array: layout units, y-up) to the mask's UV: u runs left->right across the
// rect, v top->bottom.
//   u = (x - left) / W,  v = (top - y) / H
bool BuildMaskMatrix(const float r[4], float out[16]) {
    const float w = r[3] - r[1];
    const float h = r[2] - r[0];
    if (w == 0.0f || h == 0.0f)
        return false;
    for (int i = 0; i < 16; ++i)
        out[i] = 0.0f;
    out[0] = 1.0f / w;
    out[5] = -1.0f / h;
    out[10] = 1.0f;
    out[12] = -r[1] / w;
    out[13] = r[2] / h;
    out[15] = 1.0f;
    return true;
}

// --- masked draw ---------------------------------------------------------------

// What a masked draw has changed, so the post-draw restore (or the SEH handler)
// undoes exactly that: bit 0 = Gx state journal pushed, bits 1..7 = that
// unit's texture matrix pushed; plus the vertex-shader slots when nulled.
struct Pending {
    void *dev = nullptr;
    uint32_t pushed = 0;
    bool shadersNulled = false;
    void *shaderA = nullptr;
    void *shaderB = nullptr;
} g_pending;

void RestoreAfterDraw() {
    if (g_pending.shadersNulled) {
        At<void *>(reinterpret_cast<void *>(Offsets::VAR_UI_VERTEX_SHADER_A), 0) = g_pending.shaderA;
        At<void *>(reinterpret_cast<void *>(Offsets::VAR_UI_VERTEX_SHADER_B), 0) = g_pending.shaderB;
        g_pending.shadersNulled = false;
    }
    void *dev = g_pending.dev;
    for (int u = 1; u <= 7; ++u) {
        if ((g_pending.pushed & (1u << u)) == 0)
            continue;
        // The engine's inline texture-matrix pop.
        auto *stack = static_cast<uint8_t *>(dev) + Offsets::OFF_GXDEV_TEXMTX_STACK0 +
                      u * Offsets::GXDEV_TEXMTX_STACK_STRIDE;
        auto &depth = At<uint32_t>(stack, 0);
        if (depth != 0)
            --depth;
        At<uint8_t>(stack, 4) = 1;
    }
    if ((g_pending.pushed & 1u) != 0)
        reinterpret_cast<GxStateOp_t>(Offsets::FUN_GX_STATE_POP)(dev);
    g_pending.pushed = 0;
}

// Draws one masked entry through the batch renderer. All fallible resolution
// happens before any Gx state is touched; returns false (caller draws the
// entry unmasked this frame) when a mask can't be resolved yet.
bool DrawMaskedImpl(void *region, const uint8_t *entry, uint8_t *header) {
    void *dev = At<void *>(reinterpret_cast<void *>(Offsets::VAR_GX_DEVICE), 0);
    if (dev == nullptr)
        return false;
    // Units 1..(stages-1), 7 at most. Masks past that are dropped — path mask
    // first, then Add order.
    int usable = At<int32_t>(dev, Offsets::OFF_GXDEV_STAGE_COUNT) - 1;
    if (usable > 7)
        usable = 7;
    if (usable < 1)
        return false;

    auto getRenderable = reinterpret_cast<GetRenderable_t>(Offsets::FUN_TEXTURE_GET_RENDERABLE);
    void *textures[7];
    float matrices[7][16];
    int units = 0;

    auto pit = g_pathMasks.find(region);
    if (pit != g_pathMasks.end()) {
        float r[4];
        textures[units] = getRenderable(pit->second.handle, 1, nullptr);
        if (textures[units] == nullptr || !RegionRect(region, r) ||
            !BuildMaskMatrix(r, matrices[units]))
            return false;
        ++units;
    }
    auto bit = g_baseMasks.find(region);
    if (bit != g_baseMasks.end()) {
        for (void *mask : bit->second) {
            if (units >= usable)
                break;
            // The mask's own HTEXTURE (set by its SetTexture; present even
            // while hidden). Resolving it each frame is the residency reference.
            void *handle = At<void *>(mask, Offsets::OFF_SIMPLETEXTURE_HTEXTURE);
            if (handle == nullptr)
                return false;
            float r[4];
            textures[units] = getRenderable(handle, 1, nullptr);
            if (textures[units] == nullptr || !RegionRect(mask, r) ||
                !BuildMaskMatrix(r, matrices[units]))
                return false;
            ++units;
        }
    }
    if (units == 0)
        return false;

    // Infallible tail. Record each change as it lands so a fault mid-sequence
    // only undoes what was done.
    auto rsPtr = reinterpret_cast<GxRsSetPtr_t>(Offsets::FUN_GX_RS_SET_PTR);
    auto rs = reinterpret_cast<GxRsSet_t>(Offsets::FUN_GX_RS_SET);
    auto mtxPushLoad = reinterpret_cast<GxTexMtxPushLoad_t>(Offsets::FUN_GX_TEXMTX_PUSH_LOAD);
    g_pending.dev = dev;
    reinterpret_cast<GxStateOp_t>(Offsets::FUN_GX_STATE_PUSH)(dev);
    g_pending.pushed = 1u;
    rsPtr(dev, nullptr, Offsets::GXRS_VERTEX_SHADER, nullptr);
    for (int i = 0; i < units; ++i) {
        const int u = 1 + i;
        rsPtr(dev, nullptr, Offsets::GXRS_TEXTURE0 + u, textures[i]);
        rs(Offsets::GXRS_COLOR_COMBINE0 + u, Offsets::GX_COMBINE_SELECT_CURRENT);
        rs(Offsets::GXRS_ALPHA_COMBINE0 + u, Offsets::GX_COMBINE_MODULATE);
        rs(Offsets::GXRS_TEXGEN0 + u, Offsets::GX_TEXGEN_OBJECT_POS);
        rs(Offsets::GXRS_TEXXFORM0 + u, Offsets::GX_TEXXFORM_TEXGEN_TIMES_MATRIX);
        mtxPushLoad(dev, nullptr, u, matrices[i]);
        g_pending.pushed |= 1u << u;
    }

    // The entry without its pixel shader, drawn on the fixed-function path.
    uint8_t copy[Offsets::RENDER_BATCH_ENTRY_SIZE];
    std::memcpy(copy, entry, sizeof(copy));
    At<void *>(copy, Offsets::OFF_RENDER_ENTRY_PIXEL_SHADER) = nullptr;
    auto &shaderA = At<void *>(reinterpret_cast<void *>(Offsets::VAR_UI_VERTEX_SHADER_A), 0);
    auto &shaderB = At<void *>(reinterpret_cast<void *>(Offsets::VAR_UI_VERTEX_SHADER_B), 0);
    g_pending.shaderA = shaderA;
    g_pending.shaderB = shaderB;
    g_pending.shadersNulled = true;
    shaderA = nullptr;
    shaderB = nullptr;
    At<uint32_t>(header, Offsets::OFF_RENDER_BATCH_COUNT) = 1;
    At<void *>(header, Offsets::OFF_RENDER_BATCH_ENTRIES) = copy;
    g_batchDrawOriginal(header);

    RestoreAfterDraw();
    return true;
}

bool SafeDrawMasked(void *region, const uint8_t *entry, uint8_t *header) {
    __try {
        return DrawMaskedImpl(region, entry, header);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        RestoreAfterDraw();
        g_enabled = false;
        return true; // the entry is dropped this frame; later frames draw unmasked
    }
}

// --- batch renderer hook -------------------------------------------------------

void *RegionOfEntry(const uint8_t *entry) {
    auto *positions = At<uint8_t *>(const_cast<uint8_t *>(entry), Offsets::OFF_RENDER_ENTRY_POSITIONS);
    return positions == nullptr ? nullptr : positions - Offsets::OFF_SIMPLETEXTURE_CORNERS;
}

bool IsMasked(void *region) {
    return region != nullptr &&
           (g_pathMasks.find(region) != g_pathMasks.end() ||
            g_baseMasks.find(region) != g_baseMasks.end());
}

void __cdecl BatchDraw_h(void *batch) {
    if (!g_enabled || (g_pathMasks.empty() && g_baseMasks.empty())) {
        g_batchDrawOriginal(batch);
        return;
    }
    const uint32_t count = At<uint32_t>(batch, Offsets::OFF_RENDER_BATCH_COUNT);
    auto *entries = At<uint8_t *>(batch, Offsets::OFF_RENDER_BATCH_ENTRIES);
    uint32_t first = 0;
    while (first < count &&
           !IsMasked(RegionOfEntry(entries + first * Offsets::RENDER_BATCH_ENTRY_SIZE)))
        ++first;
    if (first == count) {
        g_batchDrawOriginal(batch);
        return;
    }

    // Sub-batches draw from a header copy with empty tail lists, so the
    // font strings and callbacks run exactly once, after every texture.
    uint8_t header[Offsets::RENDER_BATCH_SIZE];
    std::memcpy(header, batch, sizeof(header));
    At<uint32_t>(header, Offsets::OFF_RENDER_BATCH_FONTSTRINGS) = 0;
    At<uint32_t>(header, Offsets::OFF_RENDER_BATCH_CALLBACKS) = 0;
    auto drawRun = [&](uint32_t begin, uint32_t end) {
        if (begin == end)
            return;
        At<uint32_t>(header, Offsets::OFF_RENDER_BATCH_COUNT) = end - begin;
        At<uint8_t *>(header, Offsets::OFF_RENDER_BATCH_ENTRIES) =
            entries + begin * Offsets::RENDER_BATCH_ENTRY_SIZE;
        g_batchDrawOriginal(header);
    };

    uint32_t runStart = 0;
    for (uint32_t i = first; i < count; ++i) {
        const uint8_t *entry = entries + i * Offsets::RENDER_BATCH_ENTRY_SIZE;
        void *region = RegionOfEntry(entry);
        if (!g_enabled || !IsMasked(region))
            continue;
        drawRun(runStart, i);
        if (!SafeDrawMasked(region, entry, header))
            drawRun(i, i + 1);
        runStart = i + 1;
    }
    drawRun(runStart, count);

    // The batch's tail on its own: a zero count skips straight to it.
    At<uint32_t>(batch, Offsets::OFF_RENDER_BATCH_COUNT) = 0;
    g_batchDrawOriginal(batch);
    At<uint32_t>(batch, Offsets::OFF_RENDER_BATCH_COUNT) = count;
}

static const Game::HookAutoRegister _batchHook{
    Offsets::FUN_RENDER_BATCH_DRAW, reinterpret_cast<void *>(&BatchDraw_h),
    reinterpret_cast<void **>(&g_batchDrawOriginal)};

// --- lifecycle -----------------------------------------------------------------

// Drops everything `region` takes part in: its own masks, and its use as a
// mask on any other texture.
void Forget(void *region) {
    auto pit = g_pathMasks.find(region);
    if (pit != g_pathMasks.end()) {
        ReleaseHandle(pit->second.handle);
        g_pathMasks.erase(pit);
    }
    g_baseMasks.erase(region);
    for (auto it = g_baseMasks.begin(); it != g_baseMasks.end();) {
        auto &v = it->second;
        v.erase(std::remove(v.begin(), v.end(), region), v.end());
        if (v.empty())
            it = g_baseMasks.erase(it);
        else
            ++it;
    }
}

void *__fastcall Dtor_h(void *self, void *edx, uint8_t flags) {
    if (self != nullptr && (!g_pathMasks.empty() || !g_baseMasks.empty()))
        Forget(self);
    return g_dtorOriginal(self, edx, flags);
}

static const Game::HookAutoRegister _dtorHook{Offsets::FUN_SIMPLETEXTURE_DTOR,
                                              reinterpret_cast<void *>(&Dtor_h),
                                              reinterpret_cast<void **>(&g_dtorOriginal)};

void PrepareForReload() {
    for (auto &kv : g_pathMasks)
        ReleaseHandle(kv.second.handle);
    g_pathMasks.clear();
    g_baseMasks.clear();
}

static const Game::ReloadAutoRegister _reloadReg{&PrepareForReload};

// --- Lua surface ---------------------------------------------------------------

// The C++ object behind the Lua object at `idx` (its `[0]` lightuserdata), or
// null for anything else. Leaves the stack as it found it.
void *ObjectAt(void *L, int idx) {
    if (Game::Lua::Type(L, idx) != Game::Lua::TYPE_TABLE)
        return nullptr;
    Game::Lua::RawGetI(L, idx, 0);
    void *object = Game::Lua::ToUserdata(L, -1);
    Game::Lua::SetTop(L, -2);
    return object;
}

// The CSimpleTexture at `idx`, or null if it isn't one.
void *TextureAt(void *L, int idx) {
    void *object = ObjectAt(L, idx);
    if (object == nullptr || At<uintptr_t>(object, 0) != Offsets::VTBL_SIMPLETEXTURE)
        return nullptr;
    return object;
}

void PushObject(void *L, void *object) {
    if (At<uint32_t>(object, Offsets::OFF_SCRIPTOBJECT_LUA_REGISTERED) == 0) {
        Game::Lua::PushNil(L);
        return;
    }
    Game::Lua::RawGetI(L, Game::Lua::REGISTRY_INDEX,
                       At<int32_t>(object, Offsets::OFF_SCRIPTOBJECT_LUA_REF));
}

// texture:SetMask(path) sets a mask spanning the texture itself;
// SetMask(nil) / SetMask("") clears it.
int __cdecl Script_SetMask(void *L) {
    void *region = TextureAt(L, 1);
    if (region == nullptr)
        return 0;
    const char *path = Game::Lua::Type(L, 2) == Game::Lua::TYPE_STRING
                           ? Game::Lua::ToString(L, 2)
                           : nullptr;
    auto it = g_pathMasks.find(region);
    if (path == nullptr || path[0] == '\0') {
        if (it != g_pathMasks.end()) {
            ReleaseHandle(it->second.handle);
            g_pathMasks.erase(it);
        }
        return 0;
    }
    if (it != g_pathMasks.end() && it->second.path == path)
        return 0;
    void *handle = LoadByPath(path);
    if (handle == nullptr)
        return 0; // unloadable file: leave the current mask as it is
    if (it != g_pathMasks.end()) {
        ReleaseHandle(it->second.handle);
        it->second.handle = handle;
        it->second.path = path;
    } else {
        g_pathMasks.emplace(region, PathMask{handle, path});
    }
    return 0;
}

// texture:AddMaskTexture(mask) — clip this texture to the mask. Repeat calls
// add further masks; adding the same mask twice is a no-op.
int __cdecl Script_AddMaskTexture(void *L) {
    void *base = TextureAt(L, 1);
    void *mask = TextureAt(L, 2);
    if (base == nullptr || mask == nullptr || base == mask)
        return 0;
    auto &v = g_baseMasks[base];
    if (std::find(v.begin(), v.end(), mask) == v.end())
        v.push_back(mask);
    return 0;
}

// texture:RemoveMaskTexture([mask]) — drop the given mask, or all if omitted.
int __cdecl Script_RemoveMaskTexture(void *L) {
    void *base = TextureAt(L, 1);
    if (base == nullptr)
        return 0;
    auto it = g_baseMasks.find(base);
    if (it == g_baseMasks.end())
        return 0;
    void *mask = TextureAt(L, 2);
    if (mask == nullptr) {
        g_baseMasks.erase(it);
        return 0;
    }
    auto &v = it->second;
    v.erase(std::remove(v.begin(), v.end(), mask), v.end());
    if (v.empty())
        g_baseMasks.erase(it);
    return 0;
}

// texture:GetNumMaskTextures() -> count.
int __cdecl Script_GetNumMaskTextures(void *L) {
    void *base = TextureAt(L, 1);
    size_t n = 0;
    if (base != nullptr) {
        auto it = g_baseMasks.find(base);
        if (it != g_baseMasks.end())
            n = it->second.size();
    }
    Game::Lua::PushNumber(L, static_cast<double>(n));
    return 1;
}

// texture:GetMaskTexture(index) -> the index-th mask (or nil).
int __cdecl Script_GetMaskTexture(void *L) {
    void *base = TextureAt(L, 1);
    const int index = Game::Lua::IsNumber(L, 2) ? static_cast<int>(Game::Lua::ToNumber(L, 2)) : 1;
    if (base != nullptr && index >= 1) {
        auto it = g_baseMasks.find(base);
        if (it != g_baseMasks.end() && static_cast<size_t>(index) <= it->second.size()) {
            PushObject(L, it->second[index - 1]);
            return 1;
        }
    }
    Game::Lua::PushNil(L);
    return 1;
}

// frame:CreateMaskTexture([name, layer, inherits]) — the engine's own
// CreateTexture (it reads the same arguments and pushes the new texture),
// hidden, since a mask is only sampled, never drawn.
int __cdecl Script_CreateMaskTexture(void *L) {
    const int rc = reinterpret_cast<Game::Lua::CFunction>(Offsets::FUN_SCRIPT_CREATE_TEXTURE)(L);
    if (rc != 1)
        return rc;
    void *region = TextureAt(L, Game::Lua::GetTop(L));
    if (region != nullptr) {
        At<uint8_t>(region, Offsets::OFF_REGION_SHOWN_FLAGS) &=
            static_cast<uint8_t>(~Offsets::REGION_SHOWN_BIT);
        reinterpret_cast<HideUpdate_t>(Offsets::FUN_REGION_HIDE_UPDATE)(region);
    }
    return 1;
}

// (name, func) pairs in the engine's 8-byte method-table layout.
struct MethodEntry {
    const char *name;
    Game::Lua::CFunction func;
};

const MethodEntry kTextureMethods[] = {
    {"SetMask", &Script_SetMask},
    {"AddMaskTexture", &Script_AddMaskTexture},
    {"RemoveMaskTexture", &Script_RemoveMaskTexture},
    {"GetNumMaskTextures", &Script_GetNumMaskTextures},
    {"GetMaskTexture", &Script_GetMaskTexture},
};

const MethodEntry kFrameMethods[] = {
    {"CreateMaskTexture", &Script_CreateMaskTexture},
};

void RegisterMethods(void *L, const MethodEntry *table, int count) {
    reinterpret_cast<RegisterFrameMethods_t>(Offsets::FUN_REGISTER_FRAME_METHODS)(L, table, count);
}

void __cdecl TextureWalker_h(void *L) {
    g_textureWalkerOriginal(L);
    RegisterMethods(L, kTextureMethods, static_cast<int>(std::size(kTextureMethods)));
}

void __cdecl FrameWalker_h(void *L) {
    g_frameWalkerOriginal(L);
    RegisterMethods(L, kFrameMethods, static_cast<int>(std::size(kFrameMethods)));
}

static const Game::HookAutoRegister _textureWalkerHook{
    Offsets::FUN_TEXTURE_METHODS_WALKER, reinterpret_cast<void *>(&TextureWalker_h),
    reinterpret_cast<void **>(&g_textureWalkerOriginal)};

static const Game::HookAutoRegister _frameWalkerHook{
    Offsets::FUN_FRAME_METHODS_WALKER, reinterpret_cast<void *>(&FrameWalker_h),
    reinterpret_cast<void **>(&g_frameWalkerOriginal)};

} // namespace

} // namespace Texture::Mask
