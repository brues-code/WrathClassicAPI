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

// A spell visual kit on a ModelSceneActor (actor:SetSpellVisualKit).
//
// The engine shows a kit on a model outside the world once: the char-select
// ghost, CGlueMgr::AttachCharacterSelectVisuals (0x004E2CE0). This is that
// routine for any kit, on the actor's model:
//   - each of its nine effect columns: a model of the SpellVisualEffectName
//     file, made in the scene, scaled by the record's scale and attached at the
//     column's attachment when the model has it (ProcessAttachment
//     0x004E2C30); the effects take the scene's light, as the actor does;
//   - its procedures: kind 1 the tint, kind 14 the alpha, clamped to 0..1.
// The kit's animation, sound and world effect are not shown, as on a model in
// a frame retail shows none of them.
//
// An effect's sequence loops by itself, so a lasting effect stays until the
// kit or the model changes. A one-shot effect stops at the end of its first
// lap, as the world stops one (CEffect::MoveToDestroyList 0x006F87C0): no
// callbacks, its mesh hidden, its emitters off, so the particles already out
// finish; it is freed once it has none left (fFlags 0x400, the world's test).
// A new kit, a new model or the actor's end takes every effect off at once.

#include "model/Scene.h"

#include "Offsets.h"
#include "dbc/Store.h"

#include <cstdint>

