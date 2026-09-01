// This file is part of WrathClassicAPI.
//
// WrathClassicAPI is free software: you can redistribute it and/or modify it under the terms
// of the GNU Lesser General Public License as published by the Free Software Foundation, either
// version 3 of the License, or (at your option) any later version.
//
// WrathClassicAPI is distributed in the hope that it will be useful, but WITHOUT ANY
// WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE. See the GNU Lesser General Public License for more details.
//
// You should have received a copy of the GNU Lesser General Public License along with
// WrathClassicAPI. If not, see <https://www.gnu.org/licenses/>.

// `C_ColorUtil` — the color-space + text-color-code helpers. Every one is pure
// math / string formatting (no engine state), so this is a plain implementation
// rather than a hook.
//
// Conventions (as the modern ColorMixin in Color.lua expects them):
//   - HSV/HSL hue is in DEGREES, [0, 360); saturation / value / lightness are
//     [0, 1]. Achromatic (gray) inputs yield hue = -1 (a sentinel, not 0).
//   - GenerateTextColorCode(color) returns the BARE 8-hex string "AARRGGBB"
//     (e.g. red -> "ffff0000"), alpha forced to ff (the arg type is colorRGB,
//     no alpha channel).
//   - WrapTextInColorCode(text, code) returns "|c" .. code .. text .. "|r".
//   - WrapTextInColor(text, color) = WrapTextInColorCode(text,
//     GenerateTextColorCode(color)) -> "|cffff0000Hi|r".
//
// The color arg is a table read by raw r/g/b fields (ColorMixin-shaped); an
// optional a field is honored (default 1.0) though the colorRGB type omits it —
// for the documented no-alpha case both give ff.

#include "Game.h"

#include <cmath>
#include <cstdio>
#include <string>

