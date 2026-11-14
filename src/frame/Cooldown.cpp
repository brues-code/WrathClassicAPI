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

// The later clients' Cooldown API (FrameAPICooldownDocumentation.lua's
// contract, as ClassicAPI's src/frame/Cooldown.cpp ports it) on 3.3.5's own
// Cooldown frame, CGCooldown (WoW/Source/Ui/CGCooldown.{h,cpp} in the
// decompiled client).
//
// Every Cooldown is still the engine's CGCooldown: the "Cooldown" factory
// (CGCooldown::Create, detoured) builds one with the engine's constructor in a
// block of the CGCooldown size plus a pointer to our Fields, then installs our
// copies of CGCooldown's two vtables:
//
//     slot                          override
//     +0x00 deleting destructor     our Fields, then ~CGCooldown, then free
//     +0x08 GetScriptMetaTable      ours: CGCooldown's methods, then ours
//     +0x0C GetScriptByName         OnCooldownDone, then CSimpleFrame's
//     +0x78 OnLayerUpdate           the timer below, then the sweep
//     +0x84 OnFrameRender           the swipe, bling and edge, each its own texture
//     +0xD4 UpdateAlpha             CSimpleFrame's, then the colours
//     layout +0x00                  our destructor
//     layout +0x04 LoadXML          CGCooldown's, then the later attributes
//
// The engine's own fields stay in use: the timer (+0x29C..+0x2AC), the star
// texture as the swipe, the edge texture, and the vertex, texcoord and colour
// arrays the batch draws from in place. CGCooldown's ResetVerts and
// OnFrameSizeChanged run unchanged.
//
// The timer is CGCooldown::OnLayerUpdate's with these changes:
//   - the bling (the end flash) has a texture and colour of its own, lasts
//     1000 * modRate ms, and can be turned off (SetDrawBling);
//   - a cooldown that ends runs OnCooldownDone;
//   - a frame shown again after its cooldown ended (or cleared), or one with a
//     zero-length cooldown, hides without flashing, where the engine flashes
//     again; a start in the future shows the full sweep until it comes;
//   - a paused frame's clock stands still.
// A reverse cooldown keeps the engine's end: it holds at duration - 1, fully
// covered and without a flash, until the addon hides it (3.3.5's target and
// focus debuff icons rely on this), and runs OnCooldownDone once.
//
// The sweep is drawn with the engine's star fan (ten vertices, eight triangles
// around the centre v9, VAR_COOLDOWN_STAR_INDICES). CGCooldown's octant
// animators always start at 12 o'clock and give only the top and centre a
// texcoord; ours start at the rotation (SetRotation) and spread the swipe
// texture over the frame, and move exactly as the engine's do (each half side
// an eighth of the duration, the point linear along it).
//
// The countdown numbers are a font string each Cooldown makes for itself at
// creation (one of its regions, destroyed with it), shown while the sweep
// runs when the countdownForCooldowns CVar is on, the frame does not hide
// them, and the cooldown is longer than its minimum.

#include "Game.h"
#include "Offsets.h"
#include "frame/Methods.h"
#include "text/NumericFormatter.h"
#include "texture/Load.h"
#include "ui/FrameObject.h"
#include "xml/Node.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <type_traits>

namespace Frame::Cooldown {

namespace {

template <typename T> T &At(void *base, uintptr_t offset) {
    return *reinterpret_cast<T *>(static_cast<uint8_t *>(base) + offset);
}

struct C2Vector {
    float x, y;
};
struct C3Vector {
    float x, y, z;
};
struct Rect {
    float minY, minX, maxY, maxX;
};

// FrameScript_Object::ScriptIx / ScriptData.
struct ScriptIx {
    int32_t luaRef;
    const char *taint;
};
struct ScriptData {
    const char *wrapper;
};

// ---- the object ---------------------------------------------------------------------

// The three textures a Cooldown draws. The swipe and the edge are the
// engine's two (+0x2B0, +0x3D4); the bling is ours.
enum TextureSlot : uint32_t { SLOT_SWIPE, SLOT_EDGE, SLOT_BLING, SLOT_COUNT };

const char *const kSlotTag[SLOT_COUNT] = {"SwipeTexture", "EdgeTexture", "BlingTexture"};
// What SetCooldown fills a slot nothing has set with: CGCooldown's files.
const char *const kSlotDefaultFile[SLOT_COUNT] = {"interface\\cooldown\\star4.blp",
                                                  "interface\\cooldown\\edge.blp",
                                                  "interface\\cooldown\\star4.blp"};
// The draw layer each slot is queued in (CGCooldown::OnFrameRender).
const uint32_t kSlotLayer[SLOT_COUNT] = {Offsets::DRAWLAYER_ARTWORK, Offsets::DRAWLAYER_OVERLAY,
                                         Offsets::DRAWLAYER_ARTWORK};

struct Fields {
    void *blingTexture = nullptr;      // HTEXTURE, one reference
    uint32_t base[SLOT_COUNT] = {};    // BGRA before the frame alpha
    uint32_t texturesSet = 0;          // 1 << slot for each slot a setter or the XML filled
    bool done = false;                 // the cooldown ended (or was cleared)
    bool paused = false;
    uint32_t pausedAt = 0;             // ms: the clock while paused
    float modRate = 1.0f;              // scales the bling and the display duration
    bool drawSwipe = true;
    bool drawBling = true;
    float rotation = 0.0f;             // radians, clockwise from 12 o'clock
    ScriptIx onCooldownDone = {0, nullptr};

