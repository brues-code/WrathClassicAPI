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

// `ModelSceneActor`: one model in a ModelScene (FrameAPIModelSceneFrameActorBase-
// Documentation.lua's contract; the port of ClassicAPI's src/model/SceneActor.cpp,
// whose retail probes set the defaults in Scene.h).
//
// An actor is an engine script object (Scene.h) owned by its scene frame, and
// holds one CM2Model in the frame's own M2 scene. Its model is set up the way
// CSimpleModel::SetModel (0x009600E0) sets up a Model frame's: the frame's
// alpha, a sequence callback, and the frame's lighting callback with the frame
// as its argument, so the scene's light and fog light it. Each frame the scene's
// update links it (Link: CSimpleModel::OnLayerUpdate's steps), advances the
// clock, then runs its update (Update: load state and scripts); the paint places
// it (Place) where RenderModel calls UpdateModel.
//
//   - The placement is the actor's position, yaw (about +Z), pitch (about +Y,
//     positive nose down, as the camera's) and roll (about +X), in that order,
//     and its own scale: the scene's projection already scales the picture
//     with the frame, so Model's root-scale factors are not applied.
//   - useCenterForOrigin moves the model so the centre of its active bounding
//     box on that axis is at the actor's position, before scale and rotation.
//   - The active bounding box is the current sequence's bounds and the max one
//     the model's overall bounds, both in model space.
//
// A creature display is built as the world builds a creature's model: the
// CreatureModelData file, its geosets and skin (ApplyMonsterGeosets,
// ReplaceMonsterSkin, as CGUnit_C::PostInit), and for a display with a baked
// NPC texture the character component CGUnit_C::InitializeComponent
// (0x00730100) builds once the model has loaded, with the display's items.
// While that dress composes the model is not drawn, as DressUpModel holds its
// own until RenderPrep is ready (0x00597F40); OnModelLoaded runs once it is.
//
// Animations loop by their own length (the M2 sequence timing); OnAnimFinished
// runs at the end of each lap of the root bone's sequence, which the model's
// sequence callback reports. SetPaused holds the root sequence at its current
// time (SetBoneSequenceTime each update) while the scene keeps running; it sets
// the global pause flag too unless affectsGlobalPause is false (ClassicAPI's
// retail probe).
//
// A mount actor seats a rider as CGUnit_C::CreateUnitMount (0x0073D5D0) does:
// the rider's model on the mount's attachment 0, playing the seat animation.
// The mount then animates the rider and holds a reference on it, and draws it
// while the rider is shown; the rider's placement is relative to the seat.
// CalculateMountScale is the world's mount scale, the mount display's
// CreatureModelScale; the world gives the rider 1 over it, as the retail
// AttachPlayerToMount requests. DetachFromMount only detaches: the rider keeps
// the seat animation (ClassicAPI's retail probe).
//
// XML: an <Actor virtual="true"> element is a template CreateActor names (the
// engine's file loader stores every virtual top-level node whatever its tag).
// Its inherits chain loads first, then the node's `alpha`, `scale`, `file` and
// <Scripts> (an element body, or `function="GlobalName"`), then OnLoad runs.

#include "model/Scene.h"

#include "Game.h"
#include "Guid.h"
#include "Offsets.h"
#include "dbc/Store.h"
#include "item/Arg.h"
#include "ui/FrameObject.h"
#include "xml/KeyValues.h"
#include "xml/Method.h"
#include "xml/Mixin.h"
#include "xml/Node.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

