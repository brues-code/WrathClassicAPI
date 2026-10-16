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

#include "cvar/CVar.h"

#include "Offsets.h"

namespace CVar {

namespace {

using LookupRegistered_t = Handle(__cdecl *)(const char *name);
using Set_t = bool(__thiscall *)(Handle cvar, const char *value, bool setValue, bool setReset,
                                 bool setDefault, bool setNeedsSave);

const char *StringAt(Handle cvar, int offset) {
    const char *s = *reinterpret_cast<const char *const *>(cvar + offset);
    return s != nullptr ? s : "";
}

} // namespace

Handle Find(const char *name) {
    if (name == nullptr)
        return nullptr;
    Handle cvar =
        reinterpret_cast<LookupRegistered_t>(Offsets::FUN_CVAR_LOOKUP_REGISTERED)(name);
    if (cvar == nullptr || (Flags(cvar) & Offsets::CVAR_FLAG_HIDDEN) != 0)
        return nullptr;
    return cvar;
}

uint32_t Flags(Handle cvar) {
    return *reinterpret_cast<const uint32_t *>(cvar + Offsets::OFF_CVAR_FLAGS);
}

const char *Value(Handle cvar) { return StringAt(cvar, Offsets::OFF_CVAR_VALUE_STR); }
const char *Default(Handle cvar) { return StringAt(cvar, Offsets::OFF_CVAR_DEFAULT_STR); }

const char **ValueSlot(Handle cvar) {
    return reinterpret_cast<const char **>(cvar + Offsets::OFF_CVAR_VALUE_STR);
}

bool IsReadOnly(Handle cvar) {
    return (Flags(cvar) & (Offsets::CVAR_FLAG_READ_ONLY | Offsets::CVAR_FLAG_READ_ONLY_2)) != 0;
}

bool IsBlocked(Handle cvar) {
    if ((Flags(cvar) & Offsets::CVAR_FLAG_PROTECTED) == 0)
        return false;
    const auto *top = *reinterpret_cast<const uint8_t *const *>(
        static_cast<uintptr_t>(Offsets::VAR_SIMPLE_TOP));
    return top == nullptr ||
           *reinterpret_cast<const int *>(top + Offsets::OFF_SIMPLE_TOP_ALLOW_PROTECTED) == 0;
}

void Set(Handle cvar, const char *value, bool persist) {
    reinterpret_cast<Set_t>(Offsets::FUN_CVAR_SET)(cvar, value, true, false, false, persist);
}

} // namespace CVar