namespace Model::Scene {

namespace {

template <typename T> T &At(void *p, uintptr_t off) { return *reinterpret_cast<T *>(static_cast<uint8_t *>(p) + off); }
template <typename T> T At(const void *p, uintptr_t off) {
    return *reinterpret_cast<const T *>(static_cast<const uint8_t *>(p) + off);
}

// The ghost's columns: the record field and the attachment it hangs at.
struct Column {
    uintptr_t field;
    uint32_t attachment;
};
constexpr Column kColumns[] = {
    {Offsets::OFF_SPELLVISUALKIT_HEAD_EFFECT, Offsets::M2_ATTACH_HEAD},
    {Offsets::OFF_SPELLVISUALKIT_CHEST_EFFECT, Offsets::M2_ATTACH_CHEST},
    {Offsets::OFF_SPELLVISUALKIT_BASE_EFFECT, Offsets::M2_ATTACH_BASE},
    {Offsets::OFF_SPELLVISUALKIT_LEFT_HAND_EFFECT, Offsets::M2_ATTACH_LEFT_HAND},
    {Offsets::OFF_SPELLVISUALKIT_RIGHT_HAND_EFFECT, Offsets::M2_ATTACH_RIGHT_HAND},
    {Offsets::OFF_SPELLVISUALKIT_BREATH_EFFECT, Offsets::M2_ATTACH_BREATH},
    {Offsets::OFF_SPELLVISUALKIT_SPECIAL_EFFECTS + 0, Offsets::M2_ATTACH_SPECIAL_1 + 0},
    {Offsets::OFF_SPELLVISUALKIT_SPECIAL_EFFECTS + 4, Offsets::M2_ATTACH_SPECIAL_1 + 1},
    {Offsets::OFF_SPELLVISUALKIT_SPECIAL_EFFECTS + 8, Offsets::M2_ATTACH_SPECIAL_1 + 2},
};

const uint8_t *KitRecord(uint32_t id) {
    return DBC::Record(Offsets::VAR_SPELLVISUALKIT_DBC_MIN_INDEX, Offsets::VAR_SPELLVISUALKIT_DBC_MAX_INDEX,
                       Offsets::VAR_SPELLVISUALKIT_DBC_INDEX_TABLE, static_cast<int>(id));
}

const uint8_t *EffectRecord(int32_t id) {
    return DBC::Record(Offsets::VAR_SPELLVISUALEFFECTNAME_DBC_MIN_INDEX,
                       Offsets::VAR_SPELLVISUALEFFECTNAME_DBC_MAX_INDEX,
                       Offsets::VAR_SPELLVISUALEFFECTNAME_DBC_INDEX_TABLE, id);
}

using SequenceCallback_t = void(__cdecl *)(void *, uint32_t, uint32_t, int, uint32_t, uint32_t, uint32_t);

void SetSequenceCallback(void *model, SequenceCallback_t callback, Actor *actor) {
    reinterpret_cast<void(__fastcall *)(void *, void *, const void *, uint32_t, uint32_t)>(
        Offsets::FUN_M2MODEL_SET_SEQUENCE_CALLBACK)(model, nullptr, reinterpret_cast<const void *>(callback),
                                                   static_cast<uint32_t>(reinterpret_cast<uintptr_t>(actor)), 0);
}

void Detach(void *model) {
    if (At<const void *>(model, Offsets::OFF_M2MODEL_ATTACH_PARENT) != nullptr)
        reinterpret_cast<void(__fastcall *)(void *, void *)>(Offsets::FUN_M2MODEL_DETACH_FROM_PARENT)(model, nullptr);
}

void Release(void *model) {
    reinterpret_cast<void(__fastcall *)(void *, void *)>(Offsets::FUN_M2MODEL_RELEASE)(model, nullptr);
}

void SetTint(void *model, float r, float g, float b) {
    float *tint = &At<float>(model, Offsets::OFF_M2MODEL_DIFFUSE_TINT);
    tint[0] = r;
    tint[1] = g;
    tint[2] = b;
}

// A one-shot effect's lap ended: stop it as the world stops an effect.
void __cdecl OnOneShotEnd(void *model, uint32_t boneId, uint32_t /*sequenceId*/, int interrupted, uint32_t /*time*/,
                          uint32_t paramLow, uint32_t /*paramHigh*/) {
    if (interrupted != 0 || boneId != 0xFFFFFFFFu)
        return;
    auto *actor = reinterpret_cast<Actor *>(static_cast<uintptr_t>(paramLow));
    for (Actor::KitEffect &effect : actor->kitEffects) {
        if (effect.model != model || effect.stopped)
            continue;
        effect.stopped = true;
        SetSequenceCallback(model, nullptr, nullptr);
        uint32_t &flags = At<uint32_t>(model, Offsets::OFF_M2MODEL_FFLAGS);
        flags = (flags & ~static_cast<uint32_t>(Offsets::M2MODEL_FFLAGS_STOP_CLEAR_ATTACHED)) |
                Offsets::M2MODEL_FFLAGS_STOP_SET_ATTACHED;
        reinterpret_cast<void(__fastcall *)(void *, void *, int)>(Offsets::FUN_M2MODEL_SET_EMITTERS_ENABLED)(
            model, nullptr, 0);
        return;
    }
}

// ProcessAttachment's steps for one column; the effect, with our reference,
// or null.
void *Hang(Actor &actor, int32_t effectID, uint32_t attachment) {
    void *model = actor.model;
    const bool has = reinterpret_cast<int(__fastcall *)(void *, void *, uint32_t)>(
                         Offsets::FUN_M2MODEL_HAS_ATTACHMENT)(model, nullptr, attachment) != 0;
    const uint8_t *record = has ? EffectRecord(effectID) : nullptr;
    if (record == nullptr)
        return nullptr;
    void *effect = reinterpret_cast<void *(__fastcall *)(void *, void *, const char *, uint32_t)>(
        Offsets::FUN_M2SCENE_CREATE_MODEL)(M2SceneOf(actor.SceneFrame()), nullptr,
                                           At<const char *>(record, Offsets::OFF_SPELLVISUALEFFECTNAME_FILE), 0);
    if (effect == nullptr)
        return nullptr;
    const float scale = At<float>(record, Offsets::OFF_SPELLVISUALEFFECTNAME_SCALE);
    float *m = &At<float>(effect, Offsets::OFF_M2MODEL_LOCAL_TRANSFORM);
    for (int i = 0; i < 16; ++i)
        m[i] = 0.0f;
    m[0] = m[5] = m[10] = scale;
    m[15] = 1.0f;
    At<uint32_t>(effect, Offsets::OFF_M2MODEL_FFLAGS) |= Offsets::M2MODEL_FFLAGS_LOCAL_TRANSFORM;
    At<const void *>(effect, Offsets::OFF_M2MODEL_LIGHTING_CALLBACK) =
        At<const void *>(model, Offsets::OFF_M2MODEL_LIGHTING_CALLBACK);
    At<void *>(effect, Offsets::OFF_M2MODEL_LIGHTING_ARG) = At<void *>(model, Offsets::OFF_M2MODEL_LIGHTING_ARG);
    if (actor.kitOneShot)
        SetSequenceCallback(effect, &OnOneShotEnd, &actor);
    reinterpret_cast<void(__fastcall *)(void *, void *, void *, uint32_t, const float *, int)>(
        Offsets::FUN_M2MODEL_ATTACH_TO_PARENT)(effect, nullptr, model, attachment, nullptr, 0);
    return effect;
}

void FreeEffect(void *effect) {
    SetSequenceCallback(effect, nullptr, nullptr);
    At<const void *>(effect, Offsets::OFF_M2MODEL_LIGHTING_CALLBACK) = nullptr;
    At<void *>(effect, Offsets::OFF_M2MODEL_LIGHTING_ARG) = nullptr;
    Detach(effect);
    Release(effect);
}

} // namespace

void Actor::SetSpellVisualKit(uint32_t kitID, bool oneShot) {
    DropKit();
    spellVisualKit = kitID;
    kitOneShot = oneShot;
    if (loaded)
        ApplyKit();
}

void Actor::ApplyKit() {
    const uint8_t *kit = spellVisualKit != 0 && model != nullptr ? KitRecord(spellVisualKit) : nullptr;
    if (kit == nullptr)
        return;
    for (const Column &column : kColumns)
        if (void *effect = Hang(*this, At<int32_t>(kit, column.field), column.attachment))
            kitEffects.push_back({effect, false});
    for (uint32_t i = 0; i < Offsets::SPELLVISUALKIT_PROC_COUNT; ++i) {
        const int32_t kind = At<int32_t>(kit, Offsets::OFF_SPELLVISUALKIT_PROC_KIND + i * 4);
        const float value = At<float>(kit, Offsets::OFF_SPELLVISUALKIT_PROC_VALUE + i * 4);
        if (kind == Offsets::SPELLVISUALKIT_PROC_TINT) {
            const auto color = static_cast<uint32_t>(static_cast<int64_t>(value + (value < 0 ? -0.5f : 0.5f)));
            constexpr float kByte = 1.0f / 255.0f;
            SetTint(model, ((color >> 16) & 0xFF) * kByte, ((color >> 8) & 0xFF) * kByte, (color & 0xFF) * kByte);
        } else if (kind == Offsets::SPELLVISUALKIT_PROC_ALPHA) {
            kitAlpha = value < 0.0f ? 0.0f : (value > 1.0f ? 1.0f : value);
            ApplyAlpha();
        }
    }
}

void Actor::DropKit() {
    for (const KitEffect &effect : kitEffects)
        FreeEffect(effect.model);
    kitEffects.clear();
    kitAlpha = 1.0f;
    if (model != nullptr) {
        SetTint(model, 1.0f, 1.0f, 1.0f);
        ApplyAlpha();
    }
}

void Actor::DrainKit() {
    for (size_t i = 0; i < kitEffects.size();) {
        const KitEffect &effect = kitEffects[i];
        if (!effect.stopped ||
            (At<uint32_t>(effect.model, Offsets::OFF_M2MODEL_FFLAGS) & Offsets::M2MODEL_FFLAGS_LIVE_PARTICLES) != 0) {
            ++i;
            continue;
        }
        FreeEffect(effect.model);
        kitEffects.erase(kitEffects.begin() + static_cast<std::ptrdiff_t>(i));
    }
}

} // namespace Model::Scene
