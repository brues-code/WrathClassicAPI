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

#pragma once

// The ModelScene frame (model/Scene.cpp) and its actors (model/SceneActor.cpp).

#include "Game.h"

#include <cstdint>
#include <string>
#include <vector>

namespace Model::Scene {

struct Vec3 {
    float x, y, z;
};

// FrameScript_Object::ScriptIx: a script's registry ref (0 for none) and taint.
struct ScriptIx {
    int32_t luaRef = 0;
    const char *taint = nullptr;
};

// FrameScript_Object::ScriptData: the wrapper GetScriptByName hands back for
// an XML script body.
struct ScriptData {
    const char *wrapper;
};

// A ModelSceneActor: one CM2Model in its scene frame's M2 scene, placed by the
// scene's paint.
//
// It is an engine script object. The engine knows a CScriptObject by its
// vtable (FrameScript_Object, then CScriptObject: the eight virtuals below, in
// that order, all __thiscall, which MSVC's own virtuals are) and the header
// that follows it; RegisterScriptObject builds its Lua table from them, and
// the engine's CScriptObject methods (GetObjectType, GetName, GetParent, ...)
// and RunScript work on it unchanged. The two IsA overloads have their own
// names here: MSVC orders overloaded virtuals its own way.
//
// No member may need 8-byte alignment (int64_t, double): MSVC then pads the
// vtable pointer to 8 bytes and the header moves to +0x08, where the engine
// does not look for it. `alignof(Actor) == 4` below holds that.
class Actor {
  public:
    // FrameScript_Object, then CScriptObject (Engine/Source/Frame/CScriptObject.h).
    virtual ~Actor();                                                // +0x00
    virtual const char *GetName();                                   // +0x04
    virtual int GetScriptMetaTable();                                // +0x08
    virtual ScriptIx *GetScriptByName(const char *name, ScriptData *data); // +0x0C
    virtual bool IsAType(int type);                                  // +0x10
    virtual void *GetScriptObjectParent();                           // +0x14
    virtual bool IsATypeName(const char *typeName);                  // +0x18
    virtual const char *GetObjectTypeName();                         // +0x1C

    // The engine's header after the vtable: m_luaRegistered, m_luaObjectRef,
    // m_onEvent, and CScriptObject's RCString m_name (unused: GetName is ours).
    struct Header {
        int32_t luaRegistered = 0;
        int32_t luaObjectRef = -2; // LUA_NOREF
        ScriptIx onEvent;
        uint32_t engineName[3] = {};
    } header;

    Actor(void *sceneFrame, const char *name);
    Actor(const Actor &) = delete;
    Actor &operator=(const Actor &) = delete;

    void *SceneFrame() const { return frame_; }
    const char *DisplayName() const { return name_.empty() ? "<unnamed>" : name_.c_str(); }

    // From the scene frame's update, before the scene clock advances: puts a
    // shown model on the scene's animate and draw lists, as a Model frame does
    // with its one model (CSimpleModel::OnLayerUpdate).
    void Link();
    // From the scene frame's update, after the clock advanced: load and dress
    // state, then the scripts.
    void Update(float elapsedSec);
    // From the scene's paint, before the scene animates: the placement.
    void Place();
    // The model's alpha: the actor's times the scene frame's.
    void ApplyAlpha();

    // The unit a model shows (SetModelByUnit), and how.
    struct UnitSource {
        void *unit;
        bool dressable; // the player's native model, dressed by the actor
        bool autoDress, sheatheWeapons, hideWeapons;
    };
    // Shows `model` (one reference, which the actor takes over); null clears.
    // `dressRecord` is a creature display's CreatureDisplayInfoExtra row with
    // a baked NPC texture, dressed once the model loads; null for none.
    // `unit` is the unit the model shows, set up before OnModelLoading runs.
    void SetModel(void *model, const char *path, const uint8_t *dressRecord, const UnitSource *unit = nullptr);
    // SetModel's unit step (model/SceneOutfit.cpp).
    void ShowUnit(const UnitSource &unit);
    void ClearModel();
    void PlayAnimation();
    // Loads an <Actor> template (its inherits chain first), then runs OnLoad.
    void LoadXml(const void *node);
    // Runs script `index` with `nargs` values on top of the stack, and pops them.
    void RunScript(int index, int nargs);

    enum Script { S_ONLOAD, S_ONUPDATE, S_ONMODELCLEARED, S_ONMODELLOADING, S_ONMODELLOADED,
                  S_ONANIMFINISHED, S_COUNT };
    ScriptIx scripts[S_COUNT];

    void *model = nullptr;
    std::string modelPath;
    // A creature display with a baked NPC texture (CreatureDisplayInfoExtra):
    // its row until the model loads and the dress is built, then the dress.
    const uint8_t *pendingDress = nullptr;
    void *component = nullptr;
    bool componentReady = false;
    bool loaded = false; // OnModelLoaded has run for the model
    bool animFinished = false;