    // The countdown numbers (retail defaults, from ClassicAPI's probes).
    void *countdown = nullptr;           // the CSimpleFontString; the frame owns it
    bool hideCountdownNumbers = false;
    float abbrevThreshold = 120.0f;      // s: below it, m:ss in place of whole minutes
    float millisecondsThreshold = 0.0f;  // s: below it, tenths
    uint32_t minCountdownDuration = 2000; // ms: no numbers for a cooldown this short or shorter
    Text::NumericFormatter::Formatter *countdownFormatter = nullptr; // one reference, or null
    std::string countdownText;           // what the font string shows; a formatter's text has no bound
};

constexpr size_t kObjectSize = Offsets::COOLDOWN_SIZE + sizeof(Fields *);

Fields *&FieldsOf(void *frame) { return At<Fields *>(frame, Offsets::COOLDOWN_SIZE); }
Fields &F(void *frame) { return *FieldsOf(frame); }
void *FrameOfLayout(void *layout) { return static_cast<uint8_t *>(layout) - Offsets::OFF_FRAME_LAYOUT; }

uint32_t &StartTime(void *frame) { return At<uint32_t>(frame, Offsets::OFF_COOLDOWN_START_TIME); }
uint32_t &Duration(void *frame) { return At<uint32_t>(frame, Offsets::OFF_COOLDOWN_DURATION); }
uint32_t &Elapsed(void *frame) { return At<uint32_t>(frame, Offsets::OFF_COOLDOWN_ELAPSED); }
uint32_t &Flashing(void *frame) { return At<uint32_t>(frame, Offsets::OFF_COOLDOWN_FLASHING); }
uint32_t &Reverse(void *frame) { return At<uint32_t>(frame, Offsets::OFF_COOLDOWN_REVERSE); }
uint32_t &DrawEdge(void *frame) { return At<uint32_t>(frame, Offsets::OFF_COOLDOWN_DRAW_EDGE); }
C3Vector *StarVerts(void *frame) { return &At<C3Vector>(frame, Offsets::OFF_COOLDOWN_STAR_VERTS); }
C2Vector *StarTexCoords(void *frame) { return &At<C2Vector>(frame, Offsets::OFF_COOLDOWN_STAR_TEXCOORDS); }
C2Vector *FlashTexCoords(void *frame) { return &At<C2Vector>(frame, Offsets::OFF_COOLDOWN_FLASH_TEXCOORDS); }
C2Vector *EdgeTexCoords(void *frame) { return &At<C2Vector>(frame, Offsets::OFF_COOLDOWN_EDGE_TEXCOORDS); }
uint32_t &StarColor(void *frame) { return At<uint32_t>(frame, Offsets::OFF_COOLDOWN_STAR_COLOR); }
uint32_t &FlashColor(void *frame) { return At<uint32_t>(frame, Offsets::OFF_COOLDOWN_FLASH_COLOR); }
uint32_t &EdgeColor(void *frame) { return At<uint32_t>(frame, Offsets::OFF_COOLDOWN_EDGE_COLOR); }
const Rect &RectOf(void *frame) { return At<Rect>(frame, Offsets::OFF_FRAME_RECT); }

void *&SlotTexture(void *frame, TextureSlot slot) {
    switch (slot) {
    case SLOT_SWIPE:
        return At<void *>(frame, Offsets::OFF_COOLDOWN_STAR_TEXTURE);
    case SLOT_EDGE:
        return At<void *>(frame, Offsets::OFF_COOLDOWN_EDGE_TEXTURE);
    default:
        return F(frame).blingTexture;
    }
}

// ---- engine calls -------------------------------------------------------------------

// __thiscall entry points mirrored as __fastcall with a dummy EDX.
using FrameCall_t = int(__fastcall *)(void *frame, void *);

uint32_t NowMs() { return reinterpret_cast<uint32_t(__cdecl *)()>(Offsets::FUN_OS_GET_ASYNC_TIME_MS)(); }
// `now` is at or past `start`, on the wrapping millisecond clock.
bool Reached(uint32_t now, uint32_t start) { return static_cast<int32_t>(now - start) >= 0; }

void Show(void *frame) { reinterpret_cast<FrameCall_t>(Offsets::FUN_SIMPLEFRAME_SHOW)(frame, nullptr); }
void Hide(void *frame) { reinterpret_cast<FrameCall_t>(Offsets::FUN_SIMPLEFRAME_HIDE)(frame, nullptr); }
void ResetVerts(void *frame) { reinterpret_cast<FrameCall_t>(Offsets::FUN_COOLDOWN_RESET_VERTS)(frame, nullptr); }

// The layer's batch is rebuilt: what is queued in it changed.
void NotifyLayer(void *frame, uint32_t layer) {
    reinterpret_cast<void(__fastcall *)(void *, void *, uint32_t)>(Offsets::FUN_SIMPLEFRAME_NOTIFY_DRAW_LAYER_CHANGED)(
        frame, nullptr, layer);
}

int TypeId() {
    int &id = At<int>(reinterpret_cast<void *>(Offsets::VAR_COOLDOWN_OBJECT_TYPE), 0);
    if (id == 0)
        id = ++At<int>(reinterpret_cast<void *>(Offsets::VAR_SCRIPTOBJECT_TYPE_COUNTER), 0);
    return id;
}

// ---- colours ------------------------------------------------------------------------

uint32_t Bgra(uint32_t r, uint32_t g, uint32_t b, uint32_t a) { return (a << 24) | (r << 16) | (g << 8) | b; }

// A millisecond count into the uint32 range: below 0 (or NaN) is 0, past
// UINT32_MAX is UINT32_MAX, where a bare cast is undefined.
uint32_t ClampMs(double ms) {
    if (!(ms > 0.0))
        return 0;
    if (ms >= 4294967295.0)
        return UINT32_MAX;
    return static_cast<uint32_t>(ms);
}

uint32_t ColorByte(double c) {
    if (!(c > 0.0))
        return 0;
    if (c >= 1.0)
        return 255;
    return static_cast<uint32_t>(c * 255.0 + 0.5);
}

// CGCooldown's draw alpha: m_alpha * m_parentAlpha / 255.
uint32_t DrawAlpha(void *frame) {
    return static_cast<uint8_t>(At<uint8_t>(frame, Offsets::OFF_FRAME_ALPHA) *
                                At<uint8_t>(frame, Offsets::OFF_FRAME_PARENT_ALPHA) / 255);
}

uint32_t WithAlpha(uint32_t base, uint32_t alpha) {
    return (base & 0x00FFFFFFu) | (((base >> 24) * alpha / 255) << 24);
}

// CGCooldown::UpdateColors (0x005EC790), each colour from its slot's base,
// whose defaults are the engine's constants.
void UpdateColors(void *frame) {
    const Fields &f = F(frame);
    const uint32_t alpha = DrawAlpha(frame);
    if (!Flashing(frame)) {
        StarColor(frame) = WithAlpha(f.base[SLOT_SWIPE], alpha);
        EdgeColor(frame) = WithAlpha(f.base[SLOT_EDGE], alpha);
        FlashColor(frame) = 0;
    } else {
        StarColor(frame) = 0;
        EdgeColor(frame) = 0;
        FlashColor(frame) = WithAlpha(f.base[SLOT_BLING], alpha);
    }
}

// ---- the sweep ----------------------------------------------------------------------

// CGCooldown::s_flash* — the bling's seven keys, a sixth of its length apart.
const float s_flashScale[7] = {1.0f, 0.6993007f, 0.5405405f, 0.6289308f, 0.7518797f, 0.625f, 0.8695652f};
const float s_flashAlpha[7] = {0.0f, 0.5f, 1.0f, 0.75f, 0.5f, 0.25f, 0.0f};
const float s_flashRotation[7] = {0.0f,         -0.1303761f, -0.25918141f, -0.39269909f,
                                  -0.51836282f, -0.6518805f, -0.78539819f};

constexpr float kTwoPi = 6.2831855f;

// The point `s` turns along the frame's outline, clockwise from the top
// centre; each half side takes an eighth of a turn, as the octant animators
// (0x005EB130..0x005EB9A0) move it. z is 1, as the engine writes it.
C3Vector OutlinePoint(const Rect &r, float s) {
    const float cx = (r.minX + r.maxX) * 0.5f;
    const float cy = (r.minY + r.maxY) * 0.5f;
    const C3Vector knots[9] = {{cx, r.maxY, 1.0f},     {r.maxX, r.maxY, 1.0f}, {r.maxX, cy, 1.0f},
                               {r.maxX, r.minY, 1.0f}, {cx, r.minY, 1.0f},     {r.minX, r.minY, 1.0f},
                               {r.minX, cy, 1.0f},     {r.minX, r.maxY, 1.0f}, {cx, r.maxY, 1.0f}};
    const float e = (s - std::floor(s)) * 8.0f;
    const int k = e < 8.0f ? static_cast<int>(e) : 7;
    const float t = e - static_cast<float>(k);
    return {knots[k].x + (knots[k + 1].x - knots[k].x) * t, knots[k].y + (knots[k + 1].y - knots[k].y) * t, 1.0f};
}

// The covered part of the sweep: the outline from turn `a` to `b` (b - a at
// most 1) with the corners between, in v0..v8 around the centre v9, the tail
// repeating the last point (degenerate triangles), as the engine pads it.
void AnimateSweep(void *frame, const Rect &r, float a, float b) {
    C3Vector *v = StarVerts(frame);
    int n = 0;
    v[n++] = OutlinePoint(r, a);
    for (float corner = std::floor(a) + 0.125f; corner < b; corner += 0.25f)
        if (corner > a)
            v[n++] = OutlinePoint(r, corner);
    v[n++] = OutlinePoint(r, b);
    for (; n < 9; ++n)
        v[n] = v[n - 1];
    v[9] = {(r.minX + r.maxX) * 0.5f, (r.minY + r.maxY) * 0.5f, 1.0f};
}

// The swipe texture over the frame: each fan vertex's texcoord from its position.
void MapStarTexCoords(void *frame, const Rect &r) {
    const float w = r.maxX - r.minX;
    const float h = r.maxY - r.minY;
    const C3Vector *v = StarVerts(frame);
    C2Vector *tc = StarTexCoords(frame);
    for (int i = 0; i < 10; ++i)
        tc[i] = {w > 0.0f ? (v[i].x - r.minX) / w : 0.5f, h > 0.0f ? (r.maxY - v[i].y) / h : 0.5f};
}

// The engine's texcoord rotation for the edge and flash quads: the corners
// turned by `angle` about the texture centre and scaled.
void RotateQuadTexCoords(C2Vector *tc, float angle, float scale) {
    const float c = std::cos(angle);
    const float s = std::sin(angle);
    const float a = c * -0.5f;
    const float b = -0.5f * s;
    const float d = c * 0.5f;
    const float e = s * 0.5f;
    tc[0] = {(a - b) * scale + 0.5f, (b + a) * scale + 0.5f};
    tc[1] = {(d - b) * scale + 0.5f, (e + a) * scale + 0.5f};
    tc[2] = {(a - e) * scale + 0.5f, (b + d) * scale + 0.5f};
    tc[3] = {(d - e) * scale + 0.5f, (d + e) * scale + 0.5f};
}

// CGCooldown::AnimateVerts (0x005EC1E0).
void AnimateVerts(void *frame) {
    const Fields &f = F(frame);
    const Rect &r = RectOf(frame);
    const float elapsed = static_cast<float>(Elapsed(frame));
    const float duration = static_cast<float>(Duration(frame));

    if (!Flashing(frame)) {
        const float done = duration > 0.0f ? std::fmin(elapsed / duration, 1.0f) : 1.0f;
        const float start = f.rotation / kTwoPi;
        // Covers what is left; reverse covers what has passed.
        if (Reverse(frame))
            AnimateSweep(frame, r, start, start + done);
        else
            AnimateSweep(frame, r, start + done, start + 1.0f);
        MapStarTexCoords(frame, r);
        if (DrawEdge(frame))
            RotateQuadTexCoords(EdgeTexCoords(frame), done * -kTwoPi - f.rotation,
                                At<float>(frame, Offsets::OFF_FRAME_LAYOUT_SCALE));
        return;
    }

    // The bling on the engine's 1000 ms timeline, whatever its length. At its
    // last update (elapsed == duration) the engine reads one key past each
    // table; the last segment stands in for it.
    const float ms = duration > 0.0f ? elapsed * (1000.0f / duration) : 1000.0f;
    int32_t index = static_cast<int32_t>(ms * 0.0059999996f);
    if (index > 5)
        index = 5;
    const float t = (ms - static_cast<float>(index) * 166.66667f) * 0.0059999996f;
    const float alpha = static_cast<float>(DrawAlpha(frame) * (f.base[SLOT_BLING] >> 24) / 255);
    const float key = (s_flashAlpha[index + 1] - s_flashAlpha[index]) * t + s_flashAlpha[index];
    FlashColor(frame) = (FlashColor(frame) & 0x00FFFFFFu) |
                        (static_cast<uint32_t>(static_cast<uint8_t>(static_cast<int32_t>(alpha * key))) << 24);
    const float angle = (s_flashRotation[index + 1] - s_flashRotation[index]) * t + s_flashRotation[index];
    const float scale = (s_flashScale[index + 1] - s_flashScale[index]) * t + s_flashScale[index];
    RotateQuadTexCoords(FlashTexCoords(frame), angle, scale);
}

// ---- textures -----------------------------------------------------------------------

// A setter's file: none (or "") is solid white for the slot's colour to tint;
// anything else loads by its path, and a file that does not load draws nothing.
void *LoadSlotTexture(const char *file) {
    if (file == nullptr || *file == '\0')
        return Texture::CreateSolid(0xFFFFFFFFu);
    return Texture::LoadFile(file);
}

// Fills `slot` with `file`, tinted `base`, or by the slot's colour as it
// stands when `base` is null.
void SetSlotTexture(void *frame, TextureSlot slot, const char *file, const uint32_t *base) {
    Fields &f = F(frame);
    void *&texture = SlotTexture(frame, slot);
    Texture::ReleaseHandle(texture);
    texture = LoadSlotTexture(file);
    if (base != nullptr)
        f.base[slot] = *base;
    f.texturesSet |= 1u << slot;
    NotifyLayer(frame, kSlotLayer[slot]);
    UpdateColors(frame);
}

// ---- the countdown numbers ----------------------------------------------------------

// Retail's switch for the numbers on every Cooldown (CVar *), off by default.
uint8_t *g_countdownCVar = nullptr;

// Screen units (what Lua's SetFont and SetPoint take) to DDC.
float ToDdc(float units) {
    return units / (At<float>(reinterpret_cast<void *>(Offsets::VAR_ASPECT_COMPENSATION), 0) * 1024.0f) *
           At<float>(reinterpret_cast<void *>(Offsets::VAR_DDC_WIDTH_SCALE), 0);
}

// The numbers' font string, as retail makes it (ClassicAPI's probe:
// Fonts\FRIZQT__.TTF 16 OUTLINE whatever the frame size, its CENTER on the
// frame's CENTER at (0, -1)), on the OVERLAY layer.
void *CreateCountdown(void *frame) {
    using GetData_t = void *(__fastcall *)(void *heap, void *, int, const char *, int);
    using Constructor_t = void *(__fastcall *)(void *fs, void *, void *frame, uint32_t layer, int show);
    using SetFont_t = int(__fastcall *)(void *fs, void *, const char *path, float height, uint32_t flags, int force);
    using SetPoint_t = void(__fastcall *)(void *layout, void *, int point, void *relative, int relativePoint, float x,
                                          float y, int flag);
    void *block = reinterpret_cast<GetData_t>(Offsets::FUN_DATA_ALLOCATOR_GET)(
        reinterpret_cast<void *>(Offsets::VAR_FONTSTRING_HEAP), nullptr, 0,
        reinterpret_cast<const char *>(Offsets::VAR_FONTSTRING_TYPE_NAME), -2);
    if (block == nullptr)
        return nullptr;
    void *fs = reinterpret_cast<Constructor_t>(Offsets::FUN_FONTSTRING_CONSTRUCTOR)(block, nullptr, frame,
                                                                                    Offsets::DRAWLAYER_OVERLAY, 1);
    const uint32_t flags = reinterpret_cast<uint32_t(__cdecl *)(const char *)>(Offsets::FUN_STRING_TO_FONT_FLAGS)("OUTLINE");
    if (reinterpret_cast<SetFont_t>(Offsets::FUN_FONTSTRING_SET_FONT)(fs, nullptr, "Fonts\\FRIZQT__.TTF", ToDdc(16.0f),
                                                                      flags, 0))
        At<uint32_t>(fs, Offsets::OFF_FONTSTRING_FONTABLE_FLAGS) &= ~1u;
    reinterpret_cast<SetPoint_t>(Offsets::FUN_LAYOUT_SET_POINT)(
        static_cast<uint8_t *>(fs) + Offsets::OFF_REGION_LAYOUT, nullptr, Offsets::FRAMEPOINT_CENTER,
        static_cast<uint8_t *>(frame) + Offsets::OFF_FRAME_LAYOUT, Offsets::FRAMEPOINT_CENTER, 0.0f, ToDdc(-1.0f), 1);
    return fs;
}

// Retail's text for `ms` left (ClassicAPI's probes: 2 s "2", 57 s "57",
// 116 s "1:56", 123 s "3m", 3698 s "2h", 89998 s "2d"; 6.49 s "7"): whole
// units rounded up; m:ss below the abbreviation threshold, which applies only
// from one minute to one hour; below the milliseconds threshold, tenths
// rounded up (threshold 5: 4.973 s "5.0", 3.24 s "3.3", 0.039 s "0.1").
//
// The ceilings are integer: in doubles 700 ms / 1000 * 10 rounds up to "0.8".
// The unit is picked on the rounded-up seconds, so 59.4 s reads "1:00", not
// "60", and 86399.5 s "1d", not "24h".
void FormatCountdown(char (&out)[16], uint32_t ms, float abbrevThreshold, float millisecondsThreshold) {
    auto ceilDiv = [](uint32_t n, uint32_t d) { return n / d + (n % d != 0 ? 1u : 0u); };
    if (ms / 1000.0 < millisecondsThreshold) {
        const uint32_t tenths = ceilDiv(ms, 100);
        std::snprintf(out, sizeof(out), "%u.%u", tenths / 10, tenths % 10);
        return;
    }
    const uint32_t s = ceilDiv(ms, 1000);
    if (s >= 86400)
        std::snprintf(out, sizeof(out), "%ud", ceilDiv(s, 86400));
    else if (s >= 3600)
        std::snprintf(out, sizeof(out), "%uh", ceilDiv(s, 3600));
    else if (s >= 60 && s >= std::fmin(abbrevThreshold, 3600.0f))
        std::snprintf(out, sizeof(out), "%um", ceilDiv(s, 60));
    else if (s >= 60)
        std::snprintf(out, sizeof(out), "%u:%02u", s / 60, s % 60);
    else
        std::snprintf(out, sizeof(out), "%u", s);
}

// The numbers while the sweep runs, nothing otherwise; SetText only when the
// text changes.
//
// A countdown formatter replaces FormatCountdown and both thresholds, and
// gets the seconds left unrounded; the gates stay (retail probe: "%.3f"
// showed "28.118" 1.882 s into a 30 s cooldown with the milliseconds
// threshold at 100, and nothing on a 1.5 s one). A format error shows no
// numbers. The formatter is held for the call: the format can collect, and
// a finalizer can replace this frame's formatter.
void UpdateCountdown(void *frame) {
    namespace Formatters = Text::NumericFormatter;
    Fields &f = F(frame);
    if (f.countdown == nullptr)
        return;
    const uint32_t duration = Duration(frame);
    const uint32_t elapsed = Elapsed(frame);
    const bool shown = !f.hideCountdownNumbers && !Flashing(frame) && !f.done && duration > f.minCountdownDuration &&
                       elapsed < duration && g_countdownCVar != nullptr &&
                       At<int32_t>(g_countdownCVar, Offsets::OFF_CVAR_INT_VALUE) != 0;
    char numbers[16] = "";
    const char *text = numbers;
    void *L = nullptr;
    int top = 0;
    Formatters::Formatter *formatter = f.countdownFormatter;
    if (shown && formatter != nullptr && (L = Game::Lua::State()) != nullptr) {
        top = Game::Lua::GetTop(L);
        Formatters::Retain(formatter);
        text = Formatters::Format(L, formatter, (duration - elapsed) / 1000.0);
        Formatters::Release(formatter);
        if (text == nullptr)
            text = "";
    } else if (shown) {
        FormatCountdown(numbers, duration - elapsed, f.abbrevThreshold, f.millisecondsThreshold);
    }
    if (f.countdownText != text) {
        f.countdownText.assign(text);
        reinterpret_cast<void(__fastcall *)(void *, void *, const char *, int)>(Offsets::FUN_FONTSTRING_SET_TEXT)(
            f.countdown, nullptr, text, 1);
    }
    if (L != nullptr)
        Game::Lua::SetTop(L, top);
}

// ---- the timer ----------------------------------------------------------------------

// CGCooldown::SetCooldown (0x005ECD70). A paused frame holds at the moment of
// the call, which CooldownFrame_SetDisplayAsPercentage relies on. The
// elapsed time is set as of now, so the numbers are right before the first
// update.
void SetCooldown(void *frame, uint32_t start, uint32_t duration, float modRate) {
    Fields &f = F(frame);
    const uint32_t now = NowMs();
    StartTime(frame) = start;
    Duration(frame) = duration;
    Elapsed(frame) = Reached(now, start) ? now - start : 0;
    Flashing(frame) = 0;
    f.modRate = modRate;
    f.done = false;
    if (f.paused)
        f.pausedAt = now;
    for (uint32_t slot = 0; slot < SLOT_COUNT; ++slot)
        if (!(f.texturesSet & (1u << slot)) && SlotTexture(frame, static_cast<TextureSlot>(slot)) == nullptr) {
            SlotTexture(frame, static_cast<TextureSlot>(slot)) = Texture::LoadFile(kSlotDefaultFile[slot]);
            NotifyLayer(frame, kSlotLayer[slot]);
        }
    UpdateColors(frame);
    ResetVerts(frame);
    UpdateCountdown(frame);
    Show(frame);
}

// Ends the cooldown without the bling or OnCooldownDone.
void Clear(void *frame) {
    StartTime(frame) = 0;
    Duration(frame) = 0;
    Elapsed(frame) = 0;
    Flashing(frame) = 0;
    F(frame).done = true;
    UpdateCountdown(frame);
    Hide(frame);
}

// Resuming moves the start past the paused span, so the sweep (or the
// bling) picks up where it stopped.
void SetPaused(void *frame, bool paused) {
    Fields &f = F(frame);
    if (paused == f.paused)
        return;
    const uint32_t now = NowMs();
    if (paused)
        f.pausedAt = now;
    else
        StartTime(frame) += now - f.pausedAt;
    f.paused = paused;
}

void RunOnCooldownDone(void *frame) {
    const ScriptIx &script = F(frame).onCooldownDone;
    if (script.luaRef == 0)
        return;
    reinterpret_cast<void(__fastcall *)(void *, void *, const ScriptIx *, int, const char *)>(
        Offsets::FUN_FRAMESCRIPT_OBJECT_RUN_SCRIPT)(frame, nullptr, &script, 0, nullptr);
}

// ---- the overrides ------------------------------------------------------------------

int g_metaTable = 0;

// An engine vtable copied with the dword in front of it (where MSVC keeps
// the RTTI locator), as the engine lays its vtables out.
template <size_t N> struct VTable {
    const void *locator;
    const void *slots[N];
    bool Built() const { return slots[0] != nullptr; }
    void CopyFrom(uintptr_t address) { std::memcpy(this, reinterpret_cast<const void *>(address - 4), sizeof(*this)); }
    void Set(uintptr_t slot, const void *fn) { slots[slot / 4] = fn; }
};
VTable<Offsets::VTBL_COOLDOWN_SLOTS> g_vtable;
VTable<Offsets::VTBL_COOLDOWN_LAYOUT_SLOTS> g_layoutVtable;

void *__fastcall DeletingDestructor(void *self, void *, uint32_t flags) {
    if (Fields *f = FieldsOf(self)) {
        Texture::ReleaseHandle(f->blingTexture);
        Text::NumericFormatter::Release(f->countdownFormatter);
        reinterpret_cast<void(__fastcall *)(ScriptIx *, void *)>(Offsets::FUN_SCRIPTIX_DESTRUCTOR)(&f->onCooldownDone,
                                                                                               nullptr);
        delete f;
        FieldsOf(self) = nullptr;
    }
    reinterpret_cast<void(__fastcall *)(void *, void *)>(Offsets::FUN_COOLDOWN_DESTRUCTOR)(self, nullptr);
    if ((flags & 1) != 0)
        reinterpret_cast<int(__stdcall *)(void *, const char *, int, int)>(Offsets::FUN_STORM_SMEM_FREE)(
            self, __FILE__, __LINE__, 0);
    return self;
}

void *__fastcall LayoutDeletingDestructor(void *layout, void *, uint32_t flags) {
    return DeletingDestructor(FrameOfLayout(layout), nullptr, flags);
}

int __fastcall GetScriptMetaTable(void *, void *) { return g_metaTable; }

ScriptIx *__fastcall GetScriptByName(void *self, void *, const char *name, ScriptData *data) {
    if (name != nullptr && _stricmp(name, "OnCooldownDone") == 0)
        return &F(self).onCooldownDone;
    return reinterpret_cast<ScriptIx *(__fastcall *)(void *, void *, const char *, ScriptData *)>(
        Offsets::FUN_SIMPLEFRAME_GET_SCRIPT_BY_NAME)(self, nullptr, name, data);
}

// CGCooldown::OnLayerUpdate (0x005EC8F0) with the changes in the file header.
void __fastcall OnLayerUpdate(void *self, void *, float elapsedSec) {
    reinterpret_cast<void(__fastcall *)(void *, void *, float)>(Offsets::FUN_SIMPLEFRAME_ON_LAYER_UPDATE)(
        self, nullptr, elapsedSec);
    Fields &f = F(self);
    const uint32_t now = f.paused ? f.pausedAt : NowMs();
    const uint32_t start = StartTime(self);
    const uint32_t duration = Duration(self);
    Elapsed(self) = Reached(now, start) ? now - start : 0;
    bool finished = false;
    if (Elapsed(self) >= duration) {
        if (Flashing(self)) {
            Flashing(self) = 0;
            Hide(self);
        } else if (Reverse(self) && duration != 0) {
            // The engine's hold, fully covered, until the addon hides it.
            Elapsed(self) = duration - 1;
            finished = !f.done;
            f.done = true;
        } else if (f.done || duration == 0) {
            // Shown again after the end or a Clear, or a zero-length timer:
            // no cooldown ended, so no bling and no OnCooldownDone.
            f.done = true;
            Hide(self);
        } else {
            f.done = true;
            finished = true;
            if (f.drawBling) {
                const float bling = 1000.0f * f.modRate;
                StartTime(self) = now;
                Duration(self) = bling >= 1.0f ? static_cast<uint32_t>(bling) : 1;
                Elapsed(self) = 0;
                Flashing(self) = 1;
                UpdateColors(self);
            } else {
                Hide(self);
            }
        }
    }
    AnimateVerts(self);
    UpdateCountdown(self);
    // Last: the handler may start another cooldown on this frame.
    if (finished)
        RunOnCooldownDone(self);
}

// CGCooldown::OnFrameRender (0x005EBD20), with the bling its own texture
// and the swipe and edge behind their flags.
void __fastcall OnFrameRender(void *self, void *, void *batch, uint32_t layer) {
    using Queue_t = void(__fastcall *)(void *batch, void *, void *texture, int blend, int posCount, const void *pos,
                                       const void *texCoord, int colorCount, const void *color, int idxCount,
                                       const void *indices, void *shader);
    reinterpret_cast<void(__fastcall *)(void *, void *, void *, uint32_t)>(Offsets::FUN_SIMPLEFRAME_ON_FRAME_RENDER)(
        self, nullptr, batch, layer);
    const auto queue = reinterpret_cast<Queue_t>(Offsets::FUN_RENDERBATCH_QUEUE);
    void *shader = reinterpret_cast<void *(__cdecl *)(int)>(Offsets::FUN_IMAGE_MODE_PIXEL_SHADER)(0);
    const auto indices = [](uintptr_t address) { return reinterpret_cast<const void *>(address); };
    const Fields &f = F(self);
    if (layer == Offsets::DRAWLAYER_ARTWORK) {
        if (void *swipe = SlotTexture(self, SLOT_SWIPE); swipe != nullptr && f.drawSwipe)
            queue(batch, nullptr, swipe, Offsets::GXBLEND_ALPHA, 10, StarVerts(self), StarTexCoords(self), 0,
                  &StarColor(self), 24, indices(Offsets::VAR_COOLDOWN_STAR_INDICES), shader);
        if (f.blingTexture != nullptr)
            queue(batch, nullptr, f.blingTexture, Offsets::GXBLEND_ADD, 4,
                  &At<C3Vector>(self, Offsets::OFF_COOLDOWN_FLASH_VERTS), FlashTexCoords(self), 0,
                  &FlashColor(self), 6, indices(Offsets::VAR_COOLDOWN_FLASH_INDICES), shader);
    } else if (layer == Offsets::DRAWLAYER_OVERLAY) {
        if (void *edge = SlotTexture(self, SLOT_EDGE); edge != nullptr && DrawEdge(self))
            queue(batch, nullptr, edge, Offsets::GXBLEND_OPAQUE, 4, &At<C3Vector>(self, Offsets::OFF_COOLDOWN_EDGE_VERTS),
                  EdgeTexCoords(self), 0, &EdgeColor(self), 6, indices(Offsets::VAR_COOLDOWN_EDGE_INDICES), shader);
    }
}

// CGCooldown::UpdateAlpha (0x005EC8C0).
void __fastcall UpdateAlpha(void *self, void *) {
    reinterpret_cast<void(__fastcall *)(void *, void *)>(Offsets::FUN_SIMPLEFRAME_UPDATE_ALPHA)(self, nullptr);
    UpdateColors(self);
}

// ---- XML ----------------------------------------------------------------------------

bool FlagAttr(const void *node, const char *name, bool &out) {
    const char *value = Xml::Attr(node, name);
    if (value == nullptr || *value == '\0')
        return false;
    out = reinterpret_cast<int(__cdecl *)(const char *)>(Offsets::FUN_STRING_TO_BOOL)(value) != 0;
    return true;
}

// A texture element's <Color> into `color`; false when it has none.
bool XmlTextureColor(const void *node, void *status, uint32_t &color) {
    const void *c = Xml::Child(node, "Color");
    if (c == nullptr)
        return false;
    reinterpret_cast<int(__cdecl *)(const void *, uint32_t &, void *)>(Offsets::FUN_XML_LOAD_COLOR)(c, color, status);
    return true;
}

// CGCooldown::LoadXML (`reverse`, `drawEdge`), then UI.xsd's CooldownType:
// `drawSwipe`, `drawBling`, `rotation` (degrees) and the <SwipeTexture>,
// <EdgeTexture> and <BlingTexture> elements (a `file` and a <Color>).
void __fastcall LoadXML(void *layout, void *, const void *node, void *status) {
    reinterpret_cast<void(__fastcall *)(void *, void *, const void *, void *)>(Offsets::FUN_COOLDOWN_LOAD_XML)(
        layout, nullptr, node, status);
    void *frame = FrameOfLayout(layout);
    if (node == nullptr)
        return;
    Fields &f = F(frame);
    FlagAttr(node, "drawSwipe", f.drawSwipe);
    FlagAttr(node, "drawBling", f.drawBling);
    FlagAttr(node, "hideCountdownNumbers", f.hideCountdownNumbers);
    if (const char *minimum = Xml::Attr(node, "minimumCountdownDuration"); minimum != nullptr && *minimum != '\0')
        f.minCountdownDuration = ClampMs(std::strtod(minimum, nullptr));
    if (const char *rotation = Xml::Attr(node, "rotation"); rotation != nullptr && *rotation != '\0')
        f.rotation = reinterpret_cast<float(__stdcall *)(const char *)>(Offsets::FUN_SSTR_TO_FLOAT)(rotation) *
                     (3.14159265f / 180.0f);
    for (const void *c = Xml::FirstChild(node); c != nullptr; c = Xml::NextSibling(c)) {
        for (uint32_t slot = 0; slot < SLOT_COUNT; ++slot) {
            if (!Xml::TagIs(c, kSlotTag[slot]))
                continue;
            uint32_t color = 0;
            const bool hasColor = XmlTextureColor(c, status, color);
            SetSlotTexture(frame, static_cast<TextureSlot>(slot), Xml::Attr(c, "file"), hasColor ? &color : nullptr);
        }
    }
}

// ---- the class ----------------------------------------------------------------------

void BuildVTables() {
    if (g_vtable.Built())
        return;
    auto fn = [](auto f) { return reinterpret_cast<const void *>(f); };
    g_vtable.CopyFrom(Offsets::VTBL_COOLDOWN);
    g_layoutVtable.CopyFrom(Offsets::VTBL_COOLDOWN_LAYOUT);
    g_vtable.Set(Offsets::VTBL_FRAME_DELETING_DESTRUCTOR, fn(&DeletingDestructor));
    g_vtable.Set(Offsets::VTBL_FRAME_GET_SCRIPT_META_TABLE, fn(&GetScriptMetaTable));
    g_vtable.Set(Offsets::VTBL_FRAME_GET_SCRIPT_BY_NAME, fn(&GetScriptByName));
    g_vtable.Set(Offsets::VTBL_FRAME_ON_LAYER_UPDATE, fn(&OnLayerUpdate));
    g_vtable.Set(Offsets::VTBL_FRAME_ON_FRAME_RENDER, fn(&OnFrameRender));
    g_vtable.Set(Offsets::VTBL_FRAME_UPDATE_ALPHA, fn(&UpdateAlpha));
    g_layoutVtable.Set(Offsets::VTBL_LAYOUT_DELETING_DESTRUCTOR, fn(&LayoutDeletingDestructor));
    g_layoutVtable.Set(Offsets::VTBL_LAYOUT_LOAD_XML, fn(&LoadXML));
}

using Create_t = void *(__cdecl *)(void *parent);
Create_t g_createOriginal = nullptr;

// The "Cooldown" factory (CGCooldown::Create), shaped like the engine's: the
// object zero-filled, as the engine's allocator hands it out, then the
// engine's constructor and our vtables.
void *__cdecl Create(void *parent) {
    BuildVTables();
    void *frame = reinterpret_cast<void *(__stdcall *)(uint32_t, const char *, int, int)>(
        Offsets::FUN_STORM_SMEM_ALLOC)(static_cast<uint32_t>(kObjectSize), __FILE__, __LINE__, 0);
    if (frame == nullptr)
        return nullptr;
    std::memset(frame, 0, kObjectSize);
    reinterpret_cast<void(__fastcall *)(void *, void *, void *)>(Offsets::FUN_COOLDOWN_CONSTRUCTOR)(frame, nullptr,
                                                                                                 parent);
    At<const void *>(frame, 0) = g_vtable.slots;
    At<const void *>(frame, Offsets::OFF_FRAME_LAYOUT) = g_layoutVtable.slots;
    Fields *f = new Fields;
    // CGCooldown::UpdateColors' constants.
    f->base[SLOT_SWIPE] = Bgra(0, 0, 0, 160);
    f->base[SLOT_EDGE] = Bgra(0xFF, 0xFF, 0xFF, 0xFF);
    f->base[SLOT_BLING] = Bgra(0x50, 0xA0, 0xFF, 0xFF);
    FieldsOf(frame) = f;
    f->countdown = CreateCountdown(frame);
    return frame;
}

const Game::HookAutoRegister _createHook{Offsets::FUN_COOLDOWN_CREATE, reinterpret_cast<void *>(&Create),
                                         reinterpret_cast<void **>(&g_createOriginal)};

// ---- the methods --------------------------------------------------------------------

void *Self(void *L) { return UI::FrameObject::ResolveSelf(L, TypeId()); }

const char *FrameName(void *frame) {
    const char *name = reinterpret_cast<const char *(__fastcall *)(void *, void *)>(
        At<const void *const *>(frame, 0)[Offsets::VTBL_FRAME_GET_NAME / 4])(frame, nullptr);
    return name != nullptr && *name != '\0' ? name : "<unnamed>";
}

bool Flag(void *L, int index) { return reinterpret_cast<int(__cdecl *)(void *, int, int)>(Offsets::FUN_LUA_TO_BOOL)(L, index, 1) != 0; }

// Seconds to ms on the GetTime() clock, as Script_SetCooldown converts them.
uint32_t Ms(double seconds) { return static_cast<uint32_t>(static_cast<int64_t>(seconds * 1000.0)); }

float ModRateArg(void *L, int index) {
    return Game::Lua::IsNumber(L, index) ? static_cast<float>(Game::Lua::ToNumber(L, index)) : 1.0f;
}

bool NumberArgs(void *L, void *frame, int first, int count, const char *usage) {
    for (int i = 0; i < count; ++i) {
        if (!Game::Lua::IsNumber(L, first + i)) {
            Game::Lua::Error(L, "Usage: %s:%s", FrameName(frame), usage);
            return false;
        }
    }
    return true;
}

// Script_SetCooldown (0x005ECE80), with retail's modRate.
int __cdecl Script_SetCooldown(void *L) {
    void *frame = Self(L);
    if (frame == nullptr || !NumberArgs(L, frame, 2, 2, "SetCooldown(start, duration[, modRate])"))
        return 0;
    SetCooldown(frame, Ms(Game::Lua::ToNumber(L, 2)), Ms(Game::Lua::ToNumber(L, 3)), ModRateArg(L, 4));
    return 0;
}

int __cdecl Script_SetCooldownDuration(void *L) {
    void *frame = Self(L);
    if (frame == nullptr || !NumberArgs(L, frame, 2, 1, "SetCooldownDuration(duration[, modRate])"))
        return 0;
    SetCooldown(frame, NowMs(), Ms(Game::Lua::ToNumber(L, 2)), ModRateArg(L, 3));
    return 0;
}

// The start that makes the sweep end at expirationTime.
int __cdecl Script_SetCooldownFromExpirationTime(void *L) {
    void *frame = Self(L);
    if (frame == nullptr ||
        !NumberArgs(L, frame, 2, 2, "SetCooldownFromExpirationTime(expirationTime, duration[, modRate])"))
        return 0;
    const double duration = Game::Lua::ToNumber(L, 3);
    SetCooldown(frame, Ms(Game::Lua::ToNumber(L, 2) - duration), Ms(duration), ModRateArg(L, 4));
    return 0;
}

int __cdecl Script_Clear(void *L) {
    if (void *frame = Self(L))
        Clear(frame);
    return 0;
}

// The timer as it stands, in ms: the cooldown's, then the bling's once it ends.
int __cdecl Script_GetCooldownTimes(void *L) {
    void *frame = Self(L);
    if (frame == nullptr)
        return 0;
    Game::Lua::PushNumber(L, static_cast<double>(StartTime(frame)));
    Game::Lua::PushNumber(L, static_cast<double>(Duration(frame)));
    return 2;
}

int __cdecl Script_GetCooldownDuration(void *L) {
    void *frame = Self(L);
    if (frame == nullptr)
        return 0;
    Game::Lua::PushNumber(L, static_cast<double>(Duration(frame)));
    return 1;
}

int __cdecl Script_GetCooldownDisplayDuration(void *L) {
    void *frame = Self(L);
    if (frame == nullptr)
        return 0;
    const double duration = static_cast<double>(Duration(frame));
    const float modRate = F(frame).modRate;
    Game::Lua::PushNumber(L, modRate > 0.0f ? duration / modRate : duration);
    return 1;
}

int __cdecl Script_Pause(void *L) {
    if (void *frame = Self(L))
        SetPaused(frame, true);
    return 0;
}

int __cdecl Script_Resume(void *L) {
    if (void *frame = Self(L))
        SetPaused(frame, false);
    return 0;
}

int __cdecl Script_SetPaused(void *L) {
    if (void *frame = Self(L))
        SetPaused(frame, Flag(L, 2));
    return 0;
}

int __cdecl Script_IsPaused(void *L) {
    void *frame = Self(L);
    if (frame == nullptr)
        return 0;
    Game::Lua::PushBool(L, F(frame).paused);
    return 1;
}

// Script_SetDrawEdge (0x005ECA30), with the edge quad built and its layer
// rebuilt now; the engine's waits for the next SetCooldown.
int __cdecl Script_SetDrawEdge(void *L) {
    void *frame = Self(L);
    if (frame == nullptr)
        return 0;
    const uint32_t drawEdge = Flag(L, 2) ? 1 : 0;
    if (drawEdge != DrawEdge(frame)) {
        DrawEdge(frame) = drawEdge;
        ResetVerts(frame);
        NotifyLayer(frame, kSlotLayer[SLOT_EDGE]);
    }
    return 0;
}

int __cdecl Script_SetDrawSwipe(void *L) {
    void *frame = Self(L);
    if (frame == nullptr)
        return 0;
    Fields &f = F(frame);
    const bool drawSwipe = Flag(L, 2);
    if (drawSwipe != f.drawSwipe) {
        f.drawSwipe = drawSwipe;
        NotifyLayer(frame, kSlotLayer[SLOT_SWIPE]);
    }
    return 0;
}

int __cdecl Script_GetDrawSwipe(void *L) {
    void *frame = Self(L);
    if (frame == nullptr)
        return 0;
    Game::Lua::PushBool(L, F(frame).drawSwipe);
    return 1;
}

int __cdecl Script_SetDrawBling(void *L) {
    if (void *frame = Self(L))
        F(frame).drawBling = Flag(L, 2);
    return 0;
}

int __cdecl Script_GetDrawBling(void *L) {
    void *frame = Self(L);
    if (frame == nullptr)
        return 0;
    Game::Lua::PushBool(L, F(frame).drawBling);
    return 1;
}

// (r, g, b[, a]) into a slot's colour; a missing alpha is opaque.
template <TextureSlot Slot> int __cdecl Script_SetColor(void *L) {
    static const char *const kUsage[SLOT_COUNT] = {"SetSwipeColor(r, g, b[, a])", "SetEdgeColor(r, g, b[, a])",
                                                   nullptr};
    void *frame = Self(L);
    if (frame == nullptr || !NumberArgs(L, frame, 2, 3, kUsage[Slot]))
        return 0;
    const double a = Game::Lua::IsNumber(L, 5) ? Game::Lua::ToNumber(L, 5) : 1.0;
    F(frame).base[Slot] = Bgra(ColorByte(Game::Lua::ToNumber(L, 2)), ColorByte(Game::Lua::ToNumber(L, 3)),
                               ColorByte(Game::Lua::ToNumber(L, 4)), ColorByte(a));
    UpdateColors(frame);
    return 0;
}

// (texture[, r, g, b[, a]]): without r, g, b the colour stays; "" is a solid colour.
template <TextureSlot Slot> int __cdecl Script_SetTexture(void *L) {
    static const char *const kUsage[SLOT_COUNT] = {"SetSwipeTexture(texture[, r, g, b, a])",
                                                   "SetEdgeTexture(texture[, r, g, b, a])",
                                                   "SetBlingTexture(texture[, r, g, b, a])"};
    void *frame = Self(L);
    if (frame == nullptr)
        return 0;
    if (!Game::Lua::IsString(L, 2)) {
        Game::Lua::Error(L, "Usage: %s:%s", FrameName(frame), kUsage[Slot]);
        return 0;
    }
    const bool hasColor = Game::Lua::IsNumber(L, 3) && Game::Lua::IsNumber(L, 4) && Game::Lua::IsNumber(L, 5);
    const uint32_t color =
        hasColor ? Bgra(ColorByte(Game::Lua::ToNumber(L, 3)), ColorByte(Game::Lua::ToNumber(L, 4)),
                        ColorByte(Game::Lua::ToNumber(L, 5)),
                        Game::Lua::IsNumber(L, 6) ? ColorByte(Game::Lua::ToNumber(L, 6)) : 0xFFu)
                 : 0;
    SetSlotTexture(frame, Slot, Game::Lua::ToString(L, 2), hasColor ? &color : nullptr);
    return 0;
}

int __cdecl Script_SetRotation(void *L) {
    void *frame = Self(L);
    if (frame == nullptr || !NumberArgs(L, frame, 2, 1, "SetRotation(radians)"))
        return 0;
    F(frame).rotation = static_cast<float>(Game::Lua::ToNumber(L, 2));
    return 0;
}

int __cdecl Script_GetRotation(void *L) {
    void *frame = Self(L);
    if (frame == nullptr)
        return 0;
    Game::Lua::PushNumber(L, F(frame).rotation);
    return 1;
}

int __cdecl Script_GetCountdownFontString(void *L) {
    void *frame = Self(L);
    if (frame == nullptr)
        return 0;
    if (void *fs = F(frame).countdown)
        UI::FrameObject::Push(L, fs);
    else
        Game::Lua::PushNil(L);
    return 1;
}

int __cdecl Script_SetHideCountdownNumbers(void *L) {
    if (void *frame = Self(L))
        F(frame).hideCountdownNumbers = Flag(L, 2);
    return 0;
}

int __cdecl Script_GetHideCountdownNumbers(void *L) {
    void *frame = Self(L);
    if (frame == nullptr)
        return 0;
    Game::Lua::PushBool(L, F(frame).hideCountdownNumbers);
    return 1;
}

// A number setter and getter over one Fields member.
template <auto Member, const char *Usage> int __cdecl Script_SetNumber(void *L) {
    void *frame = Self(L);
    if (frame == nullptr || !NumberArgs(L, frame, 2, 1, Usage))
        return 0;
    const double value = Game::Lua::ToNumber(L, 2);
    auto &field = F(frame).*Member;
    if constexpr (std::is_same_v<std::remove_reference_t<decltype(field)>, uint32_t>)
        field = ClampMs(value);
    else
        field = static_cast<float>(value);
    return 0;
}

template <auto Member> int __cdecl Script_GetNumber(void *L) {
    void *frame = Self(L);
    if (frame == nullptr)
        return 0;
    Game::Lua::PushNumber(L, static_cast<double>(F(frame).*Member));
    return 1;
}

// The frame shares the formatter: a later SetBreakpoints on it shows (retail
// probe). The text follows at the next update.
int __cdecl Script_SetCountdownFormatter(void *L) {
    namespace Formatters = Text::NumericFormatter;
    void *frame = Self(L);
    if (frame == nullptr)
        return 0;
    Formatters::Formatter *formatter = nullptr;
    if (Game::Lua::Type(L, 2) > Game::Lua::TYPE_NIL) {
        formatter = Formatters::ToFormatter(L, 2);
        if (formatter == nullptr) {
            Game::Lua::Error(L, "Usage: %s:SetCountdownFormatter([formatter])", FrameName(frame));
            return 0;
        }
        Formatters::Retain(formatter);
    }
    Fields &f = F(frame);
    Formatters::Release(f.countdownFormatter);
    f.countdownFormatter = formatter;
    return 0;
}

// A new Lua object for the formatter each call, as retail's (probe:
// rawequal(GetCountdownFormatter(), formatter) is false).
int __cdecl Script_GetCountdownFormatter(void *L) {
    void *frame = Self(L);
    if (frame == nullptr)
        return 0;
    if (Text::NumericFormatter::Formatter *formatter = F(frame).countdownFormatter)
        Text::NumericFormatter::Push(L, formatter);
    else
        Game::Lua::PushNil(L);
    return 1;
}

constexpr char kUsageAbbrev[] = "SetCountdownAbbrevThreshold(seconds)";
constexpr char kUsageMilliseconds[] = "SetCountdownMillisecondsThreshold(seconds)";
constexpr char kUsageMinimum[] = "SetMinimumCountdownDuration(milliseconds)";

// After CGCooldown's own five, so SetCooldown and SetDrawEdge replace the engine's.
const Frame::Methods::Entry kMethods[] = {
    {"SetCooldown", &Script_SetCooldown},
    {"SetCooldownDuration", &Script_SetCooldownDuration},
    {"SetCooldownFromExpirationTime", &Script_SetCooldownFromExpirationTime},
    {"Clear", &Script_Clear},
    {"GetCooldownTimes", &Script_GetCooldownTimes},
    {"GetCooldownDuration", &Script_GetCooldownDuration},
    {"GetCooldownDisplayDuration", &Script_GetCooldownDisplayDuration},
    {"Pause", &Script_Pause},
    {"Resume", &Script_Resume},
    {"SetPaused", &Script_SetPaused},
    {"IsPaused", &Script_IsPaused},
    {"SetDrawEdge", &Script_SetDrawEdge},
    {"SetDrawSwipe", &Script_SetDrawSwipe},
    {"GetDrawSwipe", &Script_GetDrawSwipe},
    {"SetDrawBling", &Script_SetDrawBling},
    {"GetDrawBling", &Script_GetDrawBling},
    {"SetSwipeColor", &Script_SetColor<SLOT_SWIPE>},
    {"SetEdgeColor", &Script_SetColor<SLOT_EDGE>},
    {"SetSwipeTexture", &Script_SetTexture<SLOT_SWIPE>},
    {"SetEdgeTexture", &Script_SetTexture<SLOT_EDGE>},
    {"SetBlingTexture", &Script_SetTexture<SLOT_BLING>},
    {"SetRotation", &Script_SetRotation},
    {"GetRotation", &Script_GetRotation},
    {"GetCountdownFontString", &Script_GetCountdownFontString},
    {"SetHideCountdownNumbers", &Script_SetHideCountdownNumbers},
    {"GetHideCountdownNumbers", &Script_GetHideCountdownNumbers},
    {"SetCountdownAbbrevThreshold", &Script_SetNumber<&Fields::abbrevThreshold, kUsageAbbrev>},
    {"GetCountdownAbbrevThreshold", &Script_GetNumber<&Fields::abbrevThreshold>},
    {"SetCountdownMillisecondsThreshold", &Script_SetNumber<&Fields::millisecondsThreshold, kUsageMilliseconds>},
    {"GetCountdownMillisecondsThreshold", &Script_GetNumber<&Fields::millisecondsThreshold>},
    {"SetMinimumCountdownDuration", &Script_SetNumber<&Fields::minCountdownDuration, kUsageMinimum>},
    {"GetMinimumCountdownDuration", &Script_GetNumber<&Fields::minCountdownDuration>},
    {"SetCountdownFormatter", &Script_SetCountdownFormatter},
    {"GetCountdownFormatter", &Script_GetCountdownFormatter},
};

void __cdecl RegisterScriptMethods(void *L) {
    reinterpret_cast<void(__cdecl *)(void *)>(Offsets::FUN_COOLDOWN_REGISTER_SCRIPT_METHODS)(L);
    reinterpret_cast<void(__cdecl *)(void *, const Frame::Methods::Entry *, int)>(Offsets::FUN_REGISTER_FRAME_METHODS)(
        L, kMethods, static_cast<int>(sizeof(kMethods) / sizeof(kMethods[0])));
}

void RegisterLuaFunctions() {
    // As Lua's RegisterCVar registers one; a value already set (Config.wtf,
    // or this session before a /reload) stays.
    using Register_t = uint8_t *(__cdecl *)(const char *, const char *, uint32_t, const char *, void *, uint32_t, bool,
                                            void *, bool);
    g_countdownCVar = reinterpret_cast<Register_t>(Offsets::FUN_CVAR_REGISTER)(
        "countdownForCooldowns",
        "Whether to use number countdown instead of radial swipe for action button cooldowns or not.", 0, "0",
        nullptr, Offsets::CVAR_CATEGORY_GAME, true, nullptr, true);
    using CreateMeta_t = int(__cdecl *)(void *, void(__cdecl *)(void *));
    g_metaTable = reinterpret_cast<CreateMeta_t>(Offsets::FUN_FRAMESCRIPT_CREATE_SCRIPT_META_TABLE)(
        Game::Lua::State(), &RegisterScriptMethods);
}

// The metatable is a ref in the Lua state the /reload closes; every Cooldown
// is destroyed before it closes, and the next state builds its own.
void PrepareForReload() { g_metaTable = 0; }

const Game::ModuleAutoRegister _autoreg{&RegisterLuaFunctions};
const Game::ReloadAutoRegister _reload{&PrepareForReload};

} // namespace

} // namespace Frame::Cooldown
