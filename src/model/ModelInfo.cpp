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

// `C_ModelInfo`: the UiModelScene rows a ModelScene's Lua lays a scene out by.
//
//   GetModelSceneInfoByID(modelSceneID) -> modelSceneType, modelCameraIDs, modelActorsIDs, flags
//   GetModelSceneCameraInfoByID(modelSceneCameraID) -> UIModelSceneCameraInfo
//   GetModelSceneActorInfoByID(modelActorID) -> UIModelSceneActorInfo
//   GetModelSceneActorDisplayInfoByID(modelActorDisplayID) -> UIModelSceneActorDisplayInfo
//   AddActiveModelScene / AddActiveModelSceneActor / ClearActiveModelScene /
//   ClearActiveModelSceneActor: do nothing, as in retail's public clients
//
// The rows are model/SceneData.h's, shaped as retail returns them. An unknown
// ID returns nothing. The vector3 fields are Vector3DMixin objects, made as
// CreateVector3D makes them: the mixin's fields, then x, y and z. The actor
// flags split into the three useCenterForOrigin booleans; a normalized scale
// of 0 is a nil normalizeScaleAggressiveness, and a display ID of 0 a nil
// modelActorDisplayID.

#include "Game.h"
#include "baselib/Mixin.h"
#include "model/SceneData.h"

#include <cstddef>
#include <cstdint>

