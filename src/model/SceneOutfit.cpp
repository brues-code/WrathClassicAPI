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

// A unit on a ModelSceneActor (actor:SetModelByUnit), and its outfit.
//
// The engine shows a unit in a frame two ways, and the actor takes both from
// CGCharacterModelBase::SetUpModel (0x005975E0): ReadyModelForUI, the model
// for the unit, WaitForLoad, RemoveVisuals, the model set, then the hand
// grips closed around what the hands hold.
//   - A player in its native form gets DressUpModel's model: a new model of
//     the native display (0x00597E10) with a character component of its own,
//     built from the player's customisation (CGDressUpModelFrame::SetModel
//     0x005989E0), dressed in what the player shows (each visible item
//     through TryOnWithVisual, a hidden helm or cloak left off, the ranged
//     slot left out), the hand lanes then taken from the unit's virtual
//     items. TryOn, Undress and the rest work on this model.
//   - Any other unit, or a player in another form when the native form is
//     not asked for, gets PlayerModel's copy of its world model, gear and
//     all (CM2Scene::DuplicateModel). It is not dressable.
//
// TryOn / TryOnWithVisual are DressUpModel's (0x00597FC0 / 0x005980D0), with
// the actor holding what the frame and the file's statics hold. An item the
// cache does not have yet is tried on when it arrives, as the frame's
// DressUpItemCallback does; the callback names the actor by serial, so an
// actor gone by then is skipped. One change: an item that evicts the other
// hand's (a two-hander after an off-hand) also clears that hand's lane, which
// the engine leaves set.

#include "model/Scene.h"

#include "Game.h"
#include "Guid.h"
#include "Offsets.h"
#include "dbc/Store.h"
#include "unit/Resolve.h"

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <unordered_map>

