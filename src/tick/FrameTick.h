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

namespace Tick::FrameTick {

// Shared per-frame callback registry. `FrameScript_FireOnUpdate` (the
// engine's once-per-render-frame OnUpdate dispatch) allows only ONE
// MinHook detour, so FrameTick.cpp owns it and fans out to subscribers.
// Subscribers run at the tail of each frame, AFTER the engine's own
// OnUpdate dispatch. Place a static instance at file scope:
//
//   static const Tick::FrameTick::AutoSubscribe _tick{&OnFrame};
//
// Callback order across modules is unspecified (static-init order);
// keep subscribers independent. Used by `Timer` (C_Timer heap walk)
// and `Bag::UpdateDelayed` (per-frame event coalescing).
struct AutoSubscribe {
    using Callback = void (*)();
    explicit AutoSubscribe(Callback cb);
    Callback cb;
    AutoSubscribe *next;
};

} // namespace Tick::FrameTick