namespace Model::ModelInfo {

namespace {

using namespace Model::SceneData;

// Enum.UIModelSceneActorFlag.
constexpr uint32_t kUseCenterForOriginX = 0x2;
constexpr uint32_t kUseCenterForOriginY = 0x4;
constexpr uint32_t kUseCenterForOriginZ = 0x8;

template <typename Row, size_t N> const Row *Find(const Row (&rows)[N], uint32_t id) {
    for (const Row &row : rows)
        if (row.id == id)
            return &row;
    return nullptr;
}

bool IdArg(void *L, uint32_t &id) {
    if (!Game::Lua::IsNumber(L, 1))
        return false;
    id = static_cast<uint32_t>(Game::Lua::ToNumber(L, 1));
    return true;
}

// t[key] = a Vector3DMixin object, on the table at the top of the stack.
void SetFieldVector3D(void *L, const char *key, const Vec3 &v) {
    Game::Lua::NewTable(L); // [t, vector]
    const int vector = Game::Lua::GetTop(L);
    Game::Lua::GetGlobal(L, "Vector3DMixin");
    if (Game::Lua::Type(L, -1) == Game::Lua::TYPE_TABLE)
        BaseLib::Mixin::Into(L, vector, vector + 1);
    Game::Lua::SetTop(L, vector);
    Game::Lua::SetFieldNumber(L, "x", v.x);
    Game::Lua::SetFieldNumber(L, "y", v.y);
    Game::Lua::SetFieldNumber(L, "z", v.z);
    Game::Lua::SetField(L, -2, key);
}

// An array of the IDs of every row of `rows` in `sceneID`, in ID order.
template <typename Row, size_t N> void PushSceneIDs(void *L, const Row (&rows)[N], uint32_t sceneID) {
    Game::Lua::NewTable(L);
    int n = 0;
    for (const Row &row : rows) {
        if (row.sceneID != sceneID)
            continue;
        Game::Lua::PushNumber(L, row.id);
        Game::Lua::RawSetI(L, -2, ++n);
    }
}

int __cdecl Script_GetModelSceneInfoByID(void *L) {
    uint32_t id = 0;
    const SceneRow *scene = IdArg(L, id) ? Find(kScenes, id) : nullptr;
    if (scene == nullptr)
        return 0;
    Game::Lua::PushNumber(L, scene->uiSystemType);
    PushSceneIDs(L, kCameras, id);
    PushSceneIDs(L, kActors, id);
    Game::Lua::PushNumber(L, scene->flags);
    return 4;
}

int __cdecl Script_GetModelSceneCameraInfoByID(void *L) {
    uint32_t id = 0;
    const CameraRow *camera = IdArg(L, id) ? Find(kCameras, id) : nullptr;
    if (camera == nullptr)
        return 0;
    Game::Lua::NewTable(L);
    Game::Lua::SetFieldNumber(L, "modelSceneCameraID", camera->id);
    Game::Lua::SetFieldString(L, "scriptTag", camera->scriptTag);
    Game::Lua::SetFieldString(L, "cameraType", "OrbitCamera");
    SetFieldVector3D(L, "target", camera->target);
    Game::Lua::SetFieldNumber(L, "yaw", camera->yaw);
    Game::Lua::SetFieldNumber(L, "pitch", camera->pitch);
    Game::Lua::SetFieldNumber(L, "roll", camera->roll);
    Game::Lua::SetFieldNumber(L, "zoomDistance", camera->zoomDistance);
    Game::Lua::SetFieldNumber(L, "minZoomDistance", camera->minZoomDistance);
    Game::Lua::SetFieldNumber(L, "maxZoomDistance", camera->maxZoomDistance);
    SetFieldVector3D(L, "zoomedTargetOffset", camera->zoomedTargetOffset);
    Game::Lua::SetFieldNumber(L, "zoomedYawOffset", camera->zoomedYawOffset);
    Game::Lua::SetFieldNumber(L, "zoomedPitchOffset", camera->zoomedPitchOffset);
    Game::Lua::SetFieldNumber(L, "zoomedRollOffset", camera->zoomedRollOffset);
    Game::Lua::SetFieldNumber(L, "flags", camera->flags);
    return 1;
}

int __cdecl Script_GetModelSceneActorInfoByID(void *L) {
    uint32_t id = 0;
    const ActorRow *actor = IdArg(L, id) ? Find(kActors, id) : nullptr;
    if (actor == nullptr)
        return 0;
    Game::Lua::NewTable(L);
    Game::Lua::SetFieldNumber(L, "modelActorID", actor->id);
    Game::Lua::SetFieldString(L, "scriptTag", actor->scriptTag);
    SetFieldVector3D(L, "position", actor->position);
    Game::Lua::SetFieldNumber(L, "yaw", actor->yaw);
    Game::Lua::SetFieldNumber(L, "pitch", actor->pitch);
    Game::Lua::SetFieldNumber(L, "roll", actor->roll);
    if (actor->normalizedScale != 0.0f)
        Game::Lua::SetFieldNumber(L, "normalizeScaleAggressiveness", actor->normalizedScale);
    Game::Lua::SetFieldBool(L, "useCenterForOriginX", (actor->flags & kUseCenterForOriginX) != 0);
    Game::Lua::SetFieldBool(L, "useCenterForOriginY", (actor->flags & kUseCenterForOriginY) != 0);
    Game::Lua::SetFieldBool(L, "useCenterForOriginZ", (actor->flags & kUseCenterForOriginZ) != 0);
    if (actor->displayID != 0)
        Game::Lua::SetFieldNumber(L, "modelActorDisplayID", actor->displayID);
    return 1;
}

int __cdecl Script_GetModelSceneActorDisplayInfoByID(void *L) {
    uint32_t id = 0;
    const ActorDisplayRow *display = IdArg(L, id) ? Find(kActorDisplays, id) : nullptr;
    if (display == nullptr)
        return 0;
    Game::Lua::NewTable(L);
    Game::Lua::SetFieldNumber(L, "animation", display->animation);
    Game::Lua::SetFieldNumber(L, "animationVariation", display->animationVariation);
    Game::Lua::SetFieldNumber(L, "animSpeed", display->animSpeed);
    if (display->animKitID != 0)
        Game::Lua::SetFieldNumber(L, "animationKitID", display->animKitID);
    if (display->spellVisualKitID != 0)
        Game::Lua::SetFieldNumber(L, "spellVisualKitID", display->spellVisualKitID);
    Game::Lua::SetFieldNumber(L, "alpha", display->alpha);
    Game::Lua::SetFieldNumber(L, "scale", display->scale);
    return 1;
}

// Retail: "This function does nothing in public clients".
int __cdecl Script_Nothing(void *) { return 0; }

// Enum.ModelSceneSetting: the camera rows' flags, which OrbitCameraMixin tests
// on every update (ShouldAlignLightToOrbitDelta).
const Game::Lua::EnumIntegerEntry kModelSceneSetting[] = {
    {"AlignLightToOrbitDelta", 1},
};

// Enum.UIModelSceneActorFlag.
const Game::Lua::EnumIntegerEntry kActorFlag[] = {
    {"Deprecated1", 0x1},
    {"UseCenterForOriginX", 0x2},
    {"UseCenterForOriginY", 0x4},
    {"UseCenterForOriginZ", 0x8},
};

void RegisterLuaFunctions() {
    Game::Lua::RegisterIntegerEnum("Enum", "ModelSceneSetting", kModelSceneSetting,
                                   static_cast<int>(sizeof kModelSceneSetting / sizeof kModelSceneSetting[0]));
    Game::Lua::RegisterIntegerEnum("Enum", "UIModelSceneActorFlag", kActorFlag,
                                   static_cast<int>(sizeof kActorFlag / sizeof kActorFlag[0]));
    Game::Lua::RegisterTableFunction("C_ModelInfo", "GetModelSceneInfoByID", &Script_GetModelSceneInfoByID);
    Game::Lua::RegisterTableFunction("C_ModelInfo", "GetModelSceneCameraInfoByID",
                                     &Script_GetModelSceneCameraInfoByID);
    Game::Lua::RegisterTableFunction("C_ModelInfo", "GetModelSceneActorInfoByID", &Script_GetModelSceneActorInfoByID);
    Game::Lua::RegisterTableFunction("C_ModelInfo", "GetModelSceneActorDisplayInfoByID",
                                     &Script_GetModelSceneActorDisplayInfoByID);
    for (const char *name :
         {"AddActiveModelScene", "AddActiveModelSceneActor", "ClearActiveModelScene", "ClearActiveModelSceneActor"})
        Game::Lua::RegisterTableFunction("C_ModelInfo", name, &Script_Nothing);
}

const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace

} // namespace Model::ModelInfo