namespace Color::Util {

namespace {

double Clamp01(double v) { return v < 0.0 ? 0.0 : (v > 1.0 ? 1.0 : v); }

int ToByte(double v) {
    const double c = Clamp01(v);
    return static_cast<int>(c * 255.0 + 0.5); // round to nearest
}

double Max3(double a, double b, double c) {
    double m = a > b ? a : b;
    return m > c ? m : c;
}
double Min3(double a, double b, double c) {
    double m = a < b ? a : b;
    return m < c ? m : c;
}
double MinLuma(double l) { return l < 1.0 - l ? l : 1.0 - l; }

// --- Pure conversions --------------------------------------------------

void RgbToHsv(double r, double g, double b, double &h, double &s, double &v) {
    const double mx = Max3(r, g, b), mn = Min3(r, g, b), d = mx - mn;
    v = mx;
    s = (mx <= 0.0) ? 0.0 : d / mx;
    if (d <= 0.0) { // achromatic
        h = -1.0;
        return;
    }
    if (mx == r)
        h = 60.0 * std::fmod((g - b) / d, 6.0);
    else if (mx == g)
        h = 60.0 * ((b - r) / d + 2.0);
    else
        h = 60.0 * ((r - g) / d + 4.0);
    if (h < 0.0)
        h += 360.0;
}

void HsvToRgb(double h, double s, double v, double &r, double &g, double &b) {
    if (s <= 0.0 || h < 0.0) { // achromatic (incl. the -1 sentinel)
        r = g = b = v;
        return;
    }
    h = std::fmod(h, 360.0);
    if (h < 0.0)
        h += 360.0;
    const double hh = h / 60.0;
    const int i = static_cast<int>(std::floor(hh));
    const double f = hh - i;
    const double p = v * (1.0 - s);
    const double q = v * (1.0 - s * f);
    const double t = v * (1.0 - s * (1.0 - f));
    switch (i % 6) {
    case 0: r = v; g = t; b = p; break;
    case 1: r = q; g = v; b = p; break;
    case 2: r = p; g = v; b = t; break;
    case 3: r = p; g = q; b = v; break;
    case 4: r = t; g = p; b = v; break;
    default: r = v; g = p; b = q; break;
    }
}

void HsvToHsl(double h, double sv, double v, double &ho, double &sl, double &l) {
    ho = h; // hue (incl. -1) passes through unchanged
    l = v * (1.0 - sv / 2.0);
    sl = (l <= 0.0 || l >= 1.0) ? 0.0 : (v - l) / MinLuma(l);
}

void HslToHsv(double h, double sl, double l, double &ho, double &sv, double &v) {
    ho = h;
    v = l + sl * MinLuma(l);
    sv = (v <= 0.0) ? 0.0 : 2.0 * (1.0 - l / v);
}

void HslToRgb(double h, double s, double l, double &r, double &g, double &b) {
    if (s <= 0.0 || h < 0.0) { // achromatic
        r = g = b = l;
        return;
    }
    h = std::fmod(h, 360.0);
    if (h < 0.0)
        h += 360.0;
    const double c = (1.0 - std::fabs(2.0 * l - 1.0)) * s;
    const double hh = h / 60.0;
    const double x = c * (1.0 - std::fabs(std::fmod(hh, 2.0) - 1.0));
    const double m = l - c / 2.0;
    double r1, g1, b1;
    switch (static_cast<int>(std::floor(hh)) % 6) {
    case 0: r1 = c; g1 = x; b1 = 0.0; break;
    case 1: r1 = x; g1 = c; b1 = 0.0; break;
    case 2: r1 = 0.0; g1 = c; b1 = x; break;
    case 3: r1 = 0.0; g1 = x; b1 = c; break;
    case 4: r1 = x; g1 = 0.0; b1 = c; break;
    default: r1 = c; g1 = 0.0; b1 = x; break;
    }
    r = r1 + m;
    g = g1 + m;
    b = b1 + m;
}

// --- Lua glue ----------------------------------------------------------

// Read arg `n` as a number, or raise the usage error.
double Arg(void *L, int n, const char *usage) {
    if (!Game::Lua::IsNumber(L, n)) {
        Game::Lua::Error(L, "%s", usage);
        return 0.0; // unreachable
    }
    return Game::Lua::ToNumber(L, n);
}

// Read a numeric field from the color table at (absolute, positive) index
// `idx`. Raw access — ColorMixin stores r/g/b/a as direct keys.
double ColorField(void *L, int idx, const char *key, double dflt) {
    Game::Lua::PushString(L, key);
    Game::Lua::RawGet(L, idx);
    const double v = Game::Lua::IsNumber(L, -1) ? Game::Lua::ToNumber(L, -1) : dflt;
    Game::Lua::SetTop(L, -2); // pop the value
    return v;
}

// "AARRGGBB" from a color table at `idx`, alpha default ff.
std::string HexFromColor(void *L, int idx) {
    const int a = ToByte(ColorField(L, idx, "a", 1.0));
    const int r = ToByte(ColorField(L, idx, "r", 0.0));
    const int g = ToByte(ColorField(L, idx, "g", 0.0));
    const int b = ToByte(ColorField(L, idx, "b", 0.0));
    char buf[9];
    std::snprintf(buf, sizeof(buf), "%02x%02x%02x%02x", a, r, g, b);
    return std::string(buf, 8);
}

int __cdecl Script_ConvertRGBToHSV(void *L) {
    const double r = Arg(L, 1, "Usage: C_ColorUtil.ConvertRGBToHSV(r, g, b)");
    const double g = Arg(L, 2, "Usage: C_ColorUtil.ConvertRGBToHSV(r, g, b)");
    const double b = Arg(L, 3, "Usage: C_ColorUtil.ConvertRGBToHSV(r, g, b)");
    double h, s, v;
    RgbToHsv(r, g, b, h, s, v);
    Game::Lua::PushNumber(L, h);
    Game::Lua::PushNumber(L, s);
    Game::Lua::PushNumber(L, v);
    return 3;
}

int __cdecl Script_ConvertHSVToRGB(void *L) {
    const double h = Arg(L, 1, "Usage: C_ColorUtil.ConvertHSVToRGB(h, s, v)");
    const double s = Arg(L, 2, "Usage: C_ColorUtil.ConvertHSVToRGB(h, s, v)");
    const double v = Arg(L, 3, "Usage: C_ColorUtil.ConvertHSVToRGB(h, s, v)");
    double r, g, b;
    HsvToRgb(h, s, v, r, g, b);
    Game::Lua::PushNumber(L, r);
    Game::Lua::PushNumber(L, g);
    Game::Lua::PushNumber(L, b);
    return 3;
}

int __cdecl Script_ConvertHSVToHSL(void *L) {
    const double h = Arg(L, 1, "Usage: C_ColorUtil.ConvertHSVToHSL(h, s, v)");
    const double s = Arg(L, 2, "Usage: C_ColorUtil.ConvertHSVToHSL(h, s, v)");
    const double v = Arg(L, 3, "Usage: C_ColorUtil.ConvertHSVToHSL(h, s, v)");
    double ho, sl, l;
    HsvToHsl(h, s, v, ho, sl, l);
    Game::Lua::PushNumber(L, ho);
    Game::Lua::PushNumber(L, sl);
    Game::Lua::PushNumber(L, l);
    return 3;
}

int __cdecl Script_ConvertHSLToHSV(void *L) {
    const double h = Arg(L, 1, "Usage: C_ColorUtil.ConvertHSLToHSV(h, s, l)");
    const double s = Arg(L, 2, "Usage: C_ColorUtil.ConvertHSLToHSV(h, s, l)");
    const double l = Arg(L, 3, "Usage: C_ColorUtil.ConvertHSLToHSV(h, s, l)");
    double ho, sv, v;
    HslToHsv(h, s, l, ho, sv, v);
    Game::Lua::PushNumber(L, ho);
    Game::Lua::PushNumber(L, sv);
    Game::Lua::PushNumber(L, v);
    return 3;
}

int __cdecl Script_ConvertHSLToRGB(void *L) {
    const double h = Arg(L, 1, "Usage: C_ColorUtil.ConvertHSLToRGB(h, s, l)");
    const double s = Arg(L, 2, "Usage: C_ColorUtil.ConvertHSLToRGB(h, s, l)");
    const double l = Arg(L, 3, "Usage: C_ColorUtil.ConvertHSLToRGB(h, s, l)");
    double r, g, b;
    HslToRgb(h, s, l, r, g, b);
    Game::Lua::PushNumber(L, r);
    Game::Lua::PushNumber(L, g);
    Game::Lua::PushNumber(L, b);
    return 3;
}

int __cdecl Script_GenerateTextColorCode(void *L) {
    if (Game::Lua::Type(L, 1) != Game::Lua::TYPE_TABLE) {
        Game::Lua::Error(L, "Usage: C_ColorUtil.GenerateTextColorCode(color)");
        return 0; // unreachable
    }
    const std::string hex = HexFromColor(L, 1);
    Game::Lua::PushString(L, hex.c_str());
    return 1;
}

int __cdecl Script_WrapTextInColor(void *L) {
    const char *text = Game::Lua::ToString(L, 1);
    if (text == nullptr || Game::Lua::Type(L, 2) != Game::Lua::TYPE_TABLE) {
        Game::Lua::Error(L, "Usage: C_ColorUtil.WrapTextInColor(text, color)");
        return 0; // unreachable
    }
    std::string out = "|c" + HexFromColor(L, 2) + text + "|r";
    Game::Lua::PushString(L, out.c_str());
    return 1;
}

int __cdecl Script_WrapTextInColorCode(void *L) {
    const char *text = Game::Lua::ToString(L, 1);
    const char *code = Game::Lua::ToString(L, 2);
    if (text == nullptr || code == nullptr) {
        Game::Lua::Error(L, "Usage: C_ColorUtil.WrapTextInColorCode(text, colorCode)");
        return 0; // unreachable
    }
    std::string out = std::string("|c") + code + text + "|r";
    Game::Lua::PushString(L, out.c_str());
    return 1;
}

void RegisterFns() {
    Game::Lua::RegisterTableFunction("C_ColorUtil", "ConvertRGBToHSV",
                                     &Script_ConvertRGBToHSV);
    Game::Lua::RegisterTableFunction("C_ColorUtil", "ConvertHSVToRGB",
                                     &Script_ConvertHSVToRGB);
    Game::Lua::RegisterTableFunction("C_ColorUtil", "ConvertHSVToHSL",
                                     &Script_ConvertHSVToHSL);
    Game::Lua::RegisterTableFunction("C_ColorUtil", "ConvertHSLToHSV",
                                     &Script_ConvertHSLToHSV);
    Game::Lua::RegisterTableFunction("C_ColorUtil", "ConvertHSLToRGB",
                                     &Script_ConvertHSLToRGB);
    Game::Lua::RegisterTableFunction("C_ColorUtil", "GenerateTextColorCode",
                                     &Script_GenerateTextColorCode);
    Game::Lua::RegisterTableFunction("C_ColorUtil", "WrapTextInColor",
                                     &Script_WrapTextInColor);
    Game::Lua::RegisterTableFunction("C_ColorUtil", "WrapTextInColorCode",
                                     &Script_WrapTextInColorCode);
}

// Pure helpers — useful on both the in-game and glue states.
const Game::ModuleAutoRegister _autoreg{&RegisterFns};
const Game::GlueModuleAutoRegister _autoregGlue{&RegisterFns};

} // namespace

} // namespace Color::Util