namespace Model::Scene {

namespace {

template <typename T> T &At(void *p, uintptr_t off) { return *reinterpret_cast<T *>(static_cast<uint8_t *>(p) + off); }
template <typename T> T At(const void *p, uintptr_t off) {
    return *reinterpret_cast<const T *>(static_cast<const uint8_t *>(p) + off);
}
template <typename T> T Global(uintptr_t address) { return *reinterpret_cast<const T *>(address); }

std::unordered_map<uint32_t, Actor *> g_live;
uint32_t g_nextSerial = 0;

// ---- engine calls ------------------------------------------------------------------

template <typename Fn> Fn Engine(uintptr_t address) { return reinterpret_cast<Fn>(address); }

const uint8_t *ItemStats(int itemID, void *callback, uint32_t param) {
    using Get_t = const uint8_t *(__thiscall *)(void *, uint32_t, const uint64_t *, void *, void *, int);
    const uint64_t none = 0;
    return Engine<Get_t>(Offsets::FUN_DBCACHE_ITEMSTATS_GET_RECORD)(
        reinterpret_cast<void *>(Offsets::VAR_ITEMDB_CACHE), static_cast<uint32_t>(itemID), &none, callback,
        reinterpret_cast<void *>(static_cast<uintptr_t>(param)), 0);
}

bool DisplayRecord(int displayID, uint8_t (&out)[Offsets::ITEMDISPLAYINFO_RECORD_SIZE]) {
    using Get_t = int(__thiscall *)(void *, int, void *);
    return Engine<Get_t>(Offsets::FUN_ITEMDISPLAYINFO_GET_RECORD)(
               reinterpret_cast<void *>(Offsets::VAR_ITEMDISPLAYINFO_DB), displayID, out) != 0;
}

bool SpellRecord(int spellID, uint8_t (&out)[Offsets::SPELL_DBC_RECORD_SIZE]) {
    using Get_t = int(__thiscall *)(void *, int, void *);
    return Engine<Get_t>(Offsets::FUN_DBC_COPY_RECORD)(reinterpret_cast<void *>(Offsets::VAR_SPELL_DBC_DESC),
                                                       spellID, out) != 0;
}

bool CanGoInSlot(int itemID, uint32_t slot) {
    return Engine<bool(__cdecl *)(int, uint32_t, int)>(Offsets::FUN_ITEM_CAN_GO_IN_SLOT)(itemID, slot, 0);
}

void DisplayError(int code) { Engine<void(__cdecl *)(int, ...)>(Offsets::FUN_DISPLAY_ERROR)(code); }

void RemoveItem(void *component, int slot) {
    Engine<void(__thiscall *)(void *, int)>(Offsets::FUN_COMPONENT_REMOVE_ITEM)(component, slot);
}

void AddItem(void *component, int slot, int displayID, int visual) {
    Engine<void(__thiscall *)(void *, int, int, int)>(Offsets::FUN_COMPONENT_ADD_ITEM)(component, slot, displayID,
                                                                                       visual);
}

void RemoveHandItem(void *model, int hand, const Actor::HandLane &lane) {
    Engine<void(__cdecl *)(void *, int, int, bool)>(Offsets::FUN_COMPONENT_REMOVE_HAND_ITEM)(
        model, hand, lane.sheatheType, lane.inventoryType == 14);
}

// The player object for the actor's unit, or null for any other unit.
void *PlayerOf(const Actor &actor) {
    return Guid::ResolveObject({actor.unitGuid[0], actor.unitGuid[1]}, Offsets::OBJ_FLAGS_PLAYER);
}

bool IsActivePlayer(const Actor &actor) {
    const uint64_t active = Engine<uint64_t(__cdecl *)()>(Offsets::FUN_LOCAL_PLAYER_GUID)();
    return active != 0 && active == ((static_cast<uint64_t>(actor.unitGuid[1]) << 32) | actor.unitGuid[0]);
}

bool CanDualWield() { return Global<int32_t>(Offsets::VAR_DUAL_WIELD_GRANT_SPELL_ID) != 0; }

// HandItemsCompatible (0x00597C50): whether the two hands' items go together.
bool HandItemsCompatible(const Actor &actor, int mainType, int offType, bool dualWield) {
    if (mainType != 13 && mainType != 21)
        return mainType == 17 && offType != 15 && CanGoInSlot(actor.mainHandItemID, 16);
    if (offType == 14 || offType == 23)
        return true;
    if (offType != 13 && offType != 22)
        return (offType == 21 || offType == 17) && CanGoInSlot(actor.offHandItemID, 16);
    return dualWield;
}

// The component's armor slot for an inventory type; -1 for a hand item, -2
// for one that shows nothing (neck, rings, trinkets, bags), -3 for one that is
// not equippable.
int ArmorSlot(int inventoryType) {
    switch (inventoryType) {
    case 1: return 0;
    case 3: return 1;
    case 4: return 2;
    case 5:
    case 20: return 3;
    case 6: return 4;
    case 7: return 5;
    case 8: return 6;
    case 9: return 7;
    case 10: return 8;
    case 19: return Offsets::COMPONENT_ITEM_SLOT_TABARD;
    case 16: return 10;
    case 2:
    case 11:
    case 12:
    case 28: return -2;
    case 13:
    case 14:
    case 15:
    case 17:
    case 21:
    case 22:
    case 23:
    case 25:
    case 26: return -1;
    default: return -3;
    }
}

void __cdecl OnItemLoaded(uint32_t itemID, const uint64_t *, void *param, int success) {
    if (success == 0)
        return;
    if (Actor *actor = LiveActor(static_cast<uint32_t>(reinterpret_cast<uintptr_t>(param))))
        actor->TryOn(static_cast<int>(itemID), nullptr, -1);
}

// A recipe on an item that shows nothing: the item its learned spell makes.
int RecipeProduct(const uint8_t *stats) {
    uint8_t spell[Offsets::SPELL_DBC_RECORD_SIZE];
    uint8_t learned[Offsets::SPELL_DBC_RECORD_SIZE];
    for (int i = 0; i < 5; ++i) {
        const int spellID = At<int32_t>(stats, Offsets::OFF_ITEMSTATS_SPELL_ID + i * 4);
        if (spellID == 0 || !SpellRecord(spellID, spell) ||
            At<int32_t>(spell, Offsets::OFF_SPELL_EFFECT) != Offsets::SPELL_EFFECT_LEARN_SPELL)
            continue;
        int learnedID = At<int32_t>(spell, Offsets::OFF_SPELL_EFFECT_TRIGGER_SPELL);
        if (learnedID == 0)
            for (int j = 0; j < 5; ++j)
                if (At<int32_t>(stats, Offsets::OFF_ITEMSTATS_SPELL_TRIGGER + j * 4) == 6) {
                    learnedID = At<int32_t>(stats, Offsets::OFF_ITEMSTATS_SPELL_ID + j * 4);
                    break;
                }
        if (SpellRecord(learnedID, learned) &&
            At<int32_t>(learned, Offsets::OFF_SPELL_EFFECT) == Offsets::SPELL_EFFECT_CREATE_ITEM)
            return At<int32_t>(learned, Offsets::OFF_SPELL_EFFECT_ITEM_TYPE);
    }
    return 0;
}

// A tabard that takes its wearer's guild colours (ItemDisplayInfo flag 1).
void ApplyGuildColors(const Actor &actor, const uint8_t (&display)[Offsets::ITEMDISPLAYINFO_RECORD_SIZE]) {
    if ((At<uint32_t>(display, Offsets::OFF_ITEMDISPLAYINFO_FLAGS) & 1) == 0)
        return;
    void *player = PlayerOf(actor);
    if (player == nullptr)
        return;
    const uint8_t *fields = At<const uint8_t *>(player, Offsets::OFF_CGPLAYER_INFO);
    int emblem = 0, emblemColor = 0, border = 0, borderColor = 0, background = 0;
    using Get_t = bool(__cdecl *)(uint32_t, uint32_t, int, void *, void *, int *, int *, int *, int *, int *);
    if (Engine<Get_t>(Offsets::FUN_GUILD_GET_TABARD)(actor.unitGuid[0], actor.unitGuid[1],
                                                    At<int32_t>(fields, Offsets::OFF_PLAYER_FIELD_GUILD_ID), nullptr,
                                                    nullptr, &emblem, &emblemColor, &border, &borderColor,
                                                    &background))
        Engine<void(__thiscall *)(void *, int, int, int, int, int)>(Offsets::FUN_COMPONENT_APPLY_GUILD_COLOR)(
            actor.component, emblem, emblemColor, border, borderColor, background);
}

// TryOnWithVisual's hand branch: the hand an item goes in, its lane updated
// and the other hand's item taken off when the two do not go together.
int TryOnHand(Actor &actor, int itemID, const uint8_t *stats, int visual, int hand) {
    const int type = At<int32_t>(stats, Offsets::OFF_ITEMSTATS_INVENTORY_TYPE);
    const bool dualWield = IsActivePlayer(actor) && CanDualWield();
    auto alternate = [&actor] {
        const int h = actor.nextHand;
        actor.nextHand = actor.nextHand == 15 ? 16 : 15;
        return h;
    };
    if (hand == -1) {
        switch (type) {
        case 13: {
            const int mainType = actor.mainHand.inventoryType;
            if (actor.mainHand.classID == 0 || (mainType != 13 && mainType != 21 && mainType != 17) || !dualWield)
                hand = 15;
            else
                hand = alternate();
            break;
        }
        case 17:
        case 21:
            if (!CanGoInSlot(itemID, 16)) {
                hand = 15;
                actor.nextHand = 15;
            } else {
                hand = alternate();
            }
            break;
        case 25:
        case 26:
            hand = 15;
            actor.nextHand = 15;
            break;
        case 14:
        case 15:
        case 22:
        case 23:
            hand = 16;
            actor.nextHand = 15;
            break;
        default:
            return Actor::TRYON_NOT_EQUIPPABLE;
        }
    }
    Actor::HandLane *lane;
    if (hand == 15) {
        actor.mainHandItemID = itemID;
        actor.mainHandVisual = visual;
        if (actor.offHand.classID != 0 && !HandItemsCompatible(actor, type, actor.offHand.inventoryType, dualWield)) {
            RemoveHandItem(actor.model, 16, actor.offHand);
            actor.offHand = Actor::HandLane{};
            actor.offHandItemID = actor.offHandVisual = 0;
        }
        lane = &actor.mainHand;
    } else {
        actor.offHandItemID = itemID;
        actor.offHandVisual = visual;
        if (actor.mainHand.classID != 0 && !HandItemsCompatible(actor, actor.mainHand.inventoryType, type, dualWield)) {
            RemoveHandItem(actor.model, 15, actor.mainHand);
            actor.mainHand = Actor::HandLane{};
            actor.mainHandItemID = actor.mainHandVisual = 0;
        }
        lane = &actor.offHand;
    }
    if (lane->classID != 0)
        RemoveHandItem(actor.model, hand, *lane);
    *lane = Actor::HandLane{};
    lane->classID = static_cast<uint8_t>(At<int32_t>(stats, Offsets::OFF_ITEMSTATS_CLASS));
    lane->subclassID = static_cast<uint8_t>(At<int32_t>(stats, Offsets::OFF_ITEMSTATS_SUBCLASS));
    lane->inventoryType = static_cast<uint8_t>(type);
    lane->sheatheType = static_cast<uint8_t>(At<int32_t>(stats, Offsets::OFF_ITEMSTATS_SHEATHE_TYPE));
    uint8_t display[Offsets::ITEMDISPLAYINFO_RECORD_SIZE];
    if (DisplayRecord(At<int32_t>(stats, Offsets::OFF_ITEMSTATS_DISPLAY_INFO_ID), display))
        Engine<int(__cdecl *)(void *, const void *, int, int, bool, bool, bool, int)>(
            Offsets::FUN_COMPONENT_ADD_HAND_ITEM)(actor.model, display, hand, lane->sheatheType, actor.sheathed,
                                                  type == 14, type == 26, visual);
    actor.weaponsHidden = false;
    return Actor::TRYON_SUCCESS;
}

// TryOnWithVisual (0x005980D0), with retail's TryOn reason: data pending
// while the item cache fetches the item (it is tried on when it arrives), not
// equippable for an item the model cannot show.
int TryOnWithVisual(Actor &actor, int itemID, int visual, int hand) {
    if (!actor.dressable || actor.model == nullptr)
        return Actor::TRYON_NOT_EQUIPPABLE;
    const uint8_t *stats = ItemStats(itemID, reinterpret_cast<void *>(&OnItemLoaded), actor.serial);
    if (stats == nullptr)
        return Actor::TRYON_DATA_PENDING;
    if (At<int32_t>(stats, Offsets::OFF_ITEMSTATS_DISPLAY_INFO_ID) == 0)
        return Actor::TRYON_NOT_EQUIPPABLE;
    if (visual < 0)
        visual = 0;
    const int type = At<int32_t>(stats, Offsets::OFF_ITEMSTATS_INVENTORY_TYPE);
    if (type == 0) {
        if (const int product = RecipeProduct(stats))
            return actor.TryOn(product, nullptr, -1);
    }
    const int slot = ArmorSlot(type);
    if (slot == -2)
        return Actor::TRYON_NOT_EQUIPPABLE;
    if (slot == -3) {
        DisplayError(Offsets::ERR_NOT_EQUIPPABLE);
        return Actor::TRYON_NOT_EQUIPPABLE;
    }
    if (slot == -1)
        return TryOnHand(actor, itemID, stats, visual, hand);
    if (actor.component == nullptr)
        return Actor::TRYON_NOT_EQUIPPABLE;
    const int displayID = At<int32_t>(stats, Offsets::OFF_ITEMSTATS_DISPLAY_INFO_ID);
    RemoveItem(actor.component, slot);
    AddItem(actor.component, slot, displayID, visual);
    if (slot == Offsets::COMPONENT_ITEM_SLOT_TABARD) {
        uint8_t display[Offsets::ITEMDISPLAYINFO_RECORD_SIZE];
        if (DisplayRecord(displayID, display))
            ApplyGuildColors(actor, display);
    }
    return Actor::TRYON_SUCCESS;
}

// CGDressUpModelFrame::SetModel's player branch: the component, then the
// player's visible items, then the hand lanes from its virtual items.
void DressAsPlayer(Actor &actor, void *player, const Actor::UnitSource &source) {
    const uint8_t *unitFields = At<const uint8_t *>(player, Offsets::OFF_UNIT_DESCRIPTOR);
    const uint8_t *playerFields = At<const uint8_t *>(player, Offsets::OFF_CGPLAYER_INFO);
    alignas(8) uint8_t data[Offsets::COMPONENT_DATA_SIZE];
    std::memset(data, 0, sizeof data); // the constructor leaves most of the flags word alone
    Engine<void(__thiscall *)(void *)>(Offsets::FUN_COMPONENT_DATA_CONSTRUCTOR)(data);
    At<uint32_t>(data, Offsets::OFF_COMPONENT_PREF_RACE) = unitFields[Offsets::OFF_UNIT_FIELD_RACE];
    At<uint32_t>(data, Offsets::OFF_COMPONENT_PREF_SEX) = playerFields[Offsets::OFF_PLAYER_FIELD_SEX];
    At<uint32_t>(data, Offsets::OFF_COMPONENT_PREF_CLASS) = unitFields[Offsets::OFF_UNIT_FIELD_CLASS];
    At<uint32_t>(data, Offsets::OFF_COMPONENT_PREF_SKIN) = playerFields[Offsets::OFF_PLAYER_FIELD_SKIN];
    At<uint32_t>(data, Offsets::OFF_COMPONENT_PREF_FACE) = playerFields[Offsets::OFF_PLAYER_FIELD_FACE];
    At<uint32_t>(data, Offsets::OFF_COMPONENT_PREF_HAIR_STYLE) = playerFields[Offsets::OFF_PLAYER_FIELD_HAIR_STYLE];
    At<uint32_t>(data, Offsets::OFF_COMPONENT_PREF_HAIR_COLOR) = playerFields[Offsets::OFF_PLAYER_FIELD_HAIR_COLOR];
    At<uint32_t>(data, Offsets::OFF_COMPONENT_PREF_FACIAL_HAIR) = playerFields[Offsets::OFF_PLAYER_FIELD_FACIAL_HAIR];
    At<void *>(data, Offsets::OFF_COMPONENT_DATA_MODEL) = actor.model;
    if (IsActivePlayer(actor))
        At<uint32_t>(data, Offsets::OFF_COMPONENT_DATA_FLAGS) |= Offsets::COMPONENT_DATA_ACTIVE_PLAYER;
    Engine<void(__cdecl *)(void *, int)>(Offsets::FUN_COMPONENT_VALIDATE_DATA)(data, Offsets::COMPONENT_CONTEXT_DRESS_UP);
    void *component = Engine<void *(__cdecl *)()>(Offsets::FUN_COMPONENT_ALLOC)();
    if (component == nullptr)
        return;
    ++At<uint32_t>(actor.model, Offsets::OFF_M2MODEL_REFCOUNT); // the component's reference
    Engine<bool(__thiscall *)(void *, void *, const char *)>(Offsets::FUN_COMPONENT_INIT)(component, data, nullptr);
    actor.component = component;
    actor.dressable = true;
    actor.sheathed = source.sheatheWeapons;
    if (!source.autoDress)
        return;
    const uint32_t flags = At<uint32_t>(playerFields, Offsets::OFF_PLAYER_FIELD_FLAGS);
    using GetVisible_t = const uint8_t *(__thiscall *)(void *, uint32_t);
    for (uint32_t slot = 0; slot <= 18; ++slot) {
        const bool handSlot = slot == 15 || slot == 16;
        if (slot == 17 || (slot == 0 && (flags & Offsets::PLAYER_FLAG_HIDE_HELM)) ||
            (slot == 14 && (flags & Offsets::PLAYER_FLAG_HIDE_CLOAK)))
            continue;
        const uint8_t *item = Engine<GetVisible_t>(Offsets::FUN_PLAYER_GET_VISIBLE_ITEM)(player, slot);
        const int itemID = item != nullptr ? std::abs(At<int32_t>(item, 0)) : 0;
        if (itemID == 0)
            continue;
        const int visual = Engine<int(__thiscall *)(const void *)>(Offsets::FUN_VISIBLE_ITEM_ENCHANT_VISUAL)(item);
        if (handSlot && source.hideWeapons) { // kept for SetSheathed to show
            (slot == 15 ? actor.mainHandItemID : actor.offHandItemID) = itemID;
            (slot == 15 ? actor.mainHandVisual : actor.offHandVisual) = visual;
            continue;
        }
        TryOnWithVisual(actor, itemID, visual, handSlot ? static_cast<int>(slot) : -1);
    }
    actor.weaponsHidden = source.hideWeapons;
    if (source.hideWeapons)
        return;
    using GetVirtual_t = const Actor::HandLane *(__thiscall *)(void *, int, bool);
    const auto getVirtual = At<GetVirtual_t>(At<const void *>(player, 0), Offsets::VTBL_UNIT_GET_VIRTUAL_ITEM);
    const Actor::HandLane *main = getVirtual(player, 0, true);
    const Actor::HandLane *off = getVirtual(player, 1, true);
    actor.mainHand = main != nullptr ? *main : Actor::HandLane{};
    actor.offHand = off != nullptr ? *off : Actor::HandLane{};
}

// The hand grips around what the hands hold (SetUpModel's attachment walks).
void CloseFingers(void *model) {
    bool right = false, left = false;
    for (void *child = At<void *>(model, Offsets::OFF_M2MODEL_ATTACH_LIST); child != nullptr;
         child = At<void *>(child, Offsets::OFF_M2MODEL_ATTACH_NEXT)) {
        const uint32_t id = At<uint32_t>(child, Offsets::OFF_M2MODEL_ATTACHMENT_ID);
        right = right || id == Offsets::M2_ATTACH_HAND_RIGHT;
        left = left || id == Offsets::M2_ATTACH_HAND_LEFT;
    }
    using Close_t = void(__cdecl *)(void *, int);
    if (left)
        Engine<Close_t>(Offsets::FUN_COMPONENT_CLOSE_FINGERS)(model, 1);
    if (right)
        Engine<Close_t>(Offsets::FUN_COMPONENT_CLOSE_FINGERS)(model, 0);
}

// The model path of a creature display, or null.
const char *DisplayModelPath(int displayID) {
    const uint8_t *display =
        DBC::Record(Offsets::VAR_CREATUREDISPLAYINFO_DBC_MIN_INDEX, Offsets::VAR_CREATUREDISPLAYINFO_DBC_MAX_INDEX,
                    Offsets::VAR_CREATUREDISPLAYINFO_DBC_INDEX_TABLE, displayID);
    const uint8_t *modelData =
        display == nullptr
            ? nullptr
            : DBC::Record(Offsets::VAR_CREATUREMODELDATA_DBC_MIN_INDEX, Offsets::VAR_CREATUREMODELDATA_DBC_MAX_INDEX,
                          Offsets::VAR_CREATUREMODELDATA_DBC_INDEX_TABLE,
                          At<int32_t>(display, Offsets::OFF_CREATUREDISPLAYINFO_MODEL_ID));
    const char *path = modelData != nullptr ? At<const char *>(modelData, Offsets::OFF_CREATUREMODELDATA_MODEL_NAME) : nullptr;
    return path != nullptr && *path != '\0' ? path : nullptr;
}

} // namespace

uint32_t AddLiveActor(Actor *actor) {
    const uint32_t serial = ++g_nextSerial;
    g_live[serial] = actor;
    return serial;
}

void RemoveLiveActor(uint32_t serial) { g_live.erase(serial); }

Actor *LiveActor(uint32_t serial) {
    const auto it = g_live.find(serial);
    return it != g_live.end() ? it->second : nullptr;
}

bool Actor::SetModelByUnit(const char *token, bool sheatheWeapons, bool autoDress, bool hideWeapons,
                           bool usePlayerNativeForm) {
    ClearModel();
    void *unit = Unit::ResolveToken(token);
    return unit != nullptr && ShowUnitObject(unit, sheatheWeapons, autoDress, hideWeapons, usePlayerNativeForm);
}

bool Actor::ShowUnitObject(void *unit, bool sheatheWeapons, bool dressUnit, bool hideWeapons, bool nativeForm) {
    ClearModel();
    autoDress = dressUnit;
    usePlayerNativeForm = nativeForm;
    if (Engine<int(__cdecl *)(void *, uint32_t)>(Offsets::FUN_READY_MODEL_FOR_UI)(unit, 3) == 0)
        return false;
    const uint8_t *unitFields = Unit::Descriptor(unit);
    if (unitFields == nullptr)
        return false;
    // OBJECT_FIELD_TYPE, the object manager's own type test.
    const bool player =
        (At<uint32_t>(At<const void *>(unit, Offsets::OFF_OBJECT_DESCRIPTORS), 8) & Offsets::OBJ_FLAGS_PLAYER) != 0;
    const int nativeDisplay = At<int32_t>(unitFields, Offsets::OFF_UNIT_FIELD_NATIVEDISPLAYID);
    const bool dress =
        player && (nativeForm || nativeDisplay == At<int32_t>(unitFields, Offsets::OFF_UNIT_FIELD_DISPLAYID));
    void *newModel = nullptr;
    const char *path = nullptr;
    if (dress) {
        path = DisplayModelPath(nativeDisplay);
        if (path != nullptr)
            newModel = Engine<void *(__thiscall *)(void *, const char *, uint32_t)>(Offsets::FUN_M2SCENE_CREATE_MODEL)(
                M2SceneOf(frame_), path, 0);
    } else if (void *world = At<void *>(unit, Offsets::OFF_UNIT_WORLD_MODEL)) {
        newModel = Engine<void *(__thiscall *)(void *, void *, uint32_t)>(Offsets::FUN_M2SCENE_DUPLICATE_MODEL)(
            M2SceneOf(frame_), world, 0);
    }
    if (newModel == nullptr)
        return false;
    if ((At<uint32_t>(newModel, Offsets::OFF_M2MODEL_FFLAGS) & 1) == 0)
        Engine<void(__thiscall *)(void *, int)>(Offsets::FUN_M2MODEL_WAIT_FOR_LOAD)(newModel, 0);
    Engine<void(__cdecl *)(void *)>(Offsets::FUN_COMPONENT_REMOVE_VISUALS)(newModel);
    const UnitSource source{unit, dress, dressUnit, sheatheWeapons, hideWeapons};
    SetModel(newModel, path, nullptr, &source);
    return model != nullptr;
}

void Actor::ShowUnit(const UnitSource &source) {
    const void *objectFields = At<const void *>(source.unit, Offsets::OFF_OBJECT_DESCRIPTORS);
    unitGuid[0] = At<uint32_t>(objectFields, 0);
    unitGuid[1] = At<uint32_t>(objectFields, 4);
    sheathed = source.sheatheWeapons;
    if (source.dressable)
        DressAsPlayer(*this, source.unit, source);
    CloseFingers(model);
}

int Actor::TryOn(int itemID, const uint32_t *enchants, int hand) {
    bool hasVisual = false;
    if (const uint8_t *stats = ItemStats(itemID, nullptr, 0)) {
        uint8_t display[Offsets::ITEMDISPLAYINFO_RECORD_SIZE];
        if (DisplayRecord(At<int32_t>(stats, Offsets::OFF_ITEMSTATS_DISPLAY_INFO_ID), display))
            hasVisual = DBC::Record(Offsets::VAR_ITEMVISUALS_DBC_MIN_INDEX, Offsets::VAR_ITEMVISUALS_DBC_MAX_INDEX,
                                    Offsets::VAR_ITEMVISUALS_DBC_INDEX_TABLE,
                                    At<int32_t>(display, Offsets::OFF_ITEMDISPLAYINFO_ITEM_VISUAL)) != nullptr;
    }
    int visual = 0;
    if (enchants != nullptr && !hasVisual)
        for (int i = 0; i < 12 && visual == 0; ++i)
            if (const uint8_t *enchant = DBC::Record(
                    Offsets::VAR_SPELLITEMENCHANTMENT_DBC_MIN_INDEX, Offsets::VAR_SPELLITEMENCHANTMENT_DBC_MAX_INDEX,
                    Offsets::VAR_SPELLITEMENCHANTMENT_DBC_INDEX_TABLE, static_cast<int>(enchants[i])))
                visual = At<int32_t>(enchant, Offsets::OFF_SPELLITEMENCHANTMENT_ITEM_VISUAL);
    const int reason = TryOnWithVisual(*this, itemID, visual, hand);
    if (reason == TRYON_SUCCESS)
        CloseFingers(model);
    return reason;
}

namespace {

// An inventory slot (1 head .. 19 tabard) as the component's armor slot, or
// -1 for one it does not show.
int ArmorSlotOf(int inventorySlot) {
    switch (inventorySlot) {
    case 1: return 0;
    case 3: return 1;
    case 4: return 2;
    case 5: return 3;
    case 6: return 4;
    case 7: return 5;
    case 8: return 6;
    case 9: return 7;
    case 10: return 8;
    case 15: return 10;
    case 19: return Offsets::COMPONENT_ITEM_SLOT_TABARD;
    default: return -1;
    }
}

void TakeOffHand(Actor &actor, int hand) {
    Actor::HandLane &lane = hand == 15 ? actor.mainHand : actor.offHand;
    if (lane.classID != 0)
        RemoveHandItem(actor.model, hand, lane);
    lane = Actor::HandLane{};
}

} // namespace

// CGDressUpModelFrame::Undress (0x00597BA0); the weapons only when asked.
void Actor::Undress(bool includeWeapons) {
    if (!dressable || model == nullptr)
        return;
    for (int slot = 0; slot < Offsets::COMPONENT_ITEM_SLOT_COUNT; ++slot)
        RemoveItem(component, slot);
    if (!includeWeapons)
        return;
    TakeOffHand(*this, 15);
    TakeOffHand(*this, 16);
    mainHandItemID = offHandItemID = mainHandVisual = offHandVisual = 0;
}

void Actor::UndressSlot(int inventorySlot) {
    if (!dressable || model == nullptr)
        return;
    if (inventorySlot == 16 || inventorySlot == 17) {
        const int hand = inventorySlot - 1;
        TakeOffHand(*this, hand);
        (hand == 15 ? mainHandItemID : offHandItemID) = 0;
        (hand == 15 ? mainHandVisual : offHandVisual) = 0;
        return;
    }
    if (const int slot = ArmorSlotOf(inventorySlot); slot >= 0)
        RemoveItem(component, slot);
}

// The unit's own item in the slot, as SetModelByUnit put it there, or the
// slot emptied when the unit shows nothing in it.
void Actor::DressPlayerSlot(int inventorySlot) {
    void *player = PlayerOf(*this);
    if (!dressable || model == nullptr || player == nullptr || inventorySlot < 1 || inventorySlot > 19)
        return;
    const uint32_t visibleSlot = static_cast<uint32_t>(inventorySlot - 1);
    const uint32_t flags =
        At<uint32_t>(At<const uint8_t *>(player, Offsets::OFF_CGPLAYER_INFO), Offsets::OFF_PLAYER_FIELD_FLAGS);
    const bool hidden = (visibleSlot == 0 && (flags & Offsets::PLAYER_FLAG_HIDE_HELM)) ||
                        (visibleSlot == 14 && (flags & Offsets::PLAYER_FLAG_HIDE_CLOAK));
    using GetVisible_t = const uint8_t *(__thiscall *)(void *, uint32_t);
    const uint8_t *item = Engine<GetVisible_t>(Offsets::FUN_PLAYER_GET_VISIBLE_ITEM)(player, visibleSlot);
    const int itemID = item != nullptr && !hidden ? std::abs(At<int32_t>(item, 0)) : 0;
    UndressSlot(inventorySlot);
    if (itemID == 0)
        return;
    const int visual = Engine<int(__thiscall *)(const void *)>(Offsets::FUN_VISIBLE_ITEM_ENCHANT_VISUAL)(item);
    const bool handSlot = inventorySlot == 16 || inventorySlot == 17;
    if (TryOnWithVisual(*this, itemID, visual, handSlot ? inventorySlot - 1 : -1) == TRYON_SUCCESS)
        CloseFingers(model);
}

// The unit's model built again (DressUpModel's Dress, which is RefreshUnit),
// dressed whatever the autoDress setting.
void Actor::Dress() {
    void *unit = Guid::ResolveObject({unitGuid[0], unitGuid[1]}, Offsets::OBJ_FLAGS_UNIT);
    if (unit == nullptr)
        return;
    const bool keepAutoDress = autoDress;
    ShowUnitObject(unit, sheathed, true, weaponsHidden, usePlayerNativeForm);
    autoDress = keepAutoDress;
}

// The hand items hung again: at their sheath points, in the hands, or off.
void Actor::SetSheathed(bool sheathe, bool hidden) {
    sheathed = sheathe;
    if (!dressable || model == nullptr) {
        weaponsHidden = hidden;
        return;
    }
    const int mainID = mainHandItemID, mainVisual = mainHandVisual;
    const int offID = offHandItemID, offVisual = offHandVisual;
    TakeOffHand(*this, 15);
    TakeOffHand(*this, 16);
    if (!hidden) {
        if (mainID != 0)
            TryOnWithVisual(*this, mainID, mainVisual, 15);
        if (offID != 0)
            TryOnWithVisual(*this, offID, offVisual, 16);
        CloseFingers(model);
    }
    weaponsHidden = hidden;
}

} // namespace Model::Scene
