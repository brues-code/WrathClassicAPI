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

// `CURSOR_CHANGED` — fires when what the cursor holds changes, carrying the
// old and new cursor types:
//
//   CURSOR_CHANGED: isDefault, newCursorType, oldCursorType, oldCursorVirtualID
//
// Plus the `Enum.UICursorType` table the two type arguments index. Ported
// from ClassicAPI's src/cursor/Changed.cpp.
//
// The trigger is a once-per-frame diff of the cursor globals, not the
// engine's own `CURSOR_UPDATE`. That event doesn't cover the cursor: it
// fires from CGGameUI::ClearCursor after the type is already zeroed, and
// again only from the bag-item and virtual-item setters. Picking up money,
// a spell, a macro, a pet action, a stabled pet or an equipment set writes
// the cursor in its own setter and announces nothing. There's no common
// choke point on the set side, so a state diff is what covers every type.

#include "Game.h"
#include "Guid.h"
#include "Offsets.h"
#include "event/Custom.h"
#include "item/ID.h"
#include "tick/FrameTick.h"

#include <cstdint>

namespace Cursor::Changed {

namespace {

const char *const kCursorChanged = "CURSOR_CHANGED";
const Event::Custom::AutoReserve _reserveCursorChanged{kCursorChanged};

// The full modern enum, not just the values this client can produce. An
// addon comparing against `Enum.UICursorType.BattlePet` should read a
// number it can never match, rather than index a nil field.
const Game::Lua::EnumIntegerEntry kCursorTypeEntries[] = {
    {"Default", 0},        {"Item", 1},
    {"Money", 2},          {"Spell", 3},
    {"PetAction", 4},      {"Merchant", 5},
    {"ActionBar", 6},      {"Macro", 7},
    {"Ammo", 8},           {"Pet", 9},
    {"GuildBank", 10},     {"GuildBankMoney", 11},
    {"EquipmentSet", 12},  {"Currency", 13},
    {"Flyout", 14},        {"VoidItem", 15},
    {"BattlePet", 16},     {"Mount", 17},
    {"Toy", 18},           {"ConduitCollectionItem", 19},
    {"PerksProgramVendorItem", 20},
};

constexpr int UI_DEFAULT = 0;
constexpr int UI_ITEM = 1;
constexpr int UI_MONEY = 2;
constexpr int UI_SPELL = 3;
constexpr int UI_PET_ACTION = 4;
constexpr int UI_MERCHANT = 5;
constexpr int UI_MACRO = 7;
constexpr int UI_AMMO = 8;
constexpr int UI_PET = 9;
constexpr int UI_GUILD_BANK = 10;
constexpr int UI_GUILD_BANK_MONEY = 11;
constexpr int UI_EQUIPMENT_SET = 12;

uint32_t ReadVar(uintptr_t va) { return *reinterpret_cast<const volatile uint32_t *>(va); }

// What the cursor holds, in the terms `CURSOR_CHANGED` reports.
struct State {
    int uiType; // Enum.UICursorType; Default when empty
    // The held content's identifying number: itemID, spellID, 1-based
    // macro slot, 1-based merchant slot, equipment setID, or a money amount
    // in copper. 0 for an empty cursor and for the two pet cursors.
    uint32_t virtualID;
};

// The cursor's type plus the globals that identify what that type holds —
// plain reads, cheap enough for every frame. Equal snapshots mean the
// cursor hasn't changed, including within one type (swapping between two
// stacks of the same item changes the GUID). Only the live type's globals
// count: the setters leave the others stale.
struct Raw {
    uint32_t type = 0;
    uint32_t key1 = 0;
    uint32_t key2 = 0;

