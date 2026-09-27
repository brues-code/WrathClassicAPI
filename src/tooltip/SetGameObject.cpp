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

// `OnTooltipSetGameObject` tooltip script — fires whenever a tooltip is
// populated with a world gameobject, so addons can annotate gameobject
// tooltips with `GameTooltip:HookScript("OnTooltipSetGameObject", fn)` the
// same way they already do with OnTooltipSetUnit / Item / Spell. A real frame
// script: SetScript / GetScript / HookScript and XML <Scripts> all work.
//
// 3.3.5 ships the other OnTooltipSet* scripts, just not this one:
//
//   - The CGGameTooltip script resolver (FUN_GAMETOOLTIP_SCRIPT_RESOLVER) maps
//     a script name to its 8-byte {handler, context} slot on the tooltip. We
//     co-hook it: for "OnTooltipSetGameObject" (unknown to the engine) we hand
//     back a C-side per-tooltip slot. The resolver's only contract is "return
//     the address the script lives at", so external storage behaves exactly
//     like an in-object slot.
//
//   - Every gameobject tooltip goes through one builder
//     (FUN_GAMETOOLTIP_BUILD_GAMEOBJECT). We co-hook it and, once it has
//     stored the GUID, fire the handler through the same runner and argument
//     shape the engine uses for its own OnTooltipSet* scripts.

#include "Game.h"
#include "Offsets.h"

#include <cstdint>
#include <cstring>

namespace Tooltip::SetGameObject {

namespace {

constexpr const char *kScriptName = "OnTooltipSetGameObject";

// Per-tooltip {handler, context} slot. Tooltips are few (GameTooltip,
// ItemRefTooltip, ShoppingTooltip1/2/3, WorldMapTooltip, plus the odd addon
// tooltip), so a small fixed table with a linear scan is plenty and gives
// each slot a stable address to hand out. Cleared on every UI teardown: the
// tooltip objects are recreated (often at recycled addresses) and the Lua
// handler refs die with the old state.
struct Cell {
    void *tooltip;
    uint32_t slot[2];
};
constexpr int kMaxTooltips = 32;
Cell g_cells[kMaxTooltips];
int g_cellCount = 0;

uint32_t *SlotFor(void *tooltip, bool create) {
    for (int i = 0; i < g_cellCount; ++i)
        if (g_cells[i].tooltip == tooltip)
            return g_cells[i].slot;
    if (!create || g_cellCount >= kMaxTooltips)
        return nullptr;
    Cell &c = g_cells[g_cellCount++];
    c.tooltip = tooltip;
    c.slot[0] = 0;
    c.slot[1] = 0;
    return c.slot;
}

// __thiscall members modelled as __fastcall(self /*ecx*/, edx /*unused*/,
// ...stack args...). The stack-arg count must match the callee's RET N.
using Resolver_t = int(__fastcall *)(void *self, void *edx, const char *name,
                                     const char **argsTemplate);
using RunScript_t = void(__fastcall *)(void *self, void *edx, uint32_t *slot,
                                       int nargs, int context);
using BuildGameObject_t = void(__fastcall *)(void *self, void *edx,
                                             const uint64_t *guid);

Resolver_t Resolver_o = nullptr;
BuildGameObject_t BuildGameObject_o = nullptr;

int __fastcall Resolver_h(void *self, void *edx, const char *name,
                          const char **argsTemplate) {
    const int engineSlot = Resolver_o(self, edx, name, argsTemplate);
    if (engineSlot != 0 || name == nullptr || _stricmp(name, kScriptName) != 0)
        return engineSlot;
    return reinterpret_cast<int>(SlotFor(self, /*create*/ true));
}

void __fastcall BuildGameObject_h(void *self, void *edx, const uint64_t *guid) {
    BuildGameObject_o(self, edx, guid);

    // The builder leaves the GUID slot zero when the object couldn't be
    // resolved (tooltip hidden) — nothing was set, so nothing fires.
    const auto *base = static_cast<const uint8_t *>(self);
    const uint32_t guidLo = *reinterpret_cast<const uint32_t *>(
        base + Offsets::OFF_TOOLTIP_GAMEOBJECT_GUID_LO);
    const uint32_t guidHi = *reinterpret_cast<const uint32_t *>(
        base + Offsets::OFF_TOOLTIP_GAMEOBJECT_GUID_HI);
    if ((guidLo | guidHi) == 0)
        return;

    uint32_t *slot = SlotFor(self, /*create*/ false);
    if (slot == nullptr || slot[0] == 0)
        return;
    reinterpret_cast<RunScript_t>(Offsets::FUN_FRAME_RUN_SCRIPT)(self, nullptr, slot, 0, 0);
}

void PrepareForReload() { g_cellCount = 0; }

const Game::HookAutoRegister _resolverHook{
    Offsets::FUN_GAMETOOLTIP_SCRIPT_RESOLVER,
    reinterpret_cast<void *>(&Resolver_h),
    reinterpret_cast<void **>(&Resolver_o)};

const Game::HookAutoRegister _buildHook{
    Offsets::FUN_GAMETOOLTIP_BUILD_GAMEOBJECT,
    reinterpret_cast<void *>(&BuildGameObject_h),
    reinterpret_cast<void **>(&BuildGameObject_o)};

const Game::ReloadAutoRegister _reloadReg{&PrepareForReload};

} // namespace
} // namespace Tooltip::SetGameObject