    float position[3] = {0.0f, 0.0f, 0.0f};
    float yaw = 0.0f, pitch = 0.0f, roll = 0.0f;
    float scale = 1.0f;
    float alpha = 1.0f;
    bool shown = true;
    bool useCenterForOrigin[3] = {false, false, false};
    uint32_t animation = 0;
    int variation = -1;
    float animSpeed = 1.0f;
    float animOffsetSec = 0.0f;
    uint32_t blendOperation = 1; // Enum.ModelBlendOperation: 0 None, 1 Anim
    bool paused = false;
    bool globalPaused = false;
    int32_t heldTimeMs = -1; // the paused frame, once read from a loaded model

    // The spell visual kit shown (model/SceneKit.cpp), 0 for none. It is
    // applied once the model has loaded, and again on each new model.
    void SetSpellVisualKit(uint32_t kitID, bool oneShot);
    // Puts the kit on the loaded model.
    void ApplyKit();
    // Takes the kit's effects off the model and undoes its tint and alpha.
    void DropKit();
    // Frees the stopped one-shot effects whose particles are gone.
    void DrainKit();
    uint32_t spellVisualKit = 0;
    bool kitOneShot = false;
    float kitAlpha = 1.0f; // the kit's alpha procedure, times the actor's alpha
    struct KitEffect {
        void *model; // one reference of ours
        bool stopped;
    };
    std::vector<KitEffect> kitEffects;

    // A unit's model (model/SceneOutfit.cpp). Shows the unit `token` names; a
    // player in its native form gets a dressable model, as DressUpModel shows
    // one, and any other unit a copy of its world model, as PlayerModel does.
    bool SetModelByUnit(const char *token, bool sheatheWeapons, bool autoDress, bool hideWeapons,
                        bool usePlayerNativeForm);
    // SetModelByUnit for the unit object `unit`.
    bool ShowUnitObject(void *unit, bool sheatheWeapons, bool autoDress, bool hideWeapons, bool usePlayerNativeForm);
    // Retail's Enum.ItemTryOnReason.
    enum TryOnReason { TRYON_SUCCESS = 0, TRYON_WRONG_RACE = 1, TRYON_NOT_EQUIPPABLE = 2, TRYON_DATA_PENDING = 3 };
    // Shows an item on a dressable model (DressUpModel's TryOn); a TryOnReason.
    // `enchants` is an item link's 12 enchant IDs, or null; `hand` is 15, 16
    // or -1 for the engine's choice.
    int TryOn(int itemID, const uint32_t *enchants, int hand);
    // The dressable model's outfit, by inventory slot (1 head .. 19 tabard).
    void Undress(bool includeWeapons);
    void UndressSlot(int inventorySlot);
    // The slot as the unit shows it.
    void DressPlayerSlot(int inventorySlot);
    // The unit's model again, dressed in what the unit shows.
    void Dress();
    // The weapons at their sheath points, or left off.
    void SetSheathed(bool sheathe, bool hidden);
    bool autoDress = true;           // dress a unit's model when it is built
    bool usePlayerNativeForm = true; // as SetModelByUnit last asked
    bool weaponsHidden = false;
    int mainHandVisual = 0, offHandVisual = 0; // the hand items' enchant glows
    // An 8-byte CGUnitVirtualItem: what a hand holds.
    struct HandLane {
        uint8_t classID, subclassID, soundOverride, material, inventoryType, sheatheType, pad[2];
    };
    uint32_t unitGuid[2] = {0, 0}; // the unit shown; two dwords for the header's alignment
    bool dressable = false;        // a component and hand lanes of our own
    HandLane mainHand = {}, offHand = {};
    int nextHand = 15; // DressUpModel's s_nextHand, s_mainHandItemId, s_offHandItemId
    int mainHandItemID = 0, offHandItemID = 0;
    bool sheathed = false;
    uint32_t serial; // names the actor to the item cache's callback

  private:
    void *frame_;
    std::string name_;
    void ReleaseModel();
};
static_assert(alignof(Actor) == 4, "the engine finds the script-object header at +0x04");

// The live actors by serial (model/SceneOutfit.cpp), for engine callbacks
// that can outlive an actor: a serial names no actor once its actor is gone.
uint32_t AddLiveActor(Actor *actor);
void RemoveLiveActor(uint32_t serial);
Actor *LiveActor(uint32_t serial);

// The scene frame's M2 scene (CSimpleModel::GetScene).
void *M2SceneOf(void *sceneFrame);
// The scene frame's alpha, 0..1.
float FrameAlpha(void *sceneFrame);

// The actor class's type id and metatable ref, for the actor's virtuals.
int ActorTypeId();
int ActorMetaTable();
// Builds the actor metatable in the current Lua state (once per state).
void CreateActorMetaTable();
void ReleaseActorMetaTable();

// The script object at stack index 1 when it is of `typeId` (its IsA), else
// the engine's errors, as FrameScript_GetObjectThis raises them, and null.
void *ResolveSelf(void *L, int typeId);

// True when the `count` arguments from `first` are numbers; else the
// engine's usage error, "Usage: <owner>:<usage>".
bool NumberArgs(void *L, int first, int count, const char *owner, const char *usage);
float FloatArg(void *L, int index);

} // namespace Model::Scene
