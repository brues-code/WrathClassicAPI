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

// `GetCursorDelta()` -> deltaX, deltaY
//
// How far the cursor moved since the last frame, 0 when it did not, in
// GetCursorPosition's units (InputUtil.GetCursorDelta divides by a frame's
// effective scale). A ModelScene's orbit camera drags with it.
//
// The cursor is sampled once a frame, before any OnUpdate runs, as the
// in-game GetCursorPosition (0x00510A10) reads it, so every handler in a frame
// sees the same value.

#include "Game.h"
#include "Offsets.h"
#include "tick/FrameTick.h"

namespace Input::CursorDelta {

namespace {

template <typename T> T Global(uintptr_t address) { return *reinterpret_cast<const T *>(address); }

struct Position {
    double x = 0.0;
    double y = 0.0;
};

Position g_previous;
Position g_current;
bool g_sampled = false;

// What GetCursorPosition pushes now; false before the UI top exists.
bool ReadCursor(Position &out) {
    const auto *top = Global<const uint8_t *>(Offsets::VAR_SIMPLE_TOP);
    if (top == nullptr)
        return false;
    const auto *ndc = reinterpret_cast<const float *>(top + Offsets::OFF_SIMPLE_TOP_MOUSE_POSITION);
    const float widthScale = Global<float>(Offsets::VAR_DDC_WIDTH_SCALE);
    const float heightScale = Global<float>(Offsets::VAR_DDC_HEIGHT_SCALE);
    const float units = Global<float>(Offsets::VAR_ASPECT_COMPENSATION) * 1024.0f;
    out.x = (units * (ndc[0] * widthScale)) / widthScale;
    out.y = (units * (ndc[1] * heightScale)) / widthScale;
    return true;
}

void Sample() {
    Position now;
    if (!ReadCursor(now))
        return;
    g_previous = g_sampled ? g_current : now;
    g_current = now;
    g_sampled = true;
}

const Tick::FrameTick::AutoSubscribeBefore _tick{&Sample};

int __cdecl Script_GetCursorDelta(void *L) {
    Game::Lua::PushNumber(L, g_current.x - g_previous.x);
    Game::Lua::PushNumber(L, g_current.y - g_previous.y);
    return 2;
}

void RegisterLuaFunctions() { Game::Lua::RegisterGlobalFunction("GetCursorDelta", &Script_GetCursorDelta); }

const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};

} // namespace

} // namespace Input::CursorDelta