    bool operator==(const Raw &o) const {
        return type == o.type && key1 == o.key1 && key2 == o.key2;
    }
};

Raw ReadRaw() {
    const uint32_t type = ReadVar(Offsets::VAR_CURSOR_TYPE);
    switch (type) {
        case 1:
            return {type, ReadVar(Offsets::VAR_CURSOR_ITEM_GUID_LO),
                    ReadVar(Offsets::VAR_CURSOR_ITEM_GUID_HI)};
        case 5:
        case 7:
        case 9:
        case 11:
            return {type, ReadVar(Offsets::VAR_CURSOR_VIRTUAL_ID),
                    ReadVar(Offsets::VAR_CURSOR_VIRTUAL_SLOT)};
        case 2:
        case 12: return {type, ReadVar(Offsets::VAR_CURSOR_MONEY)};
        case 3:  return {type, ReadVar(Offsets::VAR_CURSOR_SPELL_ID)};
        case 4:  return {type, ReadVar(Offsets::VAR_CURSOR_PET_ACTION)};
        case 8:  return {type, ReadVar(Offsets::VAR_CURSOR_MACRO_ID)};
        case 10: return {type, ReadVar(Offsets::VAR_CURSOR_STABLE_PET)};
        case 13: return {type, ReadVar(Offsets::VAR_CURSOR_EQUIPMENT_SET)};
        default: return {type};
    }
}

uint32_t HeldItemID(const Raw &raw) {
    const auto *item = static_cast<const uint8_t *>(
        Guid::ResolveObject({raw.key1, raw.key2}, Offsets::OBJ_FLAGS_ITEM));
    const int itemID = item != nullptr ? Item::ID::FromCGItem(item) : 0;
    return itemID > 0 ? static_cast<uint32_t>(itemID) : 0;
}

uint32_t MacroSlot(uint32_t macroID) {
    using IndexByID_t = int(__cdecl *)(uint32_t macroID);
    const int slot0 = reinterpret_cast<IndexByID_t>(Offsets::FUN_MACRO_INDEX_BY_ID)(macroID);
    return slot0 >= 0 ? static_cast<uint32_t>(slot0 + 1) : 0;
}

State Decode(const Raw &raw) {
    switch (raw.type) {
        case 1:  return {UI_ITEM, HeldItemID(raw)};
        case 2:  return {UI_MONEY, raw.key1};
        case 3:  return {UI_SPELL, raw.key1};
        case 4:  return {UI_PET_ACTION, 0};
        case 5:  return {UI_MERCHANT, raw.key2 + 1};
        case 7:  return {UI_ITEM, raw.key1};
        case 8:  return {UI_MACRO, MacroSlot(raw.key1)};
        case 9:  return {UI_AMMO, raw.key1};
        case 10: return {UI_PET, 0};
        case 11: return {UI_GUILD_BANK, raw.key1};
        case 12: return {UI_GUILD_BANK_MONEY, raw.key1};
        case 13: return {UI_EQUIPMENT_SET, raw.key1};
        default: return {UI_DEFAULT, 0};
    }
}

// The cursor as of the last event we fired. Deliberately NOT reset on
// /reload: the engine's cursor globals outlive the reload, so a cursor
// still holding something reports its real previous type rather than a
// phantom change back from Default.
Raw g_lastRaw{};
State g_lastState{UI_DEFAULT, 0};

void OnTick() {
    const Raw raw = ReadRaw();
    if (raw == g_lastRaw)
        return; // cursor untouched this frame — the common case
    g_lastRaw = raw;

    const State now = Decode(raw);
    const State previous = g_lastState;
    g_lastState = now;

    Event::Custom::Fire(Event::Custom::Lookup(kCursorChanged), "%b%d%d%u",
                        now.uiType == UI_DEFAULT ? 1 : 0, now.uiType, previous.uiType,
                        previous.virtualID);
}

const Tick::FrameTick::AutoSubscribe _tick{&OnTick};

void RegisterLuaFunctions() {
    Game::Lua::RegisterIntegerEnum(
        "Enum", "UICursorType", kCursorTypeEntries,
        static_cast<int>(sizeof(kCursorTypeEntries) / sizeof(kCursorTypeEntries[0])));
}

const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace

} // namespace Cursor::Changed