namespace Model::Scene {

namespace {

template <typename T> T &At(void *base, uintptr_t offset) {
    return *reinterpret_cast<T *>(static_cast<uint8_t *>(base) + offset);
}
template <typename T> T At(const void *base, uintptr_t offset) {
    return *reinterpret_cast<const T *>(static_cast<const uint8_t *>(base) + offset);
}
template <typename T> T Global(uintptr_t address) { return *reinterpret_cast<T *>(address); }

constexpr char kTypeName[] = "ModelSceneActor";

int g_typeId = 0;
int g_metaTable = 0;

// ---- engine calls -------------------------------------------------------------------

// __thiscall entry points mirrored as __fastcall with a dummy EDX.
using ScriptRegister_t = void(__fastcall *)(void *object, void *, const char *name);
using CreateName_t = void(__fastcall *)(void *object, void *, const char *source, char *dest, uint32_t size);
using RunScript_t = void(__fastcall *)(void *object, void *, const ScriptIx *script, int argCount,
                                       const char *taint);

int LuaRef(void *L) {
    return reinterpret_cast<int(__cdecl *)(void *, int)>(Offsets::LUAL_REF)(L, Game::Lua::REGISTRY_INDEX);
}
void LuaUnref(void *L, int ref) {
    reinterpret_cast<void(__cdecl *)(void *, int, int)>(Offsets::LUAL_UNREF)(L, Game::Lua::REGISTRY_INDEX, ref);
}
const char *LuaTainted() { return Global<const char *>(Offsets::VAR_LUA_TAINTED); }

void ReleaseM2(void *model) { reinterpret_cast<void(__fastcall *)(void *, void *)>(Offsets::FUN_M2MODEL_RELEASE)(model, nullptr); }

bool IsLoaded(void *model, bool recursive) {
    return reinterpret_cast<uint32_t(__fastcall *)(void *, void *, int, int)>(Offsets::FUN_M2MODEL_IS_LOADED)(
               model, nullptr, 0, recursive ? 1 : 0) != 0;
}

void *CreateM2(void *frame, const char *path, uint32_t flags) {
    return reinterpret_cast<void *(__fastcall *)(void *, void *, const char *, uint32_t)>(
        Offsets::FUN_M2SCENE_CREATE_MODEL)(M2SceneOf(frame), nullptr, path, flags);
}

// The model's sequence callback (M2SequenceCallback, cdecl): the end of a lap
// of the root bone's sequence is the actor's animation's end.
void __cdecl OnSequenceEnd(void * /*model*/, uint32_t boneId, uint32_t /*sequenceId*/, int interrupted,
                           uint32_t /*time*/, uint32_t paramLow, uint32_t /*paramHigh*/) {
    if (interrupted == 0 && boneId == 0xFFFFFFFFu)
        reinterpret_cast<Actor *>(static_cast<uintptr_t>(paramLow))->animFinished = true;
}

void SetSequenceCallback(void *model, Actor *actor) {
    using Set_t = void(__fastcall *)(void *, void *, const void *callback, uint32_t low, uint32_t high);
    reinterpret_cast<Set_t>(Offsets::FUN_M2MODEL_SET_SEQUENCE_CALLBACK)(
        model, nullptr, actor != nullptr ? reinterpret_cast<const void *>(&OnSequenceEnd) : nullptr,
        static_cast<uint32_t>(reinterpret_cast<uintptr_t>(actor)), 0);
}

// The current sequence's bounds, model space {bottom xyz, top xyz}; the model's
// overall bounds when it plays none. The model must be loaded.
void ActiveBounds(void *model, float (&box)[6]) {
    const void *data = At<const void *>(At<const void *>(model, Offsets::OFF_M2MODEL_SHARED), Offsets::OFF_M2SHARED_DATA);
    const uint32_t count = At<uint32_t>(data, Offsets::OFF_M2DATA_SEQUENCES);
    const auto *sequences = At<const uint8_t *>(data, Offsets::OFF_M2DATA_SEQUENCES + 4);
    const uint16_t index = At<uint16_t>(At<const void *>(model, Offsets::OFF_M2MODEL_BONES), Offsets::OFF_M2BONE_SEQUENCE_INDEX);
    if (sequences != nullptr && index < count) {
        std::memcpy(box, sequences + index * Offsets::M2SEQUENCE_SIZE + Offsets::OFF_M2SEQUENCE_BOUNDS, sizeof box);
        return;
    }
    reinterpret_cast<void *(__fastcall *)(void *, void *, float *)>(Offsets::FUN_M2MODEL_GET_BOUNDING_BOX)(model, nullptr,
                                                                                                        box);
}

// ---- the dress of a creature display -------------------------------------------------

// CGUnit_C::InitializeComponent's CreatureDisplayInfoExtra branch (0x00730100):
// the baked NPC texture as the skin, the display's customisation, then its
// items. The component takes a reference on the model.
void *BuildDress(void *model, const uint8_t *extra) {
    alignas(8) uint8_t data[Offsets::COMPONENT_DATA_SIZE];
    reinterpret_cast<void(__fastcall *)(void *, void *)>(Offsets::FUN_COMPONENT_DATA_CONSTRUCTOR)(data, nullptr);
    auto pref = [&](uintptr_t to, uintptr_t from) { At<uint32_t>(data, to) = At<uint32_t>(extra, from); };
    pref(Offsets::OFF_COMPONENT_PREF_RACE, Offsets::OFF_CREATUREDISPLAYINFOEXTRA_RACE);
    pref(Offsets::OFF_COMPONENT_PREF_SEX, Offsets::OFF_CREATUREDISPLAYINFOEXTRA_SEX);
    At<uint32_t>(data, Offsets::OFF_COMPONENT_PREF_CLASS) = 0;
    pref(Offsets::OFF_COMPONENT_PREF_SKIN, Offsets::OFF_CREATUREDISPLAYINFOEXTRA_SKIN);
    pref(Offsets::OFF_COMPONENT_PREF_FACE, Offsets::OFF_CREATUREDISPLAYINFOEXTRA_FACE);
    pref(Offsets::OFF_COMPONENT_PREF_HAIR_STYLE, Offsets::OFF_CREATUREDISPLAYINFOEXTRA_HAIR_STYLE);
    pref(Offsets::OFF_COMPONENT_PREF_HAIR_COLOR, Offsets::OFF_CREATUREDISPLAYINFOEXTRA_HAIR_COLOR);
    pref(Offsets::OFF_COMPONENT_PREF_FACIAL_HAIR, Offsets::OFF_CREATUREDISPLAYINFOEXTRA_FACIAL_HAIR);
    At<uint32_t>(data, Offsets::OFF_COMPONENT_DATA_FLAGS) |= 0x1;
    std::snprintf(reinterpret_cast<char *>(data + Offsets::OFF_COMPONENT_DATA_NPC_SKIN),
                  Offsets::COMPONENT_DATA_NPC_SKIN_SIZE, "%s%s", "Textures\\BakedNpcTextures\\",
                  At<const char *>(extra, Offsets::OFF_CREATUREDISPLAYINFOEXTRA_BAKE_NAME));
    At<void *>(data, Offsets::OFF_COMPONENT_DATA_MODEL) = model;
    ++At<uint32_t>(model, Offsets::OFF_M2MODEL_REFCOUNT);
    void *component = reinterpret_cast<void *(__cdecl *)()>(Offsets::FUN_COMPONENT_ALLOC)();
    if (component == nullptr) {
        ReleaseM2(model);
        return nullptr;
    }
    reinterpret_cast<bool(__fastcall *)(void *, void *, void *, const char *)>(Offsets::FUN_COMPONENT_INIT)(
        component, nullptr, data, nullptr);
    for (uint32_t slot = 0; slot < Offsets::CREATUREDISPLAYINFOEXTRA_ITEM_COUNT; ++slot) {
        const auto display = At<int32_t>(extra, Offsets::OFF_CREATUREDISPLAYINFOEXTRA_ITEMS + slot * 4);
        if (display != 0)
            reinterpret_cast<void(__fastcall *)(void *, void *, uint32_t, int32_t, int32_t)>(
                Offsets::FUN_COMPONENT_ADD_ITEM)(component, nullptr, slot, display, 0);
    }
    return component;
}

void FreeDress(void *component) {
    if (component != nullptr)
        reinterpret_cast<void(__cdecl *)(void *)>(Offsets::FUN_COMPONENT_FREE)(component);
}

bool DressReady(void *component) {
    return (reinterpret_cast<uint32_t(__fastcall *)(void *, void *, int)>(Offsets::FUN_COMPONENT_RENDER_PREP)(
                component, nullptr, 0) &
            1) != 0;
}

// ---- scripts ------------------------------------------------------------------------

const char *const kScriptNames[Actor::S_COUNT] = {"OnLoad",         "OnUpdate",      "OnModelCleared",
                                                  "OnModelLoading", "OnModelLoaded", "OnAnimFinished"};

const char *ScriptWrapper(int index) {
    return index == Actor::S_ONUPDATE ? "return function(self,elapsed) %s end" : "return function(self) %s end";
}

int ScriptIndex(const char *name) {
    if (name != nullptr)
        for (int i = 0; i < Actor::S_COUNT; ++i)
            if (_stricmp(name, kScriptNames[i]) == 0)
                return i;
    return -1;
}

// Puts `ref` in the slot, freeing the function it held.
void AssignScript(ScriptIx &slot, int ref, const char *taint) {
    void *L = Game::Lua::State();
    if (slot.luaRef > 0 && L != nullptr)
        LuaUnref(L, slot.luaRef);
    slot.luaRef = ref > 0 ? ref : 0;
    slot.taint = taint;
}

} // namespace

// ---- the class ----------------------------------------------------------------------

int ActorTypeId() {
    if (g_typeId == 0)
        g_typeId = ++At<int>(reinterpret_cast<void *>(Offsets::VAR_SCRIPTOBJECT_TYPE_COUNTER), 0);
    return g_typeId;
}

int ActorMetaTable() { return g_metaTable; }

Actor::Actor(void *sceneFrame, const char *name) : frame_(sceneFrame) {
    static_assert(sizeof(Header) == 0x1C, "the engine's script-object header");
    if (name != nullptr && *name != '\0') {
        char fullName[1024];
        reinterpret_cast<CreateName_t>(Offsets::FUN_SCRIPTOBJECT_CREATE_NAME)(this, nullptr, name, fullName,
                                                                              sizeof fullName);
        name_ = fullName;
    }
    reinterpret_cast<ScriptRegister_t>(Offsets::FUN_FRAMESCRIPT_OBJECT_SCRIPT_REGISTER)(
        this, nullptr, name_.empty() ? nullptr : name_.c_str());
    serial = AddLiveActor(this);
}

Actor::~Actor() {
    RemoveLiveActor(serial);
    ReleaseModel();
    for (ScriptIx &slot : scripts)
        AssignScript(slot, 0, nullptr);
    reinterpret_cast<ScriptRegister_t>(Offsets::FUN_FRAMESCRIPT_OBJECT_UNREGISTER)(
        this, nullptr, name_.empty() ? nullptr : name_.c_str());
}

const char *Actor::GetName() { return name_.empty() ? nullptr : name_.c_str(); }

int Actor::GetScriptMetaTable() { return g_metaTable; }

ScriptIx *Actor::GetScriptByName(const char *name, ScriptData *data) {
    const int index = ScriptIndex(name);
    if (index < 0)
        return nullptr;
    if (data != nullptr)
        data->wrapper = ScriptWrapper(index);
    return &scripts[index];
}

// The actor, and what every script object is (CScriptObject's own methods
// resolve self with that type); retail's actor is neither a Frame nor a Region.
bool Actor::IsAType(int type) {
    const int objectType = Global<int>(Offsets::VAR_SCRIPTOBJECT_OBJECT_TYPE);
    return type == ActorTypeId() || (objectType != 0 && type == objectType);
}

void *Actor::GetScriptObjectParent() { return frame_; }

bool Actor::IsATypeName(const char *typeName) {
    return typeName != nullptr && (_stricmp(typeName, kTypeName) == 0 || _stricmp(typeName, "Object") == 0);
}

const char *Actor::GetObjectTypeName() { return kTypeName; }

void Actor::RunScript(int index, int nargs) {
    void *L = Game::Lua::State();
    if (L == nullptr)
        return;
    if (scripts[index].luaRef == 0) {
        Game::Lua::SetTop(L, -1 - nargs);
        return;
    }
    reinterpret_cast<RunScript_t>(Offsets::FUN_FRAMESCRIPT_OBJECT_RUN_SCRIPT)(this, nullptr, &scripts[index], nargs,
                                                                              nullptr);
}

// ---- the model ----------------------------------------------------------------------

void Actor::ReleaseModel() {
    if (model == nullptr)
        return;
    DropKit(); // the kit stays, for the next model
    // A rider leaves its seat, which drops the mount's reference on it. A
    // mount's riders leave when its model is freed (~CM2Model detaches every
    // child).
    if (Seated())
        reinterpret_cast<void(__fastcall *)(void *, void *)>(Offsets::FUN_M2MODEL_DETACH_FROM_PARENT)(model, nullptr);
    SetSequenceCallback(model, nullptr);
    FreeDress(component); // drops the dress's reference on the model
    At<const void *>(model, Offsets::OFF_M2MODEL_LIGHTING_CALLBACK) = nullptr;
    At<void *>(model, Offsets::OFF_M2MODEL_LIGHTING_ARG) = nullptr;
    ReleaseM2(model);
    model = nullptr;
    modelPath.clear();
    pendingDress = nullptr;
    component = nullptr;
    componentReady = false;
    loaded = false;
    animFinished = false;
    heldTimeMs = -1;
    unitGuid[0] = unitGuid[1] = 0;
    dressable = false;
    mainHand = offHand = HandLane{};
    nextHand = 15;
    mainHandItemID = offHandItemID = 0;
    displayScale = 1.0f;
}

void Actor::ClearModel() {
    if (model == nullptr)
        return;
    ReleaseModel();
    RunScript(S_ONMODELCLEARED, 0);
}

void Actor::SetModel(void *newModel, const char *path, const uint8_t *dressRecord, const UnitSource *unit) {
    ClearModel();
    ReleaseModel(); // OnModelCleared may have set one: the caller's new model wins
    if (newModel == nullptr)
        return;
    model = newModel;
    modelPath = path != nullptr ? path : "";
    pendingDress = dressRecord;
    At<const void *>(model, Offsets::OFF_M2MODEL_LIGHTING_CALLBACK) =
        reinterpret_cast<const void *>(Offsets::FUN_SIMPLEMODEL_LIGHTING_CALLBACK);
    At<void *>(model, Offsets::OFF_M2MODEL_LIGHTING_ARG) = frame_;
    SetSequenceCallback(model, this);
    ApplyAlpha();
    PlayAnimation();
    if (unit != nullptr)
        ShowUnit(*unit);
    // Last: the script may clear or replace the model.
    RunScript(S_ONMODELLOADING, 0);
}

void Actor::PlayAnimation() {
    if (model == nullptr)
        return;
    using SetSequence_t =
        void(__fastcall *)(void *, void *, uint32_t, uint32_t, uint32_t, uint32_t, float, int, int);
    heldTimeMs = -1; // a paused actor holds the new animation's first frame
    animFinished = false;
    reinterpret_cast<SetSequence_t>(Offsets::FUN_M2MODEL_SET_BONE_SEQUENCE)(
        model, nullptr, 0xFFFFFFFFu, animation, static_cast<uint32_t>(variation),
        static_cast<uint32_t>(animOffsetSec * 1000.0f), animSpeed, static_cast<int>(blendOperation), 1);
}

void Actor::ApplyAlpha() {
    if (model != nullptr)
        At<float>(model, Offsets::OFF_M2MODEL_FADE_ALPHA) = alpha * kitAlpha * FrameAlpha(frame_);
}

// A seated rider is animated by its mount and drawn while the mount is
// (AttachToParent takes it off the animate list); its own flag says whether.
void Actor::Link() {
    if (model == nullptr)
        return;
    if (shown && component != nullptr)
        componentReady = DressReady(component);
    const bool drawn = shown && (component == nullptr || componentReady);
    uint32_t &flags = At<uint32_t>(model, Offsets::OFF_M2MODEL_FFLAGS);
    if (Seated()) {
        flags = drawn ? flags | Offsets::M2MODEL_FFLAGS_DRAWN_WITH_PARENT
                      : flags & ~static_cast<uint32_t>(Offsets::M2MODEL_FFLAGS_DRAWN_WITH_PARENT);
        return;
    }
    if (!shown)
        return;
    reinterpret_cast<void(__fastcall *)(void *, void *, int)>(Offsets::FUN_M2MODEL_SET_ANIMATING)(model, nullptr, 1);
    if (drawn)
        flags |= Offsets::M2MODEL_FFLAGS_DRAWN;
}

bool Actor::Seated() const {
    return model != nullptr && At<const void *>(model, Offsets::OFF_M2MODEL_ATTACH_PARENT) != nullptr;
}

// CreateUnitMount's seating: the rider on the seat, playing the seat
// animation. A rider already seated moves here. The mount may not hang, at
// any depth, from the rider: the engine animates a model's parents first.
bool Actor::Seat(Actor &rider, uint32_t seatAnimation) {
    if (model == nullptr || rider.model == nullptr || rider.frame_ != frame_)
        return false;
    for (const void *m = model; m != nullptr; m = At<const void *>(m, Offsets::OFF_M2MODEL_ATTACH_PARENT))
        if (m == rider.model)
            return false;
    // A loaded mount without a seat leaves the rider unattached.
    reinterpret_cast<void(__fastcall *)(void *, void *, void *, uint32_t, const float *, int)>(
        Offsets::FUN_M2MODEL_ATTACH_TO_PARENT)(rider.model, nullptr, model, Offsets::M2_ATTACH_MOUNT, nullptr, 0);
    if (At<const void *>(rider.model, Offsets::OFF_M2MODEL_ATTACH_PARENT) != model)
        return false;
    rider.animation = seatAnimation;
    rider.PlayAnimation();
    return true;
}

void Actor::Update(float elapsedSec) {
    if (model != nullptr && !loaded) {
        bool ready = IsLoaded(model, false);
        if (ready && pendingDress != nullptr && component == nullptr) {
            component = BuildDress(model, pendingDress);
            pendingDress = nullptr;
        }
        if (component != nullptr && !componentReady)
            componentReady = DressReady(component); // also while hidden, when Link does not run
        if (component != nullptr && !componentReady)
            ready = false;
        if (ready && IsLoaded(model, true)) {
            loaded = true;
            ApplyKit();
            RunScript(S_ONMODELLOADED, 0);
        }
    }
    DrainKit();
    if (model != nullptr && loaded && (paused || globalPaused)) {
        if (heldTimeMs < 0)
            heldTimeMs = At<uint32_t>(At<void *>(model, Offsets::OFF_M2MODEL_BONES), Offsets::OFF_M2BONE_SEQUENCE_CURRENT_TIME);
        reinterpret_cast<void(__fastcall *)(void *, void *, uint32_t, uint32_t)>(
            Offsets::FUN_M2MODEL_SET_BONE_SEQUENCE_TIME)(model, nullptr, 0xFFFFFFFFu, static_cast<uint32_t>(heldTimeMs));
    }
    if (model != nullptr && animFinished) {
        animFinished = false;
        RunScript(S_ONANIMFINISHED, 0);
    }
    if (scripts[S_ONUPDATE].luaRef != 0) {
        if (void *L = Game::Lua::State()) {
            Game::Lua::PushNumber(L, elapsedSec);
            RunScript(S_ONUPDATE, 1);
        }
    }
}

// world = position + R (scale (p + centre offset)), R = Rz(yaw) Ry(pitch)
// Rx(roll), as a row-vector C44Matrix (p' = p M: rows are R's columns times
// the scale, translation in the last row).
void Actor::Place() {
    if (model == nullptr)
        return;
    const float cy = std::cos(yaw), sy = std::sin(yaw);
    const float cp = std::cos(pitch), sp = std::sin(pitch);
    const float cr = std::cos(roll), sr = std::sin(roll);
    // R's columns: the images of the model's x, y and z axes.
    const float axes[3][3] = {
        {cy * cp, sy * cp, -sp},
        {cy * sp * sr - sy * cr, sy * sp * sr + cy * cr, cp * sr},
        {cy * sp * cr + sy * sr, sy * sp * cr - cy * sr, cp * cr},
    };
    float offset[3] = {0.0f, 0.0f, 0.0f};
    if (loaded && (useCenterForOrigin[0] || useCenterForOrigin[1] || useCenterForOrigin[2])) {
        float box[6];
        ActiveBounds(model, box);
        for (int i = 0; i < 3; ++i)
            offset[i] = useCenterForOrigin[i] ? -(box[i] + box[i + 3]) * 0.5f : 0.0f;
    }
    float m[16] = {};
    for (int row = 0; row < 3; ++row)
        for (int col = 0; col < 3; ++col)
            m[row * 4 + col] = axes[row][col] * scale;
    for (int col = 0; col < 3; ++col)
        m[12 + col] = position[col] + offset[0] * m[col] + offset[1] * m[4 + col] + offset[2] * m[8 + col];
    m[15] = 1.0f;
    reinterpret_cast<void(__fastcall *)(void *, void *, const float *)>(Offsets::FUN_M2MODEL_SET_WORLD_TRANSFORM)(
        model, nullptr, m);
}

// ---- XML templates --------------------------------------------------------------------

namespace {

using Xml::Attr;
using Xml::FirstChild;
using Xml::NextSibling;
using Xml::Tag;

bool FloatAttr(const void *node, const char *name, float &out) {
    const char *value = Attr(node, name);
    if (value == nullptr || *value == '\0')
        return false;
    out = static_cast<float>(std::atof(value));
    return true;
}

// The node's <Scripts>: each handler is the actor's own method (`method`),
// the global function `function` names, or the body compiled with its
// wrapper. `index` is the actor's table.
void LoadScripts(void *L, int index, Actor &actor, const void *node) {
    for (const void *scriptsNode = FirstChild(node); scriptsNode != nullptr; scriptsNode = NextSibling(scriptsNode)) {
        if (!Xml::TagIs(scriptsNode, "Scripts"))
            continue;
        for (const void *c = FirstChild(scriptsNode); c != nullptr; c = NextSibling(c)) {
            ScriptData data{nullptr};
            ScriptIx *slot = actor.GetScriptByName(Tag(c), &data);
            if (slot == nullptr)
                continue;
            int ref = -1;
            if (const char *method = Attr(c, "method"); method != nullptr && *method != '\0') {
                ref = Xml::Method::Ref(L, index, method, nullptr, actor.DisplayName(), Tag(c));
            } else if (const char *function = Attr(c, "function"); function != nullptr && *function != '\0') {
                Game::Lua::GetGlobal(L, function);
                ref = LuaRef(L);
            } else if (const char *body = Xml::Body(c); body != nullptr && *body != '\0') {
                const std::string name = std::string(actor.DisplayName()) + ":" + Tag(c);
                ref = reinterpret_cast<int(__cdecl *)(const char *, const char *, const char *, void *)>(
                    Offsets::FUN_FRAMESCRIPT_COMPILE_FUNCTION)(name.c_str(), data.wrapper, body, nullptr);
            }
            if (ref > 0)
                AssignScript(*slot, ref, LuaTainted());
        }
    }
}

// One node of the template chain, its inherits first. `index` is the actor's
// table; the chain's mixins are already on it.
void LoadNode(void *L, int index, Actor &actor, const void *node) {
    using Acquire_t = const void *(__cdecl *)(const char *, const char *&, bool &);
    using Release_t = void(__cdecl *)(const char *);
    Xml::ForEachName(Attr(node, "inherits"), [&](const char *name) {
        const char *taint = nullptr;
        bool wasLocked = false;
        const void *base = reinterpret_cast<Acquire_t>(Offsets::FUN_FRAMEXML_ACQUIRE_HASH_NODE)(name, taint, wasLocked);
        if (base == nullptr || wasLocked)
            return; // unknown, or a cycle
        LoadNode(L, index, actor, base);
        reinterpret_cast<Release_t>(Offsets::FUN_FRAMEXML_RELEASE_HASH_NODE)(name);
    });
    float value = 0.0f;
    if (FloatAttr(node, "alpha", value)) {
        actor.alpha = value < 0.0f ? 0.0f : (value > 1.0f ? 1.0f : value);
        actor.ApplyAlpha();
    }
    if (FloatAttr(node, "scale", value))
        actor.scale = value;
    if (const char *file = Attr(node, "file"); file != nullptr && *file != '\0')
        actor.SetModel(CreateM2(actor.SceneFrame(), file, 1), file, nullptr);
    Xml::KeyValues::Apply(L, index, node, nullptr, actor.DisplayName());
    LoadScripts(L, index, actor, node);
}

} // namespace

// The template's mixins first (Xml::Mixin), then its nodes, then OnLoad.
void Actor::LoadXml(const void *node) {
    void *L = Game::Lua::State();
    if (L == nullptr)
        return;
    const int top = Game::Lua::GetTop(L);
    UI::FrameObject::Push(L, this);
    Xml::Mixin::ApplyChain(L, top + 1, node, nullptr, DisplayName());
    LoadNode(L, top + 1, *this, node);
    Game::Lua::SetTop(L, top);
    RunScript(S_ONLOAD, 0);
}

// ---- the methods ------------------------------------------------------------------

namespace {

Actor *Self(void *L) { return static_cast<Actor *>(UI::FrameObject::ResolveSelf(L, ActorTypeId())); }

// HasScript / GetScript / SetScript / HookScript, as the engine's animation
// objects have them (CSimpleAnimGroup_Scripts.cpp 0x004A7480..0x004A7780).
ScriptIx *ScriptArg(void *L, Actor *actor, const char *usage) {
    if (!Game::Lua::IsString(L, 2)) {
        Game::Lua::Error(L, "Usage: %s:%s", actor->DisplayName(), usage);
        return nullptr;
    }
    ScriptIx *slot = actor->GetScriptByName(Game::Lua::ToString(L, 2), nullptr);
    if (slot == nullptr)
        Game::Lua::Error(L, "%s doesn't have a \"%s\" script", actor->DisplayName(), Game::Lua::ToString(L, 2));
    return slot;
}

int __cdecl Script_HasScript(void *L) {
    Actor *actor = Self(L);
    if (actor == nullptr)
        return 0;
    if (!Game::Lua::IsString(L, 2)) {
        Game::Lua::Error(L, "Usage: %s:HasScript(\"type\")", actor->DisplayName());
        return 0;
    }
    if (actor->GetScriptByName(Game::Lua::ToString(L, 2), nullptr) != nullptr)
        Game::Lua::PushNumber(L, 1.0);
    else
        Game::Lua::PushNil(L);
    return 1;
}

int __cdecl Script_GetScript(void *L) {
    Actor *actor = Self(L);
    ScriptIx *slot = actor != nullptr ? ScriptArg(L, actor, "GetScript(\"type\")") : nullptr;
    if (slot == nullptr)
        return 0;
    if (slot->luaRef > 0)
        Game::Lua::RawGetI(L, Game::Lua::REGISTRY_INDEX, slot->luaRef);
    else
        Game::Lua::PushNil(L);
    return 1;
}

int __cdecl Script_SetScript(void *L) {
    Actor *actor = Self(L);
    if (actor == nullptr)
        return 0;
    const int type = Game::Lua::Type(L, 3);
    if (!Game::Lua::IsString(L, 2) || (type != Game::Lua::TYPE_FUNCTION && type != Game::Lua::TYPE_NIL)) {
        Game::Lua::Error(L, "Usage: %s:SetScript(\"type\", function)", actor->DisplayName());
        return 0;
    }
    ScriptIx *slot = ScriptArg(L, actor, "SetScript(\"type\", function)");
    if (slot == nullptr)
        return 0;
    Game::Lua::SetTop(L, 3);
    AssignScript(*slot, LuaRef(L), LuaTainted());
    return 0;
}

int __cdecl Script_HookScript(void *L) {
    Actor *actor = Self(L);
    if (actor == nullptr)
        return 0;
    if (!Game::Lua::IsString(L, 2) || Game::Lua::Type(L, 3) != Game::Lua::TYPE_FUNCTION) {
        Game::Lua::Error(L, "Usage: %s:HookScript(\"type\", function)", actor->DisplayName());
        return 0;
    }
    ScriptIx *slot = ScriptArg(L, actor, "HookScript(\"type\", function)");
    if (slot == nullptr)
        return 0;
    Game::Lua::SetTop(L, 3);
    if (slot->luaRef == 0) {
        AssignScript(*slot, LuaRef(L), LuaTainted());
        return 0;
    }
    // The engine's chain: a closure of the old script and the hook.
    Game::Lua::Remove(L, 2);                                   // self, hook
    Game::Lua::RawGetI(L, Game::Lua::REGISTRY_INDEX, slot->luaRef); // self, hook, old
    Game::Lua::Insert(L, 2);                                   // self, old, hook
    Game::Lua::PushCClosure(L, reinterpret_cast<Game::Lua::CFunction>(Offsets::FUN_FRAMESCRIPT_HOOK_WRAPPER), 2);
    AssignScript(*slot, LuaRef(L), slot->taint);
    return 0;
}

const uint8_t *CreatureDisplayRecord(uint32_t id) {
    return DBC::Record(Offsets::VAR_CREATUREDISPLAYINFO_DBC_MIN_INDEX, Offsets::VAR_CREATUREDISPLAYINFO_DBC_MAX_INDEX,
                       Offsets::VAR_CREATUREDISPLAYINFO_DBC_INDEX_TABLE, static_cast<int>(id));
}

// A creature display as the world shows a creature: CreatureModelData's file,
// the display's geosets and skin, and its dress when it has a baked texture.
bool ShowCreatureDisplay(Actor &actor, uint32_t displayID) {
    actor.ClearModel();
    const uint8_t *display = CreatureDisplayRecord(displayID);
    if (display == nullptr)
        return false;
    const uint8_t *modelData =
        DBC::Record(Offsets::VAR_CREATUREMODELDATA_DBC_MIN_INDEX, Offsets::VAR_CREATUREMODELDATA_DBC_MAX_INDEX,
                    Offsets::VAR_CREATUREMODELDATA_DBC_INDEX_TABLE,
                    At<int32_t>(display, Offsets::OFF_CREATUREDISPLAYINFO_MODEL_ID));
    const char *path = modelData != nullptr ? At<const char *>(modelData, Offsets::OFF_CREATUREMODELDATA_MODEL_NAME) : nullptr;
    if (path == nullptr || *path == '\0')
        return false;
    void *model = CreateM2(actor.SceneFrame(), path, 0);
    if (model == nullptr)
        return false;
    reinterpret_cast<void(__cdecl *)(void *, const void *)>(Offsets::FUN_COMPONENT_APPLY_MONSTER_GEOSETS)(model, display);
    reinterpret_cast<void(__cdecl *)(void *, const void *, const void *)>(Offsets::FUN_COMPONENT_REPLACE_MONSTER_SKIN)(
        model, display, modelData);
    const uint8_t *extra =
        DBC::Record(Offsets::VAR_CREATUREDISPLAYINFOEXTRA_DBC_MIN_INDEX, Offsets::VAR_CREATUREDISPLAYINFOEXTRA_DBC_MAX_INDEX,
                    Offsets::VAR_CREATUREDISPLAYINFOEXTRA_DBC_INDEX_TABLE,
                    At<int32_t>(display, Offsets::OFF_CREATUREDISPLAYINFO_EXTRA_ID));
    const char *bake = extra != nullptr ? At<const char *>(extra, Offsets::OFF_CREATUREDISPLAYINFOEXTRA_BAKE_NAME) : nullptr;
    actor.displayScale = At<float>(display, Offsets::OFF_CREATUREDISPLAYINFO_SCALE);
    actor.SetModel(model, path, bake != nullptr && *bake != '\0' ? extra : nullptr);
    return true;
}

int __cdecl Script_SetModelByCreatureDisplayID(void *L) {
    Actor *actor = Self(L);
    if (actor == nullptr ||
        !NumberArgs(L, 2, 1, actor->DisplayName(), "SetModelByCreatureDisplayID(creatureDisplayID)"))
        return 0;
    Game::Lua::PushBool(L, ShowCreatureDisplay(*actor, static_cast<uint32_t>(Game::Lua::ToNumber(L, 2))));
    return 1;
}

int __cdecl Script_SetModelByPath(void *L) {
    Actor *actor = Self(L);
    if (actor == nullptr)
        return 0;
    if (!Game::Lua::IsString(L, 2)) {
        Game::Lua::Error(L, "Usage: %s:SetModelByPath(asset)", actor->DisplayName());
        return 0;
    }
    const char *path = Game::Lua::ToString(L, 2);
    actor->ClearModel();
    void *model = *path != '\0' ? CreateM2(actor->SceneFrame(), path, 1) : nullptr;
    actor->SetModel(model, path, nullptr);
    Game::Lua::PushBool(L, model != nullptr);
    return 1;
}

int __cdecl Script_ClearModel(void *L) {
    if (Actor *actor = Self(L))
        actor->ClearModel();
    return 0;
}

int __cdecl Script_IsLoaded(void *L) {
    Actor *actor = Self(L);
    if (actor == nullptr)
        return 0;
    Game::Lua::PushBool(L, actor->loaded);
    return 1;
}

int __cdecl Script_GetModelPath(void *L) {
    Actor *actor = Self(L);
    if (actor == nullptr)
        return 0;
    Game::Lua::PushString(L, actor->modelPath.c_str());
    return 1;
}

int __cdecl Script_SetPosition(void *L) {
    Actor *actor = Self(L);
    if (actor == nullptr || !NumberArgs(L, 2, 3, actor->DisplayName(), "SetPosition(x, y, z)"))
        return 0;
    for (int i = 0; i < 3; ++i)
        actor->position[i] = FloatArg(L, 2 + i);
    return 0;
}

int __cdecl Script_GetPosition(void *L) {
    Actor *actor = Self(L);
    if (actor == nullptr)
        return 0;
    for (float v : actor->position)
        Game::Lua::PushNumber(L, v);
    return 3;
}

constexpr char kUsageSetYaw[] = "SetYaw(yaw)";
constexpr char kUsageSetPitch[] = "SetPitch(pitch)";
constexpr char kUsageSetRoll[] = "SetRoll(roll)";
constexpr char kUsageSetScale[] = "SetScale(scale)";

template <float Actor::*Field, const char *Usage> int __cdecl SetFloat(void *L) {
    Actor *actor = Self(L);
    if (actor == nullptr || !NumberArgs(L, 2, 1, actor->DisplayName(), Usage))
        return 0;
    actor->*Field = FloatArg(L, 2);
    return 0;
}

template <float Actor::*Field> int __cdecl GetFloat(void *L) {
    Actor *actor = Self(L);
    if (actor == nullptr)
        return 0;
    Game::Lua::PushNumber(L, actor->*Field);
    return 1;
}

int __cdecl Script_SetAlpha(void *L) {
    Actor *actor = Self(L);
    if (actor == nullptr || !NumberArgs(L, 2, 1, actor->DisplayName(), "SetAlpha(alpha)"))
        return 0;
    const float a = FloatArg(L, 2);
    actor->alpha = a < 0.0f ? 0.0f : (a > 1.0f ? 1.0f : a);
    actor->ApplyAlpha();
    return 0;
}

int __cdecl Script_SetUseCenterForOrigin(void *L) {
    Actor *actor = Self(L);
    if (actor == nullptr)
        return 0;
    for (int i = 0; i < 3; ++i)
        actor->useCenterForOrigin[i] = Game::Lua::ToBoolean(L, 2 + i) != 0;
    return 0;
}

int __cdecl Script_IsUsingCenterForOrigin(void *L) {
    Actor *actor = Self(L);
    if (actor == nullptr)
        return 0;
    for (bool b : actor->useCenterForOrigin)
        Game::Lua::PushBool(L, b);
    return 3;
}

int __cdecl Script_Show(void *L) {
    if (Actor *actor = Self(L))
        actor->shown = true;
    return 0;
}

int __cdecl Script_Hide(void *L) {
    if (Actor *actor = Self(L))
        actor->shown = false;
    return 0;
}

int __cdecl Script_SetShown(void *L) {
    if (Actor *actor = Self(L))
        actor->shown = Game::Lua::ToBoolean(L, 2) != 0;
    return 0;
}

int __cdecl Script_IsShown(void *L) {
    Actor *actor = Self(L);
    if (actor == nullptr)
        return 0;
    Game::Lua::PushBool(L, actor->shown);
    return 1;
}

int __cdecl Script_IsVisible(void *L) {
    Actor *actor = Self(L);
    if (actor == nullptr)
        return 0;
    Game::Lua::PushBool(L, actor->shown && At<uint32_t>(actor->SceneFrame(), Offsets::OFF_FRAME_IS_VISIBLE) != 0);
    return 1;
}

int __cdecl Script_SetAnimation(void *L) {
    Actor *actor = Self(L);
    if (actor == nullptr || !NumberArgs(L, 2, 1, actor->DisplayName(),
                                        "SetAnimation(animation [, variation, animSpeed, animOffsetSeconds])"))
        return 0;
    actor->animation = static_cast<uint32_t>(Game::Lua::ToNumber(L, 2));
    actor->variation = Game::Lua::IsNumber(L, 3) ? static_cast<int>(Game::Lua::ToNumber(L, 3)) : -1;
    actor->animSpeed = Game::Lua::IsNumber(L, 4) ? FloatArg(L, 4) : 1.0f;
    actor->animOffsetSec = Game::Lua::IsNumber(L, 5) ? FloatArg(L, 5) : 0.0f;
    actor->PlayAnimation();
    return 0;
}

int __cdecl Script_GetAnimation(void *L) {
    Actor *actor = Self(L);
    if (actor == nullptr)
        return 0;
    Game::Lua::PushNumber(L, actor->animation);
    return 1;
}

// Retail pushes the variation unsigned: -1 reads 4294967295.
int __cdecl Script_GetAnimationVariation(void *L) {
    Actor *actor = Self(L);
    if (actor == nullptr)
        return 0;
    Game::Lua::PushNumber(L, static_cast<double>(static_cast<uint32_t>(actor->variation)));
    return 1;
}

// An optional boolean argument: `fallback` when absent or nil.
bool OptBool(void *L, int index, bool fallback) {
    return Game::Lua::Type(L, index) <= Game::Lua::TYPE_NIL ? fallback : Game::Lua::ToBoolean(L, index) != 0;
}

// SetModelByUnit(unit [, sheatheWeapons [, autoDress [, hideWeapons [,
// usePlayerNativeForm]]]]) -> success
int __cdecl Script_SetModelByUnit(void *L) {
    Actor *actor = Self(L);
    if (actor == nullptr)
        return 0;
    if (!Game::Lua::IsString(L, 2)) {
        Game::Lua::Error(L, "Usage: %s:SetModelByUnit(unit [, sheatheWeapons, autoDress, hideWeapons, usePlayerNativeForm])",
                         actor->DisplayName());
        return 0;
    }
    Game::Lua::PushBool(L, actor->SetModelByUnit(Game::Lua::ToString(L, 2), OptBool(L, 3, false), OptBool(L, 4, true),
                                                 OptBool(L, 5, false), OptBool(L, 6, true)));
    return 1;
}

// GetModelUnitGUID() -> the shown unit's GUID, or nil.
int __cdecl Script_GetModelUnitGUID(void *L) {
    Actor *actor = Self(L);
    if (actor == nullptr || (actor->unitGuid[0] == 0 && actor->unitGuid[1] == 0))
        return 0;
    char buf[Guid::STRING_SIZE];
    Game::Lua::PushString(L, Guid::Format({actor->unitGuid[0], actor->unitGuid[1]}, buf, sizeof buf));
    return 1;
}

// IsGeoReady() -> whether the model and its outfit are ready to show.
int __cdecl Script_IsGeoReady(void *L) {
    Actor *actor = Self(L);
    if (actor == nullptr)
        return 0;
    Game::Lua::PushBool(L, actor->loaded);
    return 1;
}

// TryOn(itemLinkOrID [, handSlotName [, spellEnchantmentID]]) -> reason. The
// item as DressUpModel's TryOn takes it (an ID, a link or `item:` string, a
// trade-skill `enchant:` link for the item its recipe makes, or the name of a
// cached item); the hand "MAINHANDSLOT" or "SECONDARYHANDSLOT"; the enchant
// whose glow a weapon shows, the link's by default.
int __cdecl Script_TryOn(void *L) {
    Actor *actor = Self(L);
    if (actor == nullptr)
        return 0;
    // Read first: resolving an item name can use the stack.
    int hand = -1;
    if (Game::Lua::Type(L, 3) == Game::Lua::TYPE_STRING) {
        const char *name = Game::Lua::ToString(L, 3);
        if (_stricmp(name, "MAINHANDSLOT") == 0)
            hand = 15;
        else if (_stricmp(name, "SECONDARYHANDSLOT") == 0)
            hand = 16;
    }
    uint32_t enchants[12] = {};
    const bool enchantGiven = Game::Lua::IsNumber(L, 4) != 0;
    if (enchantGiven)
        enchants[0] = static_cast<uint32_t>(Game::Lua::ToNumber(L, 4));
    int itemID = 0;
    const char *text = Game::Lua::Type(L, 2) == Game::Lua::TYPE_STRING ? Game::Lua::ToString(L, 2) : nullptr;
    if (const char *enchant = text != nullptr ? std::strstr(text, "enchant:") : nullptr) {
        // A trade-skill link: the item the recipe spell makes (Script_TryOn
        // 0x00598830: attribute 0x20, the first effect's item).
        uint8_t spell[Offsets::SPELL_DBC_RECORD_SIZE];
        const int spellID = std::atoi(enchant + 8);
        if (spellID > 0 &&
            reinterpret_cast<int(__thiscall *)(void *, int, void *)>(Offsets::FUN_DBC_COPY_RECORD)(
                reinterpret_cast<void *>(Offsets::VAR_SPELL_DBC_DESC), spellID, spell) != 0 &&
            (At<uint32_t>(spell, Offsets::OFF_SPELL_RECORD_ATTRIBUTES) & 0x20) != 0)
            itemID = At<int32_t>(spell, Offsets::OFF_SPELL_EFFECT_ITEM_TYPE);
    } else {
        const Item::Arg::Resolved item = Item::Arg::Resolve(L, 2);
        itemID = item.itemID;
        if (!enchantGiven)
            enchants[0] = static_cast<uint32_t>(item.enchant);
    }
    if (itemID <= 0) {
        reinterpret_cast<void(__cdecl *)(int, ...)>(Offsets::FUN_DISPLAY_ERROR)(Offsets::ERR_NOT_EQUIPPABLE);
        Game::Lua::PushNumber(L, Actor::TRYON_NOT_EQUIPPABLE);
        return 1;
    }
    Game::Lua::PushNumber(L, actor->TryOn(itemID, enchants, hand));
    return 1;
}

// Undress([includeWeapons]): the weapons too unless false.
int __cdecl Script_Undress(void *L) {
    if (Actor *actor = Self(L))
        actor->Undress(OptBool(L, 2, true));
    return 0;
}

int __cdecl Script_UndressSlot(void *L) {
    Actor *actor = Self(L);
    if (actor != nullptr && NumberArgs(L, 2, 1, actor->DisplayName(), "UndressSlot(inventorySlot)"))
        actor->UndressSlot(static_cast<int>(Game::Lua::ToNumber(L, 2)));
    return 0;
}

int __cdecl Script_DressPlayerSlot(void *L) {
    Actor *actor = Self(L);
    if (actor != nullptr && NumberArgs(L, 2, 1, actor->DisplayName(), "DressPlayerSlot(inventorySlot)"))
        actor->DressPlayerSlot(static_cast<int>(Game::Lua::ToNumber(L, 2)));
    return 0;
}

int __cdecl Script_Dress(void *L) {
    if (Actor *actor = Self(L))
        actor->Dress();
    return 0;
}

// SetSheathed(sheathed [, hidden])
int __cdecl Script_SetSheathed(void *L) {
    if (Actor *actor = Self(L))
        actor->SetSheathed(Game::Lua::ToBoolean(L, 2) != 0, OptBool(L, 3, false));
    return 0;
}

int __cdecl Script_GetSheathed(void *L) {
    Actor *actor = Self(L);
    if (actor == nullptr)
        return 0;
    Game::Lua::PushBool(L, actor->sheathed);
    return 1;
}

int __cdecl Script_SetAutoDress(void *L) {
    if (Actor *actor = Self(L))
        actor->autoDress = Game::Lua::ToBoolean(L, 2) != 0;
    return 0;
}

int __cdecl Script_GetAutoDress(void *L) {
    Actor *actor = Self(L);
    if (actor == nullptr)
        return 0;
    Game::Lua::PushBool(L, actor->autoDress);
    return 1;
}

// SetSpellVisualKit([spellVisualKitID [, oneShot]]): 0 or none removes the kit.
int __cdecl Script_SetSpellVisualKit(void *L) {
    Actor *actor = Self(L);
    if (actor == nullptr)
        return 0;
    const auto kitID = Game::Lua::IsNumber(L, 2) ? static_cast<uint32_t>(Game::Lua::ToNumber(L, 2)) : 0u;
    actor->SetSpellVisualKit(kitID, Game::Lua::ToBoolean(L, 3) != 0);
    return 0;
}

int __cdecl Script_GetSpellVisualKit(void *L) {
    Actor *actor = Self(L);
    if (actor == nullptr)
        return 0;
    Game::Lua::PushNumber(L, actor->spellVisualKit);
    return 1;
}

// This client has no animation kits (AnimKit data is a later client's):
// there is nothing to play or stop.
int __cdecl Script_PlayAnimationKit(void *L) {
    Actor *actor = Self(L);
    if (actor != nullptr)
        NumberArgs(L, 2, 1, actor->DisplayName(), "PlayAnimationKit(animationKit [, isLooping])");
    return 0;
}

int __cdecl Script_StopAnimationKit(void *L) {
    Self(L);
    return 0;
}

int __cdecl Script_SetAnimationBlendOperation(void *L) {
    Actor *actor = Self(L);
    if (actor == nullptr || !NumberArgs(L, 2, 1, actor->DisplayName(), "SetAnimationBlendOperation(blendOperation)"))
        return 0;
    actor->blendOperation = Game::Lua::ToNumber(L, 2) != 0.0 ? 1u : 0u;
    return 0;
}

int __cdecl Script_GetAnimationBlendOperation(void *L) {
    Actor *actor = Self(L);
    if (actor == nullptr)
        return 0;
    Game::Lua::PushNumber(L, actor->blendOperation);
    return 1;
}

// Six numbers, bottom x, y, z then top x, y, z (what the retail getters
// return and ModelSceneActorMixin reads); nothing until the model loads.
int PushBox(void *L, const float (&box)[6]) {
    for (float v : box)
        Game::Lua::PushNumber(L, v);
    return 6;
}

int __cdecl Script_GetActiveBoundingBox(void *L) {
    Actor *actor = Self(L);
    if (actor == nullptr || !actor->loaded)
        return 0;
    float box[6];
    ActiveBounds(actor->model, box);
    return PushBox(L, box);
}

int __cdecl Script_GetMaxBoundingBox(void *L) {
    Actor *actor = Self(L);
    if (actor == nullptr || !actor->loaded)
        return 0;
    float box[6];
    reinterpret_cast<void *(__fastcall *)(void *, void *, float *)>(Offsets::FUN_M2MODEL_GET_BOUNDING_BOX)(
        actor->model, nullptr, box);
    return PushBox(L, box);
}

int __cdecl Script_SetPaused(void *L) {
    Actor *actor = Self(L);
    if (actor == nullptr)
        return 0;
    actor->paused = Game::Lua::ToBoolean(L, 2) != 0;
    if (Game::Lua::Type(L, 3) <= Game::Lua::TYPE_NIL || Game::Lua::ToBoolean(L, 3) != 0)
        actor->globalPaused = actor->paused;
    actor->heldTimeMs = -1;
    return 0;
}

int __cdecl Script_GetPaused(void *L) {
    Actor *actor = Self(L);
    if (actor == nullptr)
        return 0;
    Game::Lua::PushBool(L, actor->paused);
    Game::Lua::PushBool(L, actor->globalPaused);
    return 2;
}

// ---- mounts ----------------------------------------------------------------------------

// The rider argument (stack index 2), an actor; else the usage error and null.
Actor *RiderArg(void *L, const Actor *mount, const char *usage) {
    void *object = nullptr;
    if (Game::Lua::Type(L, 2) == Game::Lua::TYPE_TABLE) {
        Game::Lua::RawGetI(L, 2, 0);
        object = Game::Lua::ToUserdata(L, -1);
        Game::Lua::SetTop(L, -2);
    }
    using IsA_t = bool(__fastcall *)(void *, void *, int);
    if (object != nullptr &&
        reinterpret_cast<IsA_t>(At<const void *const *>(object, 0)[Offsets::VTBL_OFF_SCRIPTOBJECT_IS_A / 4])(
            object, nullptr, ActorTypeId()))
        return static_cast<Actor *>(object);
    Game::Lua::Error(L, "Usage: %s:%s", mount->DisplayName(), usage);
    return nullptr;
}

int __cdecl Script_AttachToMount(void *L) {
    constexpr char kUsage[] = "AttachToMount(rider, animation [, spellKitVisualID])";
    Actor *mount = Self(L);
    if (mount == nullptr)
        return 0;
    Actor *rider = RiderArg(L, mount, kUsage);
    if (rider == nullptr || !NumberArgs(L, 3, 1, mount->DisplayName(), kUsage))
        return 0;
    const bool seated = mount->Seat(*rider, static_cast<uint32_t>(Game::Lua::ToNumber(L, 3)));
    if (seated && Game::Lua::IsNumber(L, 4) && Game::Lua::ToNumber(L, 4) != 0.0)
        rider->SetSpellVisualKit(static_cast<uint32_t>(Game::Lua::ToNumber(L, 4)), false);
    Game::Lua::PushBool(L, seated);
    return 1;
}

// Only takes the rider off: it keeps the seat animation.
int __cdecl Script_DetachFromMount(void *L) {
    Actor *mount = Self(L);
    Actor *rider = mount != nullptr ? RiderArg(L, mount, "DetachFromMount(rider)") : nullptr;
    if (rider == nullptr)
        return 0;
    const bool seated = mount->model != nullptr && rider->model != nullptr &&
                        At<const void *>(rider->model, Offsets::OFF_M2MODEL_ATTACH_PARENT) == mount->model;
    if (seated)
        reinterpret_cast<void(__fastcall *)(void *, void *)>(Offsets::FUN_M2MODEL_DETACH_FROM_PARENT)(rider->model,
                                                                                                    nullptr);
    Game::Lua::PushBool(L, seated);
    return 1;
}

// The world's mount scale (CreateUnitMount): the mount display's
// CreatureModelScale. The rider fits the mount at 1 over it.
int __cdecl Script_CalculateMountScale(void *L) {
    Actor *mount = Self(L);
    if (mount == nullptr || RiderArg(L, mount, "CalculateMountScale(rider)") == nullptr)
        return 0;
    Game::Lua::PushNumber(L, mount->displayScale);
    return 1;
}

struct Method {
    const char *name;
    Game::Lua::CFunction func;
};

const Method kMethods[] = {
    {"HasScript", &Script_HasScript},
    {"GetScript", &Script_GetScript},
    {"SetScript", &Script_SetScript},
    {"HookScript", &Script_HookScript},
    {"SetModelByCreatureDisplayID", &Script_SetModelByCreatureDisplayID},
    {"SetModelByPath", &Script_SetModelByPath},
    {"SetModelByUnit", &Script_SetModelByUnit},
    {"GetModelUnitGUID", &Script_GetModelUnitGUID},
    {"IsGeoReady", &Script_IsGeoReady},
    {"TryOn", &Script_TryOn},
    {"Undress", &Script_Undress},
    {"UndressSlot", &Script_UndressSlot},
    {"DressPlayerSlot", &Script_DressPlayerSlot},
    {"Dress", &Script_Dress},
    {"SetSheathed", &Script_SetSheathed},
    {"GetSheathed", &Script_GetSheathed},
    {"SetAutoDress", &Script_SetAutoDress},
    {"GetAutoDress", &Script_GetAutoDress},
    {"AttachToMount", &Script_AttachToMount},
    {"DetachFromMount", &Script_DetachFromMount},
    {"CalculateMountScale", &Script_CalculateMountScale},
    {"ClearModel", &Script_ClearModel},
    {"IsLoaded", &Script_IsLoaded},
    {"GetModelPath", &Script_GetModelPath},
    {"SetPosition", &Script_SetPosition},
    {"GetPosition", &Script_GetPosition},
    {"SetYaw", &SetFloat<&Actor::yaw, kUsageSetYaw>},
    {"GetYaw", &GetFloat<&Actor::yaw>},
    {"SetPitch", &SetFloat<&Actor::pitch, kUsageSetPitch>},
    {"GetPitch", &GetFloat<&Actor::pitch>},
    {"SetRoll", &SetFloat<&Actor::roll, kUsageSetRoll>},
    {"GetRoll", &GetFloat<&Actor::roll>},
    {"SetScale", &SetFloat<&Actor::scale, kUsageSetScale>},
    {"GetScale", &GetFloat<&Actor::scale>},
    {"SetAlpha", &Script_SetAlpha},
    {"GetAlpha", &GetFloat<&Actor::alpha>},
    {"SetUseCenterForOrigin", &Script_SetUseCenterForOrigin},
    {"IsUsingCenterForOrigin", &Script_IsUsingCenterForOrigin},
    {"Show", &Script_Show},
    {"Hide", &Script_Hide},
    {"SetShown", &Script_SetShown},
    {"IsShown", &Script_IsShown},
    {"IsVisible", &Script_IsVisible},
    {"SetAnimation", &Script_SetAnimation},
    {"GetAnimation", &Script_GetAnimation},
    {"GetAnimationVariation", &Script_GetAnimationVariation},
    {"GetActiveBoundingBox", &Script_GetActiveBoundingBox},
    {"GetMaxBoundingBox", &Script_GetMaxBoundingBox},
    {"SetAnimationBlendOperation", &Script_SetAnimationBlendOperation},
    {"GetAnimationBlendOperation", &Script_GetAnimationBlendOperation},
    {"SetPaused", &Script_SetPaused},
    {"GetPaused", &Script_GetPaused},
    {"SetSpellVisualKit", &Script_SetSpellVisualKit},
    {"GetSpellVisualKit", &Script_GetSpellVisualKit},
    {"PlayAnimationKit", &Script_PlayAnimationKit},
    {"StopAnimationKit", &Script_StopAnimationKit},
};

// CScriptObject's methods (GetObjectType, IsObjectType, GetName, GetParent),
// then the actor's.
void __cdecl RegisterScriptMethods(void *L) {
    reinterpret_cast<void(__cdecl *)(void *)>(Offsets::FUN_SCRIPTOBJECT_REGISTER_SCRIPT_METHODS)(L);
    reinterpret_cast<void(__cdecl *)(void *, const Method *, int)>(Offsets::FUN_REGISTER_FRAME_METHODS)(
        L, kMethods, static_cast<int>(sizeof(kMethods) / sizeof(kMethods[0])));
}

// Retail's Enum.ModelBlendOperation (ModelAnimationSharedDocumentation.lua).
const Game::Lua::EnumIntegerEntry kModelBlendOperation[] = {
    {"None", 0},
    {"Anim", 1},
};

// Retail's Enum.ItemTryOnReason (UIModelInfoSharedDocumentation.lua).
const Game::Lua::EnumIntegerEntry kItemTryOnReason[] = {
    {"Success", Actor::TRYON_SUCCESS},
    {"WrongRace", Actor::TRYON_WRONG_RACE},
    {"NotEquippable", Actor::TRYON_NOT_EQUIPPABLE},
    {"DataPending", Actor::TRYON_DATA_PENDING},
};

} // namespace

void CreateActorMetaTable() {
    using CreateMeta_t = int(__cdecl *)(void *, void(__cdecl *)(void *));
    g_metaTable = reinterpret_cast<CreateMeta_t>(Offsets::FUN_FRAMESCRIPT_CREATE_SCRIPT_META_TABLE)(
        Game::Lua::State(), &RegisterScriptMethods);
    Game::Lua::RegisterIntegerEnum("Enum", "ModelBlendOperation", kModelBlendOperation,
                                   static_cast<int>(sizeof(kModelBlendOperation) / sizeof(kModelBlendOperation[0])));
    Game::Lua::RegisterIntegerEnum("Enum", "ItemTryOnReason", kItemTryOnReason,
                                   static_cast<int>(sizeof(kItemTryOnReason) / sizeof(kItemTryOnReason[0])));
}

void ReleaseActorMetaTable() { g_metaTable = 0; }

} // namespace Model::Scene
