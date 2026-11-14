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

// `ModelScene` frame type: retail's 3D stage of model actors with an explicit
// camera, light and fog (FrameAPIModelSceneFrameDocumentation.lua's contract;
// the port of ClassicAPI's src/model/Scene.cpp, whose retail probes set the
// defaults below).
//
// 3.3.5's Model frame (CSimpleModel, Engine/Source/Frame/CSimpleModel.cpp in
// the decompiled client) is already a stage: each frame owns an M2 scene and a
// clock, and its paint (RenderModel 0x0095FC30) animates and draws that WHOLE
// scene, every model in it at its own world transform. The stock frame only
// ever places one model. So a ModelScene is a CSimpleModel whose Model half Lua
// never sees: one SMemAlloc block of the CSimpleModel size plus a pointer to
// our Fields, CSimpleModel's constructor, then our copies of its two vtables:
//
//     slot                          override
//     +0x00 deleting destructor     the actors, then CSimpleModel's body (which
//                                   closes the camera and the scene), then free
//     +0x08 GetScriptMetaTable      ours: CSimpleFrame's methods, then ours
//     +0x0C GetScriptByName         CSimpleFrame's: no OnUpdateModel
//     +0x10 / +0x18 / +0x1C         ModelScene, then Frame's answers: retail's
//                                   IsObjectType("Model") is false
//     +0x58 GetScriptTime           CSimpleFrame's
//     +0x78 OnLayerUpdate           Frame's, the actors linked, the clock, then
//                                   each actor's update
//     +0x84 OnFrameRender           Frame's, then our paint on the draw layer
//     +0xD4 UpdateAlpha             Frame's, then each actor's model
//     layout +0x00 / +0x04          our destructor / the XML loader below
//
// The paint mirrors RenderModel call for call: the device projection, view and
// viewport saved; the camera's projection and view over the frame's rect
// (CameraSetupWorldProjection); the viewport the rect clamped to the screen,
// nothing drawn when it is degenerate; a depth clear; every actor placed (where
// RenderModel calls UpdateModel); the scene animated from the camera's eye and
// drawn in passes 0 and 1; the three states restored.
//
// The camera is a CCamera (CameraCreate) installed with CSimpleModel::SetCamera.
// Retail's camera is a position and three axes; the CCamera's eye is the
// position and its target the position plus forward. The view's up vector is
// built from the camera's yaw and roll properties as (sin yaw sin roll, -cos
// yaw sin roll, cos roll), which can name any unit vector, so both are set to
// name retail's up. Retail's projection convention is this engine's own
// (ClassicAPI's probe matched it to 0.02%), so its field of view and clips go
// in unchanged, and so do its defaults: FOV 0.9424778, near 0.2, far 100, at
// the origin looking along +X.
//
// The light is retail's default, as PlayerModel's constructor (0x00597370)
// sets its own: directional along (0, 1, 0), ambient 178/255, diffuse (204,
// 204, 163)/255, visible. Each actor's model is lit by it through the frame's
// own lighting callback, as a Model frame's model is.
//
// Fog is the Model frame's own (+0x330..+0x33C): the colour arms it, ClearFog
// disarms it only. It starts with retail's values (near 1e7, far 1e8, white),
// which fog nothing. SetPaused stops the scene clock: every actor holds its
// pose.
//
// View insets and translation (retail screenshots, ClassicAPI): the camera's
// picture is built over the frame's rect less the insets, moved by the
// translation (frame units, y up), but drawing still stops at the frame's
// edges and Project3DPointTo2D ignores both. So the projection is built over
// that view rect, then scaled and shifted to place the view in the frame.
//
// XML: `drawLayer`, `fogNear` and `fogFar` (clamped at 0, as Model's loader
// clamps them), <FogColor> (which arms the fog) and <ViewInsets left right top
// bottom>, after the frame's own loader. SetAllowOverlappedModels only stores
// its flag (retail's default false): a model frame draws whatever overlaps it.

#include "model/Scene.h"

#include "Game.h"
#include "Offsets.h"
#include "ui/FrameObject.h"
#include "xml/Node.h"

#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <vector>

namespace Model::Scene {

namespace {

template <typename T> T &At(void *base, uintptr_t offset) {
    return *reinterpret_cast<T *>(static_cast<uint8_t *>(base) + offset);
}
template <typename T> T Global(uintptr_t address) { return *reinterpret_cast<T *>(address); }

// Retail's camera defaults (ClassicAPI's retail probe).
constexpr float kDefaultFieldOfView = 0.9424778f;
constexpr float kDefaultNearClip = 0.2f;
constexpr float kDefaultFarClip = 100.0f;

struct Fields {
    std::vector<Actor *> actors; // owned, in creation order
    Vec3 position{0.0f, 0.0f, 0.0f};
    Vec3 forward{1.0f, 0.0f, 0.0f}, right{0.0f, 1.0f, 0.0f}, up{0.0f, 0.0f, 1.0f};
    float fieldOfView = kDefaultFieldOfView;
    float nearClip = kDefaultNearClip;
    float farClip = kDefaultFarClip;
    uint32_t drawLayer = Offsets::DRAWLAYER_ARTWORK;
    // What the light setters were given, for the getters.
    uint32_t lightType = 0;
    bool lightVisible = true;
    Vec3 lightDirection{0.0f, 1.0f, 0.0f};
    Vec3 lightPosition{0.0f, 0.0f, 0.0f};
    bool paused = false;
    bool allowOverlappedModels = false;
    // In the frame's own units: left, right, top, bottom; then x, y (y up).
    float viewInsets[4] = {};
    float viewTranslation[2] = {};
};

constexpr size_t kObjectSize = Offsets::SIMPLEMODEL_SIZE + sizeof(Fields *);

Fields *&FieldsOf(void *frame) { return At<Fields *>(frame, Offsets::SIMPLEMODEL_SIZE); }
Fields &F(void *frame) { return *FieldsOf(frame); }
void *LayoutOf(void *frame) { return static_cast<uint8_t *>(frame) + Offsets::OFF_FRAME_LAYOUT; }
void *FrameOfLayout(void *layout) { return static_cast<uint8_t *>(layout) - Offsets::OFF_FRAME_LAYOUT; }

// ---- the class ----------------------------------------------------------------------

int g_typeId = 0;
int g_metaTable = 0;

int TypeId() {
    if (g_typeId == 0)
        g_typeId = ++At<int>(reinterpret_cast<void *>(Offsets::VAR_SCRIPTOBJECT_TYPE_COUNTER), 0);
    return g_typeId;
}

constexpr char kTypeName[] = "ModelScene";

// An MSVC vtable with its RTTI locator in front, as the engine's are laid out.
template <size_t N> struct VTable {
    const void *locator;
    const void *slots[N];
    bool Built() const { return slots[0] != nullptr; }
    void CopyFrom(uintptr_t address) { std::memcpy(this, reinterpret_cast<const void *>(address - 4), sizeof(*this)); }
    void Set(uintptr_t slot, const void *fn) { slots[slot / 4] = fn; }
};

VTable<Offsets::VTBL_SIMPLEMODEL_SLOTS> g_vtable;
VTable<Offsets::VTBL_SIMPLEMODEL_LAYOUT_SLOTS> g_layoutVtable;

// ---- engine calls -------------------------------------------------------------------

// __thiscall entry points mirrored as __fastcall with a dummy EDX.
using GetRect_t = int(__fastcall *)(void *layout, void *, float *rect);
using GetScene_t = void *(__fastcall *)(void *frame, void *);
using SetCamera_t = void(__fastcall *)(void *frame, void *, void *camera);
using LightSetVec_t = void(__fastcall *)(void *light, void *, const float *v);
using LightSetInt_t = void(__fastcall *)(void *light, void *, int value);
using SetMatrix_t = void(__fastcall *)(void *device, void *, const float *m);

void *Scene(void *frame) { return reinterpret_cast<GetScene_t>(Offsets::FUN_SIMPLEMODEL_GET_SCENE)(frame, nullptr); }
void *CameraOf(void *frame) { return At<void *>(frame, Offsets::OFF_SIMPLEMODEL_CAMERA); }

void CameraSetCoord(void *camera, uint32_t prop, const Vec3 &v) {
    reinterpret_cast<void(__cdecl *)(void *, uint32_t, const Vec3 &, uint32_t)>(Offsets::FUN_CAMERA_SET_COORD)(
        camera, prop, v, 0);
}
void CameraSetFloat(void *camera, uint32_t prop, float value) {
    reinterpret_cast<void(__cdecl *)(void *, uint32_t, float)>(Offsets::FUN_CAMERA_SET_FLOAT)(camera, prop, value);
}
Vec3 CameraGetCoord(void *camera, uint32_t prop) {
    Vec3 v{0.0f, 0.0f, 0.0f};
    reinterpret_cast<void(__cdecl *)(void *, uint32_t, Vec3 *)>(Offsets::FUN_CAMERA_GET_COORD)(camera, prop, &v);
    return v;
}
void CameraSetupWorldProjection(void *camera, const float *rect) {
    reinterpret_cast<void(__cdecl *)(void *, const float *, uint32_t)>(Offsets::FUN_CAMERA_SETUP_WORLD_PROJECTION)(
        camera, rect, 0);
}

void *Device() { return Global<void *>(Offsets::VAR_GX_DEVICE); }
const float *DeviceProjection() {
    return reinterpret_cast<const float *>(static_cast<uint8_t *>(Device()) + Offsets::OFF_GX_DEVICE_PROJECTION);
}
const float *DeviceView() {
    void *device = Device();
    const uint32_t level = At<uint32_t>(device, Offsets::OFF_GX_DEVICE_VIEW_LEVEL);
    return reinterpret_cast<const float *>(static_cast<uint8_t *>(device) + Offsets::OFF_GX_DEVICE_VIEW_STACK +
                                           level * 64);
}
void SetDeviceMatrix(uintptr_t slot, const float *m) {
    void *device = Device();
    reinterpret_cast<SetMatrix_t>(At<const void *const *>(device, 0)[slot / 4])(device, nullptr, m);
}
void UpdateProjMatrix() { reinterpret_cast<void(__cdecl *)()>(Offsets::FUN_SHADER_EFFECT_UPDATE_PROJ_MATRIX)(); }

// The device projection and view, saved and put back (RenderModel's prologue
// and epilogue).
struct SavedTransforms {
    float projection[16];
    float view[16];
    SavedTransforms() {
        std::memcpy(projection, DeviceProjection(), sizeof projection);
        std::memcpy(view, DeviceView(), sizeof view);
    }
    ~SavedTransforms() {
        SetDeviceMatrix(Offsets::VTBL_GX_DEVICE_SET_PROJECTION, projection);
        SetDeviceMatrix(Offsets::VTBL_GX_DEVICE_SET_VIEW, view);
    }
    SavedTransforms(const SavedTransforms &) = delete;
    SavedTransforms &operator=(const SavedTransforms &) = delete;
};

// {minY, minX, maxY, maxX} in DDC; false while the frame has no rect.
bool GetRect(void *frame, float (&rect)[4]) {
    rect[0] = rect[1] = rect[2] = rect[3] = 0.0f;
    return reinterpret_cast<GetRect_t>(Offsets::FUN_LAYOUT_GET_RECT)(LayoutOf(frame), nullptr, rect) != 0;
}

// DDC per frame unit at the frame's scale (Script_GetLeft's conversion,
// inverted), and DDC to frame units at an effective scale of 1.
float DdcPerUnit(void *frame) {
    return Global<float>(Offsets::VAR_DDC_WIDTH_SCALE) * At<float>(frame, Offsets::OFF_FRAME_LAYOUT_SCALE) /
           (Global<float>(Offsets::VAR_ASPECT_COMPENSATION) * 1024.0f);
}
double DdcToScreenUnits(float ddc) {
    return static_cast<double>(ddc) * Global<float>(Offsets::VAR_ASPECT_COMPENSATION) * 1024.0 /
           Global<float>(Offsets::VAR_DDC_WIDTH_SCALE);
}

// ---- the camera ---------------------------------------------------------------------

// Writes the scene's camera into its CCamera.
void ApplyCamera(void *frame) {
    void *camera = CameraOf(frame);
    if (camera == nullptr)
        return;
    const Fields &f = F(frame);
    CameraSetCoord(camera, Offsets::CAMERA_PROP_EYE, f.position);
    CameraSetCoord(camera, Offsets::CAMERA_PROP_TARGET,
                   {f.position.x + f.forward.x, f.position.y + f.forward.y, f.position.z + f.forward.z});
    // up = (sin yaw sin roll, -cos yaw sin roll, cos roll), roll in [0, pi].
    const float upLength = std::sqrt(f.up.x * f.up.x + f.up.y * f.up.y + f.up.z * f.up.z);
    const float cosRoll = upLength > 0.0f ? f.up.z / upLength : 1.0f;
    const float roll = std::acos(cosRoll < -1.0f ? -1.0f : (cosRoll > 1.0f ? 1.0f : cosRoll));
    const float yaw = (f.up.x != 0.0f || f.up.y != 0.0f) ? std::atan2(f.up.x, -f.up.y) : 0.0f;
    CameraSetFloat(camera, Offsets::CAMERA_PROP_ROLL, roll);
    CameraSetFloat(camera, Offsets::CAMERA_PROP_YAW, yaw);
    CameraSetFloat(camera, Offsets::CAMERA_PROP_FOV, f.fieldOfView);
    CameraSetFloat(camera, Offsets::CAMERA_PROP_NEAR_CLIP, f.nearClip);
    CameraSetFloat(camera, Offsets::CAMERA_PROP_FAR_CLIP, f.farClip);
}

// Retail's axes from yaw, pitch and roll (Vector3D_CalculateNormalFromYawPitch:
// forward = (cos p cos y, cos p sin y, -sin p), positive pitch looking down;
// right = (0, 1, 0) and up = (0, 0, 1) at rest, and roll turns both about
// forward).
void SetOrientation(Fields &f, float yaw, float pitch, float roll) {
    const float cy = std::cos(yaw), sy = std::sin(yaw);
    const float cp = std::cos(pitch), sp = std::sin(pitch);
    const float cr = std::cos(roll), sr = std::sin(roll);
    const Vec3 right0{-sy, cy, 0.0f};
    const Vec3 up0{sp * cy, sp * sy, cp};
    f.forward = {cp * cy, cp * sy, -sp};
    f.right = {cr * right0.x + sr * up0.x, cr * right0.y + sr * up0.y, cr * right0.z + sr * up0.z};
    f.up = {cr * up0.x - sr * right0.x, cr * up0.y - sr * right0.y, cr * up0.z - sr * right0.z};
}

void InstallCamera(void *frame) {
    void *camera = reinterpret_cast<void *(__cdecl *)()>(Offsets::FUN_CAMERA_CREATE)();
    if (camera == nullptr)
        return;
    reinterpret_cast<SetCamera_t>(Offsets::FUN_SIMPLEMODEL_SET_CAMERA)(frame, nullptr, camera);
    reinterpret_cast<void(__cdecl *)(void *)>(Offsets::FUN_HANDLE_CLOSE)(camera);
    ApplyCamera(frame);
}

// ---- the light and fog ----------------------------------------------------------------

void *LightOf(void *frame) { return static_cast<uint8_t *>(frame) + Offsets::OFF_SIMPLEMODEL_LIGHT; }
float *AmbientOf(void *frame) { return &At<float>(LightOf(frame), Offsets::OFF_M2LIGHT_AMBIENT); }
float *DiffuseOf(void *frame) { return &At<float>(LightOf(frame), Offsets::OFF_M2LIGHT_DIFFUSE); }

void SetLightType(void *frame, uint32_t type) {
    F(frame).lightType = type;
    reinterpret_cast<LightSetInt_t>(Offsets::FUN_M2LIGHT_SET_LIGHT_TYPE)(LightOf(frame), nullptr,
                                                                          static_cast<int>(type));
}
void SetLightVisible(void *frame, bool visible) {
    F(frame).lightVisible = visible;
    reinterpret_cast<LightSetInt_t>(Offsets::FUN_M2LIGHT_SET_VISIBLE)(LightOf(frame), nullptr, visible ? 1 : 0);
}
void SetLightDirection(void *frame, const Vec3 &v) {
    F(frame).lightDirection = v;
    reinterpret_cast<LightSetVec_t>(Offsets::FUN_M2LIGHT_SET_DIRECTION)(LightOf(frame), nullptr, &v.x);
}
void SetLightPosition(void *frame, const Vec3 &v) {
    F(frame).lightPosition = v;
    reinterpret_cast<LightSetVec_t>(Offsets::FUN_M2LIGHT_SET_POSITION)(LightOf(frame), nullptr, &v.x);
}
void SetColor(float *color, const Vec3 &v) {
    color[0] = v.x;
    color[1] = v.y;
    color[2] = v.z;
}

void InitLight(void *frame) {
    SetLightType(frame, 0);
    SetLightDirection(frame, {0.0f, 1.0f, 0.0f});
    SetColor(AmbientOf(frame), {178.0f / 255.0f, 178.0f / 255.0f, 178.0f / 255.0f});
    SetColor(DiffuseOf(frame), {204.0f / 255.0f, 204.0f / 255.0f, 163.0f / 255.0f});
    SetLightVisible(frame, true);
}

uint32_t &FogFlags(void *frame) { return At<uint32_t>(frame, Offsets::OFF_SIMPLEMODEL_FLAGS); }

// A colour channel's byte: clamped to [0, 1] (NaN to 1), rounded.
uint32_t ColorByte(float c) {
    if (!(c < 1.0f))
        c = 1.0f;
    else if (!(c > 0.0f))
        c = 0.0f;
    return static_cast<uint32_t>(c * 255.0f + 0.5f);
}

void SetFogColor(void *frame, const Vec3 &c) {
    FogFlags(frame) |= Offsets::SIMPLEMODEL_FOGGED;
    // CImVector: b, g, r, a in memory, so 0xAARRGGBB as a dword.
    At<uint32_t>(frame, Offsets::OFF_SIMPLEMODEL_FOG_COLOR) =
        0xFF000000u | (ColorByte(c.x) << 16) | (ColorByte(c.y) << 8) | ColorByte(c.z);
}

void InitFog(void *frame) {
    FogFlags(frame) &= ~static_cast<uint32_t>(Offsets::SIMPLEMODEL_FOGGED);
    At<uint32_t>(frame, Offsets::OFF_SIMPLEMODEL_FOG_COLOR) = 0xFFFFFFFFu;
    At<float>(frame, Offsets::OFF_SIMPLEMODEL_FOG_NEAR) = 1.0e7f;
    At<float>(frame, Offsets::OFF_SIMPLEMODEL_FOG_FAR) = 1.0e8f;
}

// ---- the paint (CSimpleModel::RenderModel) ---------------------------------------------

float ClampUnit(float v) {
    if (!(v < 1.0f))
        return 1.0f;
    return v > 0.0f ? v : 0.0f;
}

// The view: the frame's rect {minY, minX, maxY, maxX} less the insets and
// moved by the translation, in DDC. Returns false when it is the frame's rect.
bool ViewRect(void *frame, const float (&rect)[4], float (&view)[4]) {
    const Fields &f = F(frame);
    const float *in = f.viewInsets;
    const float *move = f.viewTranslation;
    if (in[0] == 0.0f && in[1] == 0.0f && in[2] == 0.0f && in[3] == 0.0f && move[0] == 0.0f && move[1] == 0.0f)
        return false;
    const float k = DdcPerUnit(frame);
    view[0] = rect[0] + k * (in[3] + move[1]);
    view[1] = rect[1] + k * (in[0] + move[0]);
    view[2] = rect[2] - k * in[2] + k * move[1];
    view[3] = rect[3] - k * in[1] + k * move[0];
    return true;
}

// Puts the camera's picture of `view` where `view` lies in `rect`: the
// projection built over `view` (its aspect and centre), scaled and shifted in
// clip space, because the viewport stays the frame's rect. Row-vector
// matrices: column 0 gives x', column 1 y', column 3 w'.
void FitProjection(const float (&rect)[4], const float (&view)[4]) {
    const float frameW = rect[3] - rect[1], frameH = rect[2] - rect[0];
    if (!(frameW > 0.0f) || !(frameH > 0.0f))
        return;
    const float sx = (view[3] - view[1]) / frameW;
    const float sy = (view[2] - view[0]) / frameH;
    const float tx = ((view[1] + view[3]) - (rect[1] + rect[3])) / frameW;
    const float ty = ((view[0] + view[2]) - (rect[0] + rect[2])) / frameH;
    float m[16];
    std::memcpy(m, DeviceProjection(), sizeof m);
    for (int row = 0; row < 4; ++row) {
        float *r = &m[row * 4];
        r[0] = sx * r[0] + tx * r[3];
        r[1] = sy * r[1] + ty * r[3];
    }
    SetDeviceMatrix(Offsets::VTBL_GX_DEVICE_SET_PROJECTION, m);
    UpdateProjMatrix();
}

void CameraTransforms(void *frame, void *camera, const float (&rect)[4]) {
    float view[4];
    if (ViewRect(frame, rect, view)) {
        CameraSetupWorldProjection(camera, view);
        FitProjection(rect, view);
    } else {
        CameraSetupWorldProjection(camera, rect);
    }
}

void __cdecl Paint(void *frame) {
    using Viewport_t = void(__cdecl *)(float *, float *, float *, float *, float *, float *);
    using SetViewport_t = void(__cdecl *)(float, float, float, float, float, float);
    using DdcToNdc_t = void(__cdecl *)(float, float, float *, float *);
    using Clear_t = void(__cdecl *)(uint32_t mask, uint32_t color);

    void *camera = CameraOf(frame);
    float rect[4];
    if (FieldsOf(frame) == nullptr || camera == nullptr || !GetRect(frame, rect))
        return;
    float minX, maxX, minY, maxY, minZ, maxZ;
    reinterpret_cast<Viewport_t>(Offsets::FUN_GX_XFORM_VIEWPORT)(&minX, &maxX, &minY, &maxY, &minZ, &maxZ);
    {
        const SavedTransforms saved;
        const Vec3 eye = CameraGetCoord(camera, Offsets::CAMERA_PROP_EYE);
        CameraTransforms(frame, camera, rect);
        auto ddcToNdc = reinterpret_cast<DdcToNdc_t>(Offsets::FUN_DDC_TO_NDC);
        ddcToNdc(rect[1], rect[0], &rect[1], &rect[0]);
        ddcToNdc(rect[3], rect[2], &rect[3], &rect[2]);
        for (float &edge : rect)
            edge = ClampUnit(edge);
        const float epsilon = Global<float>(Offsets::VAR_RENDER_MODEL_MIN_EXTENT);
        if (!(std::fabs(rect[1] - rect[3]) < epsilon) && !(std::fabs(rect[0] - rect[2]) < epsilon)) {
            reinterpret_cast<SetViewport_t>(Offsets::FUN_GX_XFORM_SET_VIEWPORT)(
                rect[1] * maxX, rect[3] * maxX, rect[0] * maxY, rect[2] * maxY, 0.0f, 1.0f);
            reinterpret_cast<Clear_t>(Offsets::FUN_GX_SCENE_CLEAR)(2, 0xFF000000u);
            UpdateProjMatrix();
            for (Actor *actor : F(frame).actors)
                actor->Place();
            void *scene = Scene(frame);
            reinterpret_cast<void(__fastcall *)(void *, void *, const Vec3 *)>(Offsets::FUN_M2SCENE_ANIMATE)(
                scene, nullptr, &eye);
            reinterpret_cast<void(__fastcall *)(void *, void *, uint32_t)>(Offsets::FUN_M2SCENE_DRAW)(scene, nullptr,
                                                                                                    0);
            reinterpret_cast<void(__fastcall *)(void *, void *, uint32_t)>(Offsets::FUN_M2SCENE_DRAW)(scene, nullptr,
                                                                                                    1);
        }
    }
    reinterpret_cast<SetViewport_t>(Offsets::FUN_GX_XFORM_SET_VIEWPORT)(minX, maxX, minY, maxY, minZ, maxZ);
    UpdateProjMatrix();
}

// ---- projection (Project3DPointTo2D) ----------------------------------------------------

// A scene point through the matrices the paint draws with: the camera framed
// over the frame's rect (not the view rect: retail's projection ignores the
// insets and translation), read back, the device's put back. The view looks
// from the origin, so the point goes in relative to the eye; the matrices are
// row-vector, w' is the view depth. A |w'| under 0.001 projects nothing.
//
// Retail returns (x, y, depth): x and y in frame units times the effective
// scale, from the bottom-left. Depth here is 1 at the near clip and 0 at the
// far (retail's own carries a factor the engine has no source for; its callers
// all discard it).
bool Project(void *frame, const Vec3 &point, double (&out)[3]) {
    void *camera = CameraOf(frame);
    float rect[4];
    if (camera == nullptr || !GetRect(frame, rect))
        return false;
    float projection[16], view[16];
    {
        const SavedTransforms saved;
        CameraSetupWorldProjection(camera, rect);
        std::memcpy(projection, DeviceProjection(), sizeof projection);
        std::memcpy(view, DeviceView(), sizeof view);
    }
    UpdateProjMatrix();
    const Vec3 eye = CameraGetCoord(camera, Offsets::CAMERA_PROP_EYE);
    const double p[4] = {point.x - eye.x, point.y - eye.y, point.z - eye.z, 1.0};
    double v[4], c[4];
    for (int col = 0; col < 4; ++col) {
        v[col] = 0.0;
        for (int row = 0; row < 4; ++row)
            v[col] += p[row] * view[row * 4 + col];
    }
    for (int col = 0; col < 4; ++col) {
        c[col] = 0.0;
        for (int row = 0; row < 4; ++row)
            c[col] += v[row] * projection[row * 4 + col];
    }
    if (!(std::fabs(c[3]) >= 0.001))
        return false;
    out[0] = (c[0] / c[3] + 1.0) * 0.5 * DdcToScreenUnits(rect[3] - rect[1]);
    out[1] = (c[1] / c[3] + 1.0) * 0.5 * DdcToScreenUnits(rect[2] - rect[0]);
    const Fields &f = F(frame);
    out[2] = (f.farClip - c[3]) / (f.farClip - f.nearClip);
    return true;
}

// ---- the overrides ------------------------------------------------------------------

void FreeFields(void *frame) {
    Fields *f = FieldsOf(frame);
    if (f == nullptr)
        return;
    // The actors' models live in the frame's scene, which the base destructor
    // releases: they go first.
    for (Actor *actor : f->actors)
        delete actor;
    delete f;
    FieldsOf(frame) = nullptr;
}

void *__fastcall DeletingDestructor(void *self, void *, uint32_t flags) {
    FreeFields(self);
    reinterpret_cast<void(__fastcall *)(void *, void *)>(Offsets::FUN_SIMPLEMODEL_DESTRUCTOR)(self, nullptr);
    if ((flags & 1) != 0)
        reinterpret_cast<int(__stdcall *)(void *, const char *, int, int)>(Offsets::FUN_STORM_SMEM_FREE)(
            self, __FILE__, __LINE__, 0);
    return self;
}

void *__fastcall LayoutDeletingDestructor(void *layout, void *, uint32_t flags) {
    return DeletingDestructor(FrameOfLayout(layout), nullptr, flags);
}

int __fastcall GetScriptMetaTable(void *, void *) { return g_metaTable; }

bool __fastcall IsA(void *self, void *, int type) {
    return type == TypeId() ||
           reinterpret_cast<bool(__fastcall *)(void *, void *, int)>(Offsets::FUN_SIMPLEFRAME_IS_A)(self, nullptr, type);
}

bool __fastcall IsAName(void *self, void *, const char *name) {
    return (name != nullptr && _stricmp(name, kTypeName) == 0) ||
           reinterpret_cast<bool(__fastcall *)(void *, void *, const char *)>(Offsets::FUN_SIMPLEFRAME_IS_A_NAME)(
               self, nullptr, name);
}

const char *__fastcall GetObjectTypeName(void *, void *) { return kTypeName; }

// The frame's own clock and scripts (Frame's update), then what
// CSimpleModel::OnLayerUpdate does for its model, for each actor: linked
// while the frame is visible, the scene advanced unless paused.
void __fastcall OnLayerUpdate(void *self, void *, float elapsedSec) {
    reinterpret_cast<void(__fastcall *)(void *, void *, float)>(Offsets::FUN_SIMPLEFRAME_ON_LAYER_UPDATE)(
        self, nullptr, elapsedSec);
    if (FieldsOf(self) == nullptr)
        return;
    if (At<uint32_t>(self, Offsets::OFF_FRAME_IS_VISIBLE) != 0)
        for (Actor *actor : F(self).actors)
            actor->Link();
    void *scene = Scene(self);
    if (!F(self).paused)
        reinterpret_cast<void(__fastcall *)(void *, void *, uint32_t)>(Offsets::FUN_M2SCENE_ADVANCE_TIME)(
            scene, nullptr, static_cast<uint32_t>(std::lround(elapsedSec * 1000.0f)));
    // By index: an actor's script may create another.
    std::vector<Actor *> &actors = F(self).actors;
    for (size_t i = 0; i < actors.size(); ++i)
        actors[i]->Update(elapsedSec);
}

void __fastcall OnFrameRender(void *self, void *, void *batch, uint32_t layer) {
    reinterpret_cast<void(__fastcall *)(void *, void *, void *, uint32_t)>(Offsets::FUN_SIMPLEFRAME_ON_FRAME_RENDER)(
        self, nullptr, batch, layer);
    if (FieldsOf(self) != nullptr && layer == F(self).drawLayer)
        reinterpret_cast<void(__fastcall *)(void *, void *, void(__cdecl *)(void *), void *)>(
            Offsets::FUN_RENDERBATCH_QUEUE_CALLBACK)(batch, nullptr, &Paint, self);
}

void __fastcall UpdateAlpha(void *self, void *) {
    reinterpret_cast<void(__fastcall *)(void *, void *)>(Offsets::FUN_SIMPLEFRAME_UPDATE_ALPHA)(self, nullptr);
    if (FieldsOf(self) != nullptr)
        for (Actor *actor : F(self).actors)
            actor->ApplyAlpha();
}

// ---- XML ----------------------------------------------------------------------------

using Xml::Attr;
using Xml::FirstChild;
using Xml::NextSibling;
using Xml::TagIs;

bool FloatAttr(const void *node, const char *name, float &out) {
    const char *value = Attr(node, name);
    if (value == nullptr || *value == '\0')
        return false;
    out = static_cast<float>(std::atof(value));
    return true;
}

bool DrawLayerFromName(const char *name, uint32_t &layer) {
    return reinterpret_cast<int(__cdecl *)(const char *, uint32_t &)>(Offsets::FUN_STRING_TO_DRAW_LAYER)(name, layer) !=
           0;
}

// The frame's own loader, then ModelSceneType's attributes and children.
void __fastcall LoadXML(void *layout, void *, const void *node, void *status) {
    reinterpret_cast<void(__fastcall *)(void *, void *, const void *, void *)>(Offsets::FUN_SIMPLEFRAME_LOAD_XML)(
        layout, nullptr, node, status);
    void *frame = FrameOfLayout(layout);
    if (node == nullptr || FieldsOf(frame) == nullptr)
        return;
    if (const char *layer = Attr(node, "drawLayer"); layer != nullptr && *layer != '\0')
        DrawLayerFromName(layer, F(frame).drawLayer);
    float value = 0.0f;
    if (FloatAttr(node, "fogNear", value))
        At<float>(frame, Offsets::OFF_SIMPLEMODEL_FOG_NEAR) = value < 0.0f ? 0.0f : value;
    if (FloatAttr(node, "fogFar", value))
        At<float>(frame, Offsets::OFF_SIMPLEMODEL_FOG_FAR) = value < 0.0f ? 0.0f : value;
    for (const void *c = FirstChild(node); c != nullptr; c = NextSibling(c)) {
        if (TagIs(c, "FogColor")) {
            reinterpret_cast<int(__cdecl *)(const void *, uint32_t &, void *)>(Offsets::FUN_XML_LOAD_COLOR)(
                c, At<uint32_t>(frame, Offsets::OFF_SIMPLEMODEL_FOG_COLOR), status);
            FogFlags(frame) |= Offsets::SIMPLEMODEL_FOGGED;
        } else if (TagIs(c, "ViewInsets")) {
            float *in = F(frame).viewInsets;
            FloatAttr(c, "left", in[0]);
            FloatAttr(c, "right", in[1]);
            FloatAttr(c, "top", in[2]);
            FloatAttr(c, "bottom", in[3]);
        }
    }
}

void BuildVTables() {
    if (g_vtable.Built())
        return;
    auto fn = [](auto f) { return reinterpret_cast<const void *>(f); };
    g_vtable.CopyFrom(Offsets::VTBL_SIMPLEMODEL);
    g_layoutVtable.CopyFrom(Offsets::VTBL_SIMPLEMODEL_LAYOUT);
    g_vtable.Set(Offsets::VTBL_FRAME_DELETING_DESTRUCTOR, fn(&DeletingDestructor));
    g_vtable.Set(Offsets::VTBL_FRAME_GET_SCRIPT_META_TABLE, fn(&GetScriptMetaTable));
    g_vtable.Set(Offsets::VTBL_FRAME_GET_SCRIPT_BY_NAME,
                 reinterpret_cast<const void *>(Offsets::FUN_SIMPLEFRAME_GET_SCRIPT_BY_NAME));
    g_vtable.Set(Offsets::VTBL_FRAME_IS_A, fn(&IsA));
    g_vtable.Set(Offsets::VTBL_FRAME_IS_A_NAME, fn(&IsAName));
    g_vtable.Set(Offsets::VTBL_FRAME_GET_OBJECT_TYPE_NAME, fn(&GetObjectTypeName));
    g_vtable.Set(Offsets::VTBL_FRAME_GET_SCRIPT_TIME,
                 reinterpret_cast<const void *>(Offsets::FUN_SIMPLEFRAME_GET_SCRIPT_TIME));
    g_vtable.Set(Offsets::VTBL_FRAME_ON_LAYER_UPDATE, fn(&OnLayerUpdate));
    g_vtable.Set(Offsets::VTBL_FRAME_ON_FRAME_RENDER, fn(&OnFrameRender));
    g_vtable.Set(Offsets::VTBL_FRAME_UPDATE_ALPHA, fn(&UpdateAlpha));
    g_layoutVtable.Set(Offsets::VTBL_LAYOUT_DELETING_DESTRUCTOR, fn(&LayoutDeletingDestructor));
    g_layoutVtable.Set(Offsets::VTBL_LAYOUT_LOAD_XML, fn(&LoadXML));
}

// The "ModelScene" factory, shaped like the engine's "Model" one
// (Create_SimpleModel 0x008126C0).
void *__cdecl CreateModelScene(void *parent) {
    BuildVTables();
    void *frame = reinterpret_cast<void *(__stdcall *)(uint32_t, const char *, int, int)>(
        Offsets::FUN_STORM_SMEM_ALLOC)(static_cast<uint32_t>(kObjectSize), __FILE__, __LINE__, 0);
    if (frame == nullptr)
        return nullptr;
    std::memset(frame, 0, kObjectSize);
    reinterpret_cast<void(__fastcall *)(void *, void *, void *)>(Offsets::FUN_SIMPLEMODEL_CONSTRUCTOR)(frame, nullptr,
                                                                                                    parent);
    At<const void *>(frame, 0) = g_vtable.slots;
    At<const void *>(frame, Offsets::OFF_FRAME_LAYOUT) = g_layoutVtable.slots;
    FieldsOf(frame) = new Fields;
    InitLight(frame);
    InitFog(frame);
    InstallCamera(frame);
    return frame;
}

// ---- the methods ------------------------------------------------------------------

void *Self(void *L) { return UI::FrameObject::ResolveSelf(L, TypeId()); }

const char *FrameName(void *frame) {
    const char *name =
        reinterpret_cast<const char *(__fastcall *)(void *, void *)>(At<const void *const *>(frame, 0)[1])(frame,
                                                                                                         nullptr);
    return name != nullptr && *name != '\0' ? name : "<unnamed>";
}

int PushVec3(void *L, const Vec3 &v) {
    Game::Lua::PushNumber(L, v.x);
    Game::Lua::PushNumber(L, v.y);
    Game::Lua::PushNumber(L, v.z);
    return 3;
}

Vec3 Vec3Args(void *L, int first) { return {FloatArg(L, first), FloatArg(L, first + 1), FloatArg(L, first + 2)}; }

int __cdecl Script_CreateActor(void *L) {
    using Acquire_t = const void *(__cdecl *)(const char *, const char *&, bool &);
    void *frame = Self(L);
    if (frame == nullptr)
        return 0;
    const char *name = Game::Lua::IsString(L, 2) ? Game::Lua::ToString(L, 2) : nullptr;
    const char *inherits = Game::Lua::IsString(L, 3) ? Game::Lua::ToString(L, 3) : nullptr;
    // The template, locked while it loads (FrameXML_AcquireHashNode); a node
    // locked already is one being loaded, so it is not found.
    const void *templateNode = nullptr;
    if (inherits != nullptr && *inherits != '\0') {
        const char *taint = nullptr;
        bool wasLocked = false;
        templateNode = reinterpret_cast<Acquire_t>(Offsets::FUN_FRAMEXML_ACQUIRE_HASH_NODE)(inherits, taint, wasLocked);
        if (templateNode == nullptr || wasLocked) {
            Game::Lua::Error(L, "%s:CreateActor(): Couldn't find inherited node \"%s\"", FrameName(frame),
                             inherits);
            return 0;
        }
    }
    auto *actor = new Actor(frame, name);
    F(frame).actors.push_back(actor);
    if (templateNode != nullptr) {
        actor->LoadXml(templateNode);
        reinterpret_cast<void(__cdecl *)(const char *)>(Offsets::FUN_FRAMEXML_RELEASE_HASH_NODE)(inherits);
    }
    UI::FrameObject::Push(L, actor);
    return 1;
}

int __cdecl Script_GetNumActors(void *L) {
    void *frame = Self(L);
    if (frame == nullptr)
        return 0;
    Game::Lua::PushNumber(L, static_cast<double>(F(frame).actors.size()));
    return 1;
}

int __cdecl Script_GetActorAtIndex(void *L) {
    void *frame = Self(L);
    if (frame == nullptr || !NumberArgs(L, 2, 1, FrameName(frame), "GetActorAtIndex(index)"))
        return 0;
    const std::vector<Actor *> &actors = F(frame).actors;
    const double index = Game::Lua::ToNumber(L, 2);
    if (!(index >= 1.0 && index <= static_cast<double>(actors.size())))
        return 0;
    UI::FrameObject::Push(L, actors[static_cast<size_t>(index) - 1]);
    return 1;
}

int __cdecl Script_SetCameraPosition(void *L) {
    void *frame = Self(L);
    if (frame == nullptr || !NumberArgs(L, 2, 3, FrameName(frame), "SetCameraPosition(x, y, z)"))
        return 0;
    F(frame).position = Vec3Args(L, 2);
    ApplyCamera(frame);
    return 0;
}

int __cdecl Script_GetCameraPosition(void *L) {
    void *frame = Self(L);
    return frame != nullptr ? PushVec3(L, F(frame).position) : 0;
}

int __cdecl Script_SetCameraOrientationByYawPitchRoll(void *L) {
    void *frame = Self(L);
    if (frame == nullptr ||
        !NumberArgs(L, 2, 3, FrameName(frame), "SetCameraOrientationByYawPitchRoll(yaw, pitch, roll)"))
        return 0;
    SetOrientation(F(frame), FloatArg(L, 2), FloatArg(L, 3), FloatArg(L, 4));
    ApplyCamera(frame);
    return 0;
}

int __cdecl Script_SetCameraOrientationByAxisVectors(void *L) {
    void *frame = Self(L);
    if (frame == nullptr ||
        !NumberArgs(L, 2, 9, FrameName(frame),
                    "SetCameraOrientationByAxisVectors(forwardX, forwardY, forwardZ, rightX, rightY, rightZ, upX, "
                    "upY, upZ)"))
        return 0;
    Fields &f = F(frame);
    f.forward = Vec3Args(L, 2);
    f.right = Vec3Args(L, 5);
    f.up = Vec3Args(L, 8);
    ApplyCamera(frame);
    return 0;
}

template <Vec3 Fields::*Field> int __cdecl GetVec3(void *L) {
    void *frame = Self(L);
    return frame != nullptr ? PushVec3(L, F(frame).*Field) : 0;
}

constexpr char kUsageSetFieldOfView[] = "SetCameraFieldOfView(fov)";
constexpr char kUsageSetNearClip[] = "SetCameraNearClip(nearClip)";
constexpr char kUsageSetFarClip[] = "SetCameraFarClip(farClip)";

template <float Fields::*Field, const char *Usage> int __cdecl SetCameraScalar(void *L) {
    void *frame = Self(L);
    if (frame == nullptr || !NumberArgs(L, 2, 1, FrameName(frame), Usage))
        return 0;
    F(frame).*Field = FloatArg(L, 2);
    ApplyCamera(frame);
    return 0;
}

template <float Fields::*Field> int __cdecl GetCameraScalar(void *L) {
    void *frame = Self(L);
    if (frame == nullptr)
        return 0;
    Game::Lua::PushNumber(L, F(frame).*Field);
    return 1;
}

void NotifyDrawLayerChanged(void *frame, uint32_t layer) {
    reinterpret_cast<void(__fastcall *)(void *, void *, uint32_t)>(Offsets::FUN_SIMPLEFRAME_NOTIFY_DRAW_LAYER_CHANGED)(
        frame, nullptr, layer);
}

int __cdecl Script_SetDrawLayer(void *L) {
    void *frame = Self(L);
    if (frame == nullptr)
        return 0;
    uint32_t layer = 0;
    if (!Game::Lua::IsString(L, 2) || !DrawLayerFromName(Game::Lua::ToString(L, 2), layer)) {
        Game::Lua::Error(L, "Usage: %s:SetDrawLayer(layer)", FrameName(frame));
        return 0;
    }
    Fields &f = F(frame);
    if (layer == f.drawLayer)
        return 0;
    const uint32_t old = f.drawLayer;
    f.drawLayer = layer;
    NotifyDrawLayerChanged(frame, old);
    NotifyDrawLayerChanged(frame, layer);
    return 0;
}

int __cdecl Script_GetDrawLayer(void *L) {
    void *frame = Self(L);
    if (frame == nullptr)
        return 0;
    Game::Lua::PushString(
        L, reinterpret_cast<const char *(__cdecl *)(uint32_t)>(Offsets::FUN_DRAW_LAYER_TO_STRING)(F(frame).drawLayer));
    Game::Lua::PushNumber(L, 0);
    return 2;
}

int __cdecl Script_SetPaused(void *L) {
    if (void *frame = Self(L))
        F(frame).paused = Game::Lua::ToBoolean(L, 2) != 0;
    return 0;
}

int __cdecl Script_SetAllowOverlappedModels(void *L) {
    if (void *frame = Self(L))
        F(frame).allowOverlappedModels = Game::Lua::ToBoolean(L, 2) != 0;
    return 0;
}

int __cdecl Script_GetAllowOverlappedModels(void *L) {
    void *frame = Self(L);
    if (frame == nullptr)
        return 0;
    Game::Lua::PushBool(L, F(frame).allowOverlappedModels);
    return 1;
}

int __cdecl Script_Project3DPointTo2D(void *L) {
    void *frame = Self(L);
    if (frame == nullptr || !NumberArgs(L, 2, 3, FrameName(frame), "Project3DPointTo2D(pointX, pointY, pointZ)"))
        return 0;
    double out[3];
    if (!Project(frame, Vec3Args(L, 2), out))
        return 0;
    for (double v : out)
        Game::Lua::PushNumber(L, v);
    return 3;
}

int __cdecl Script_SetViewInsets(void *L) {
    void *frame = Self(L);
    if (frame == nullptr || !NumberArgs(L, 2, 4, FrameName(frame), "SetViewInsets(left, right, top, bottom)"))
        return 0;
    for (int i = 0; i < 4; ++i)
        F(frame).viewInsets[i] = FloatArg(L, 2 + i);
    return 0;
}

int __cdecl Script_GetViewInsets(void *L) {
    void *frame = Self(L);
    if (frame == nullptr)
        return 0;
    for (float v : F(frame).viewInsets)
        Game::Lua::PushNumber(L, v);
    return 4;
}

int __cdecl Script_SetViewTranslation(void *L) {
    void *frame = Self(L);
    if (frame == nullptr || !NumberArgs(L, 2, 2, FrameName(frame), "SetViewTranslation(x, y)"))
        return 0;
    F(frame).viewTranslation[0] = FloatArg(L, 2);
    F(frame).viewTranslation[1] = FloatArg(L, 3);
    return 0;
}

int __cdecl Script_GetViewTranslation(void *L) {
    void *frame = Self(L);
    if (frame == nullptr)
        return 0;
    Game::Lua::PushNumber(L, F(frame).viewTranslation[0]);
    Game::Lua::PushNumber(L, F(frame).viewTranslation[1]);
    return 2;
}

int __cdecl Script_SetLightType(void *L) {
    void *frame = Self(L);
    if (frame == nullptr || !NumberArgs(L, 2, 1, FrameName(frame), "SetLightType(lightType)"))
        return 0;
    SetLightType(frame, Game::Lua::ToNumber(L, 2) != 0.0 ? 1u : 0u);
    return 0;
}

int __cdecl Script_GetLightType(void *L) {
    void *frame = Self(L);
    if (frame == nullptr)
        return 0;
    Game::Lua::PushNumber(L, F(frame).lightType);
    return 1;
}

int __cdecl Script_SetLightVisible(void *L) {
    if (void *frame = Self(L))
        SetLightVisible(frame, Game::Lua::ToBoolean(L, 2) != 0);
    return 0;
}

int __cdecl Script_IsLightVisible(void *L) {
    void *frame = Self(L);
    if (frame == nullptr)
        return 0;
    Game::Lua::PushBool(L, F(frame).lightVisible);
    return 1;
}

int __cdecl Script_SetLightDirection(void *L) {
    void *frame = Self(L);
    if (frame == nullptr || !NumberArgs(L, 2, 3, FrameName(frame), "SetLightDirection(x, y, z)"))
        return 0;
    SetLightDirection(frame, Vec3Args(L, 2));
    return 0;
}

int __cdecl Script_SetLightPosition(void *L) {
    void *frame = Self(L);
    if (frame == nullptr || !NumberArgs(L, 2, 3, FrameName(frame), "SetLightPosition(x, y, z)"))
        return 0;
    SetLightPosition(frame, Vec3Args(L, 2));
    return 0;
}

template <float *(*Color)(void *), const char *Usage> int __cdecl SetLightColor(void *L) {
    void *frame = Self(L);
    if (frame == nullptr || !NumberArgs(L, 2, 3, FrameName(frame), Usage))
        return 0;
    SetColor(Color(frame), Vec3Args(L, 2));
    return 0;
}

template <float *(*Color)(void *)> int __cdecl GetLightColor(void *L) {
    void *frame = Self(L);
    if (frame == nullptr)
        return 0;
    const float *c = Color(frame);
    return PushVec3(L, {c[0], c[1], c[2]});
}

constexpr char kUsageSetAmbient[] = "SetLightAmbientColor(colorR, colorG, colorB)";
constexpr char kUsageSetDiffuse[] = "SetLightDiffuseColor(colorR, colorG, colorB)";

int __cdecl Script_SetFogColor(void *L) {
    void *frame = Self(L);
    if (frame == nullptr || !NumberArgs(L, 2, 3, FrameName(frame), "SetFogColor(colorR, colorG, colorB)"))
        return 0;
    SetFogColor(frame, Vec3Args(L, 2));
    return 0;
}

int __cdecl Script_GetFogColor(void *L) {
    void *frame = Self(L);
    if (frame == nullptr)
        return 0;
    const uint32_t c = At<uint32_t>(frame, Offsets::OFF_SIMPLEMODEL_FOG_COLOR);
    return PushVec3(L, {((c >> 16) & 0xFF) / 255.0f, ((c >> 8) & 0xFF) / 255.0f, (c & 0xFF) / 255.0f});
}

template <uintptr_t Offset, const char *Usage> int __cdecl SetFogDistance(void *L) {
    void *frame = Self(L);
    if (frame == nullptr || !NumberArgs(L, 2, 1, FrameName(frame), Usage))
        return 0;
    At<float>(frame, Offset) = FloatArg(L, 2);
    return 0;
}

template <uintptr_t Offset> int __cdecl GetFogDistance(void *L) {
    void *frame = Self(L);
    if (frame == nullptr)
        return 0;
    Game::Lua::PushNumber(L, At<float>(frame, Offset));
    return 1;
}

constexpr char kUsageSetFogNear[] = "SetFogNear(near)";
constexpr char kUsageSetFogFar[] = "SetFogFar(far)";

int __cdecl Script_ClearFog(void *L) {
    if (void *frame = Self(L))
        FogFlags(frame) &= ~static_cast<uint32_t>(Offsets::SIMPLEMODEL_FOGGED);
    return 0;
}

struct Method {
    const char *name;
    Game::Lua::CFunction func;
};

const Method kMethods[] = {
    {"CreateActor", &Script_CreateActor},
    {"GetNumActors", &Script_GetNumActors},
    {"GetActorAtIndex", &Script_GetActorAtIndex},
    {"SetCameraPosition", &Script_SetCameraPosition},
    {"GetCameraPosition", &Script_GetCameraPosition},
    {"SetCameraOrientationByYawPitchRoll", &Script_SetCameraOrientationByYawPitchRoll},
    {"SetCameraOrientationByAxisVectors", &Script_SetCameraOrientationByAxisVectors},
    {"GetCameraForward", &GetVec3<&Fields::forward>},
    {"GetCameraRight", &GetVec3<&Fields::right>},
    {"GetCameraUp", &GetVec3<&Fields::up>},
    {"SetCameraFieldOfView", &SetCameraScalar<&Fields::fieldOfView, kUsageSetFieldOfView>},
    {"GetCameraFieldOfView", &GetCameraScalar<&Fields::fieldOfView>},
    {"SetCameraNearClip", &SetCameraScalar<&Fields::nearClip, kUsageSetNearClip>},
    {"GetCameraNearClip", &GetCameraScalar<&Fields::nearClip>},
    {"SetCameraFarClip", &SetCameraScalar<&Fields::farClip, kUsageSetFarClip>},
    {"GetCameraFarClip", &GetCameraScalar<&Fields::farClip>},
    {"SetDrawLayer", &Script_SetDrawLayer},
    {"GetDrawLayer", &Script_GetDrawLayer},
    {"SetPaused", &Script_SetPaused},
    {"SetAllowOverlappedModels", &Script_SetAllowOverlappedModels},
    {"GetAllowOverlappedModels", &Script_GetAllowOverlappedModels},
    {"Project3DPointTo2D", &Script_Project3DPointTo2D},
    {"SetViewInsets", &Script_SetViewInsets},
    {"GetViewInsets", &Script_GetViewInsets},
    {"SetViewTranslation", &Script_SetViewTranslation},
    {"GetViewTranslation", &Script_GetViewTranslation},
    {"SetLightType", &Script_SetLightType},
    {"GetLightType", &Script_GetLightType},
    {"SetLightVisible", &Script_SetLightVisible},
    {"IsLightVisible", &Script_IsLightVisible},
    {"SetLightDirection", &Script_SetLightDirection},
    {"GetLightDirection", &GetVec3<&Fields::lightDirection>},
    {"SetLightPosition", &Script_SetLightPosition},
    {"GetLightPosition", &GetVec3<&Fields::lightPosition>},
    {"SetLightAmbientColor", &SetLightColor<&AmbientOf, kUsageSetAmbient>},
    {"GetLightAmbientColor", &GetLightColor<&AmbientOf>},
    {"SetLightDiffuseColor", &SetLightColor<&DiffuseOf, kUsageSetDiffuse>},
    {"GetLightDiffuseColor", &GetLightColor<&DiffuseOf>},
    {"SetFogColor", &Script_SetFogColor},
    {"GetFogColor", &Script_GetFogColor},
    {"SetFogNear", &SetFogDistance<Offsets::OFF_SIMPLEMODEL_FOG_NEAR, kUsageSetFogNear>},
    {"GetFogNear", &GetFogDistance<Offsets::OFF_SIMPLEMODEL_FOG_NEAR>},
    {"SetFogFar", &SetFogDistance<Offsets::OFF_SIMPLEMODEL_FOG_FAR, kUsageSetFogFar>},
    {"GetFogFar", &GetFogDistance<Offsets::OFF_SIMPLEMODEL_FOG_FAR>},
    {"ClearFog", &Script_ClearFog},
};

// The metatable's method table: a Frame's (CSimpleFrame::RegisterScriptMethods,
// whose hook in frame/Methods.cpp adds this DLL's Frame methods), then ours —
// none of Model's.
void __cdecl RegisterScriptMethods(void *L) {
    reinterpret_cast<void(__cdecl *)(void *)>(Offsets::FUN_SIMPLEFRAME_REGISTER_SCRIPT_METHODS)(L);
    reinterpret_cast<void(__cdecl *)(void *, const Method *, int)>(Offsets::FUN_REGISTER_FRAME_METHODS)(
        L, kMethods, static_cast<int>(sizeof(kMethods) / sizeof(kMethods[0])));
}

void RegisterLuaFunctions() {
    void *L = Game::Lua::State();
    using CreateMeta_t = int(__cdecl *)(void *, void(__cdecl *)(void *));
    g_metaTable = reinterpret_cast<CreateMeta_t>(Offsets::FUN_FRAMESCRIPT_CREATE_SCRIPT_META_TABLE)(
        L, &RegisterScriptMethods);
    CreateActorMetaTable();
    reinterpret_cast<int(__cdecl *)(const char *, void *(__cdecl *)(void *), bool)>(
        Offsets::FUN_FRAMEXML_REGISTER_FACTORY)(kTypeName, &CreateModelScene, false);
}

// The metatables are refs in the Lua state the /reload closes; the next state
// builds its own. Every ModelScene and actor is gone by then (CGGameUI::Shutdown
// destroys the frames before the state closes).
void PrepareForReload() {
    g_metaTable = 0;
    ReleaseActorMetaTable();
}

const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};
const Game::ReloadAutoRegister _reload{&PrepareForReload};

} // namespace

void *M2SceneOf(void *sceneFrame) { return Scene(sceneFrame); }

float FrameAlpha(void *sceneFrame) {
    // CSimpleModel::UpdateAlpha's alpha: (uint8)(alpha * parentAlpha / 255) / 255.
    const uint32_t alpha = At<uint8_t>(sceneFrame, Offsets::OFF_FRAME_ALPHA);
    const uint32_t parentAlpha = At<uint8_t>(sceneFrame, Offsets::OFF_FRAME_PARENT_ALPHA);
    return static_cast<float>(static_cast<uint8_t>(alpha * parentAlpha / 255)) / 255.0f;
}

bool NumberArgs(void *L, int first, int count, const char *owner, const char *usage) {
    for (int i = 0; i < count; ++i) {
        if (Game::Lua::IsNumber(L, first + i) == 0) {
            Game::Lua::Error(L, "Usage: %s:%s", owner, usage);
            return false;
        }
    }
    return true;
}

float FloatArg(void *L, int index) { return static_cast<float>(Game::Lua::ToNumber(L, index)); }

} // namespace Model::Scene
