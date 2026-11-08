#include "gameui/SettingPreview.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <string>

namespace gameui {

namespace {

constexpr float kPi = 3.14159265f;

struct Ctx {
    DrawList& d;
    Rect r;
    double t;
    const Theme& th;
    const SettingRow& row;
};
using Fn = void (*)(const Ctx&, bool on, const std::string& val, float num);

Color mixc(Color a, Color b, float t) {
    t = std::clamp(t, 0.0f, 1.0f);
    auto ch = [&](int shift) {
        const float x = static_cast<float>((a >> shift) & 0xff), y = static_cast<float>((b >> shift) & 0xff);
        return static_cast<uint32_t>(std::lround(x + (y - x) * t)) & 0xffu;
    };
    return (ch(24) << 24) | (ch(16) << 16) | (ch(8) << 8) | ch(0);
}
float frac(double v) {
    return static_cast<float>(v - std::floor(v));
}
float fsize(const Rect& r) {
    return std::clamp(r.h * 0.075f, 11.0f, 30.0f);
}
void label(const Ctx& c, const Rect& at, const std::string& s, Color col, TextAlign a = TextAlign::Left) {
    c.d.text(at, s, FontRole::Small, fsize(c.r), col, a);
}
void disc(DrawList& d, float cx, float cy, float rad, Color col) {
    d.rect({cx - rad, cy - rad, rad * 2, rad * 2}, col, rad);
}

// The shared little world: dusk sky, a dark ground, optional pillar. `horizon` is a fraction of the height.
const Color kSkyTop = rgba(28, 38, 70), kSkyLow = rgba(176, 108, 70), kGround = rgba(52, 38, 28),
            kGroundFar = rgba(26, 19, 14);
void sky(const Ctx& c, const Rect& r, float horizon = 0.62f) {
    c.d.gradient({r.x, r.y, r.w, r.h * horizon}, kSkyTop, kSkyLow);
    c.d.gradient({r.x, r.y + r.h * horizon, r.w, r.h * (1 - horizon)}, kGroundFar, kGround);
}
void pillar(const Ctx& c, float x, float baseY, float w, float h) {
    c.d.rect({x, baseY - h, w, h}, rgba(150, 126, 98), 3);
    c.d.rect({x + w * 0.62f, baseY - h, w * 0.38f, h}, rgba(96, 78, 60), 3);
    c.d.rect({x - w * 0.12f, baseY - h - h * 0.05f, w * 1.24f, h * 0.06f}, rgba(176, 150, 118), 2);
}

// ---------------------------------------------------------------- display

void vsync(const Ctx& c, bool on, const std::string&, float) {
    const Rect& r = c.r;
    c.d.rect(r, rgba(14, 12, 12));
    const float bar = r.w * 0.14f, x = r.x + frac(c.t * 0.45) * (r.w - bar);
    const float tear = r.y + r.h * 0.52f;
    c.d.rect({x, r.y, bar, tear - r.y}, c.th.heading);
    c.d.rect({on ? x : std::max(r.x, x - r.w * 0.09f), tear, bar, r.bottom() - tear}, c.th.heading);
    if (!on) {
        c.d.rect({r.x, tear - 1.5f, r.w, 3}, c.th.bad);
        label(c, {r.x + 8, tear + 6, r.w - 16, fsize(r) + 4}, "tearing", c.th.bad, TextAlign::Left);
    } else {
        label(c, {r.x + 8, r.bottom() - fsize(r) - 10, r.w - 16, fsize(r) + 4}, "in step with the screen", c.th.good);
    }
}

void aspect(const Ctx& c, bool, const std::string& v, float) {
    const Rect& r = c.r;
    c.d.rect(r, rgba(4, 3, 3));
    float ar = r.w / r.h;
    bool stretched = false;
    if (v == "4:3" || v == "native")
        ar = 4.0f / 3.0f;
    else if (v == "16:9")
        ar = 16.0f / 9.0f;
    else if (v == "21:9")
        ar = 21.0f / 9.0f;
    else if (v == "stretch")
        stretched = true;
    Rect in = r;
    if (!stretched && ar > 0) {
        if (r.w / r.h > ar)
            in = {r.x + (r.w - r.h * ar) * 0.5f, r.y, r.h * ar, r.h};
        else
            in = {r.x, r.y + (r.h - r.w / ar) * 0.5f, r.w, r.w / ar};
    }
    c.d.pushClip(in);
    sky(c, in);
    // A round sun and a column: the sun stays round unless the picture is stretched.
    const float base = in.h * 0.28f;
    const float sw = stretched ? base * (r.w / (r.h * 4.0f / 3.0f)) : base;
    c.d.rect({in.x + in.w * 0.68f, in.y + in.h * 0.12f, sw, base}, rgba(238, 196, 120), std::min(sw, base) * 0.5f);
    pillar(c, in.x + in.w * 0.2f, in.y + in.h * 0.66f, stretched ? in.w * 0.12f : in.h * 0.12f, in.h * 0.38f);
    c.d.popClip();
    const std::string tag = stretched ? "stretched" : (v == "auto" ? "window shape" : v == "native" ? "4:3" : v);
    label(c, {in.x + 8, in.bottom() - fsize(r) - 8, in.w - 16, fsize(r) + 4}, tag, c.th.heading, TextAlign::Right);
}

void renderScale(const Ctx& c, bool, const std::string& v, float) {
    const Rect& r = c.r;
    int s = v == "auto" ? 4 : std::atoi(v.c_str());
    s = std::clamp(s, 1, 8);
    c.d.pushClip(r);
    sky(c, r, 0.7f);
    const float cell = std::max(2.0f, r.h / (9.0f * static_cast<float>(s)));
    const float cx = r.cx(), cy = r.y + r.h * 0.46f, rad = r.h * 0.3f;
    const float y0 = r.y + std::floor((cy - rad - r.y) / cell) * cell;
    for (float y = y0; y < cy + rad; y += cell) {
        const float dy = y + cell * 0.5f - cy;
        if (std::fabs(dy) >= rad)
            continue;
        const float half = std::sqrt(rad * rad - dy * dy);
        const int i0 = static_cast<int>(std::ceil((cx - half - r.x) / cell - 0.5f));
        const int i1 = static_cast<int>(std::floor((cx + half - r.x) / cell - 0.5f));
        if (i1 >= i0)
            c.d.rect({r.x + static_cast<float>(i0) * cell, y, static_cast<float>(i1 - i0 + 1) * cell, cell},
                     mixc(rgba(240, 200, 120), rgba(206, 120, 60), (y - (cy - rad)) / (rad * 2)));
    }
    c.d.popClip();
    label(c, {r.x + 8, r.bottom() - fsize(r) - 8, r.w - 16, fsize(r) + 4}, std::to_string(s) + "x pixels", c.th.heading,
          TextAlign::Right);
}

// A tiny brick texture magnified: blocky (point) or smooth (bilinear).
Color texel(int i, int j) {
    const bool mortar = (j % 3 == 2) || ((i + (j / 3) * 2) % 4 == 3);
    const unsigned h = static_cast<unsigned>(i * 73856093 ^ j * 19349663);
    const int v = static_cast<int>(h % 24);
    return mortar ? rgba(static_cast<uint8_t>(70 + v / 2), 58, 48)
                  : rgba(static_cast<uint8_t>(150 + v), static_cast<uint8_t>(82 + v / 2), 56);
}
void magnified(const Ctx& c, bool smooth) {
    const Rect& r = c.r;
    constexpr int kW = 8, kH = 6, kSub = 4;
    const float cw = r.w / kW, ch = r.h / kH;
    c.d.pushClip(r);
    for (int j = 0; j < kH; ++j)
        for (int i = 0; i < kW; ++i) {
            if (!smooth) {
                c.d.rect({r.x + static_cast<float>(i) * cw, r.y + static_cast<float>(j) * ch, cw + 0.5f, ch + 0.5f},
                         texel(i, j));
                continue;
            }
            for (int sj = 0; sj < kSub; ++sj)
                for (int si = 0; si < kSub; ++si) {
                    const float u = static_cast<float>(i) + (static_cast<float>(si) + 0.5f) / kSub - 0.5f;
                    const float w = static_cast<float>(j) + (static_cast<float>(sj) + 0.5f) / kSub - 0.5f;
                    const int i0 = static_cast<int>(std::floor(u)), j0 = static_cast<int>(std::floor(w));
                    const float fx = u - static_cast<float>(i0), fy = w - static_cast<float>(j0);
                    auto at = [&](int a, int b) { return texel(std::clamp(a, 0, kW - 1), std::clamp(b, 0, kH - 1)); };
                    const Color top = mixc(at(i0, j0), at(i0 + 1, j0), fx),
                                bot = mixc(at(i0, j0 + 1), at(i0 + 1, j0 + 1), fx);
                    c.d.rect({r.x + (static_cast<float>(i) + static_cast<float>(si) / kSub) * cw,
                              r.y + (static_cast<float>(j) + static_cast<float>(sj) / kSub) * ch, cw / kSub + 0.5f,
                              ch / kSub + 0.5f},
                             mixc(top, bot, fy));
                }
        }
    c.d.popClip();
}
void textureFilter(const Ctx& c, bool, const std::string& v, float) {
    magnified(c, v != "point");
    label(c, {c.r.x + 8, c.r.bottom() - fsize(c.r) - 8, c.r.w - 16, fsize(c.r) + 4},
          v == "point" ? "nearest: crisp, blocky" : "bilinear: smooth", c.th.heading, TextAlign::Right);
}

// A checker floor in perspective; far rows melt into grey unless the filtering keeps them sharp.
void anisotropic(const Ctx& c, bool, const std::string& v, float) {
    const Rect& r = c.r;
    const int level = std::clamp(std::atoi(v.c_str()), 1, 16);
    const float keep = std::log2(static_cast<float>(level)) / 4.0f; // 0 .. 1
    const float horizon = r.y + r.h * 0.18f;
    c.d.pushClip(r);
    c.d.gradient({r.x, r.y, r.w, horizon - r.y + 1}, kSkyTop, kSkyLow);
    constexpr int kRows = 16, kCols = 12;
    const float scroll = frac(c.t * 0.25);
    for (int k = kRows - 1; k >= 0; --k) {
        const float z0 = 1.0f + (static_cast<float>(k) + scroll) * 0.62f, z1 = z0 + 0.62f;
        const float yBot = horizon + (r.bottom() - horizon) / z0, yTop = horizon + (r.bottom() - horizon) / z1;
        const float blur = (1.0f - keep) * std::min(1.0f, static_cast<float>(k) / 8.0f);
        const float w0 = r.w * 1.6f / z0 / kCols;
        for (int i = -kCols / 2 - 1; i <= kCols / 2 + 1; ++i) {
            const bool light = ((i + k) & 1) == 0;
            const Color base = light ? rgba(190, 150, 100) : rgba(64, 46, 34);
            const Color grey = rgba(124, 98, 68);
            c.d.rect({r.cx() + static_cast<float>(i) * w0, yTop, w0 + 0.5f, yBot - yTop + 0.5f},
                     mixc(base, grey, blur));
        }
    }
    c.d.popClip();
    label(c, {r.x + 8, r.bottom() - fsize(r) - 8, r.w - 16, fsize(r) + 4},
          level == 1 ? "far floor blurs" : std::to_string(level) + "x: far floor stays sharp", c.th.heading,
          TextAlign::Right);
}

void hdTextures(const Ctx& c, bool on, const std::string&, float) {
    const Rect& r = c.r;
    if (!on) {
        magnified(c, false);
        return;
    }
    // Fine bricks with mortar, chips and a lit edge.
    c.d.pushClip(r);
    c.d.rect(r, rgba(78, 64, 52));
    const float bh = r.h / 7;
    for (int j = 0; j < 7; ++j) {
        const float off = (j & 1) ? r.w / 10 : 0;
        for (int i = -1; i < 5; ++i) {
            const float bw = r.w / 4;
            const unsigned h = static_cast<unsigned>((i + 7) * 2654435761u ^ static_cast<unsigned>(j) * 40503u);
            const int v = static_cast<int>(h % 32);
            const Rect b{r.x + static_cast<float>(i) * bw + off + 2, r.y + static_cast<float>(j) * bh + 2, bw - 4,
                         bh - 4};
            c.d.rect(b, rgba(static_cast<uint8_t>(150 + v), static_cast<uint8_t>(84 + v / 2), 58), 2);
            c.d.rect({b.x, b.y, b.w, 2}, rgba(220, 160, 110, 180));
            c.d.rect({b.x, b.bottom() - 2, b.w, 2}, rgba(60, 30, 20, 160));
            if (v > 22)
                c.d.line({b.x + b.w * 0.3f, b.y + 3}, {b.x + b.w * 0.42f, b.bottom() - 4}, rgba(60, 32, 22), 1.2f);
        }
    }
    c.d.popClip();
}

void fov(const Ctx& c, bool, const std::string&, float deg) {
    const Rect& r = c.r;
    c.d.rect(r, rgba(14, 11, 10));
    const float half = std::clamp(deg, 30.0f, 140.0f) * 0.5f * kPi / 180.0f;
    const float cx = r.cx(), cy = r.bottom() - r.h * 0.12f, len = r.h * 0.8f;
    c.d.pushClip(r);
    for (float y = 0; y < len; y += 3.0f) {
        const float hw = std::min(y * std::tan(half), r.w * 0.5f);
        c.d.rect({cx - hw, cy - y, hw * 2, 3.5f}, withAlpha(c.th.brass, static_cast<uint8_t>(34 + 26 * (1 - y / len))));
    }
    c.d.line({cx, cy}, {cx - std::sin(half) * len * 1.2f, cy - std::cos(half) * len * 1.2f}, c.th.brass, 2);
    c.d.line({cx, cy}, {cx + std::sin(half) * len * 1.2f, cy - std::cos(half) * len * 1.2f}, c.th.brass, 2);
    static const float kPol[][2] = {{-70, 0.9f}, {-52, 0.75f}, {-38, 0.95f}, {-22, 0.6f}, {-8, 0.85f}, {8, 0.55f},
                                    {24, 0.9f},  {40, 0.7f},   {54, 0.95f},  {68, 0.8f},  {80, 0.65f}};
    for (const auto& p : kPol) {
        const float a = p[0] * kPi / 180.0f, dist = p[1] * len;
        const bool in = std::fabs(a) <= half;
        disc(c.d, cx + std::sin(a) * dist, cy - std::cos(a) * dist, r.h * 0.025f,
             in ? c.th.heading : withAlpha(c.th.textDisabled, 150));
    }
    c.d.rect({cx - 9, cy - 5, 18, 14}, c.th.ember, 3);
    c.d.popClip();
    label(c, {r.x + 8, r.y + 6, r.w - 16, fsize(r) + 4},
          "top view: " + std::to_string(static_cast<int>(deg)) + " degrees", c.th.heading);
}

// Smoothness: the ball's path sampled at the frame rate.
void frameRate(const Ctx& c, bool, const std::string& v, float) {
    const Rect& r = c.r;
    c.d.rect(r, rgba(14, 12, 12));
    float hz = 120;
    if (v != "uncapped" && v != "0")
        hz = std::clamp(static_cast<float>(std::atof(v.c_str())), 10.0f, 240.0f);
    const float track = r.w * 0.78f, x0 = r.cx() - track * 0.5f, y = r.cy();
    auto pos = [&](double t) { return x0 + track * (0.5f + 0.5f * std::sin(static_cast<float>(t) * 2.0f)); };
    const double tq = std::floor(c.t * hz) / hz;
    c.d.line({x0, y + r.h * 0.2f}, {x0 + track, y + r.h * 0.2f}, c.th.textDisabled, 2);
    const float step = hz >= 100 ? 0.0f : 1.0f / hz;
    for (int k = 7; k >= 1; --k) {
        const float a = 1.0f - static_cast<float>(k) / 8.0f;
        disc(c.d, pos(tq - k * (step > 0 ? step : 0.012)), y, r.h * 0.07f,
             withAlpha(c.th.brass, static_cast<uint8_t>(110 * a)));
    }
    disc(c.d, pos(tq), y, r.h * 0.1f, c.th.heading);
    label(c, {r.x + 8, r.bottom() - fsize(r) - 8, r.w - 16, fsize(r) + 4},
          hz >= 100 ? "very smooth motion" : std::to_string(static_cast<int>(hz)) + " pictures a second", c.th.heading,
          TextAlign::Right);
}

void windowMode(const Ctx& c, bool, const std::string& v, float) {
    const Rect& r = c.r;
    const Rect mon = r.inset(std::min(r.w, r.h) * 0.08f);
    c.d.rect(mon, rgba(8, 8, 10), 8);
    c.d.outline(mon, c.th.brassDim, 3, 8);
    const Rect scr = mon.inset(9);
    c.d.pushClip(scr);
    if (v == "windowed") {
        c.d.rect(scr, rgba(36, 48, 64));
        c.d.rect({scr.x, scr.bottom() - scr.h * 0.1f, scr.w, scr.h * 0.1f}, rgba(18, 22, 30));
        const Rect win{scr.x + scr.w * 0.18f, scr.y + scr.h * 0.12f, scr.w * 0.64f, scr.h * 0.64f};
        c.d.rect({win.x, win.y, win.w, win.h * 0.14f}, rgba(70, 70, 78));
        for (int i = 0; i < 3; ++i)
            disc(c.d, win.right() - 12.0f - static_cast<float>(i) * 14, win.y + win.h * 0.07f, 4, rgba(200, 190, 180));
        const Rect g{win.x, win.y + win.h * 0.14f, win.w, win.h * 0.86f};
        c.d.pushClip(g);
        sky(c, g);
        c.d.popClip();
        label(c, {scr.x, scr.bottom() - scr.h * 0.1f - 2, scr.w, scr.h * 0.1f}, "  desktop and taskbar stay",
              c.th.textMuted);
    } else {
        sky(c, scr);
        if (v == "exclusive")
            label(c, {scr.x + 6, scr.y + 6, scr.w - 12, fsize(r) + 4}, "own video mode", c.th.heading, TextAlign::Left);
        else
            label(c, {scr.x + 6, scr.y + 6, scr.w - 12, fsize(r) + 4}, "no border, instant Alt+Tab", c.th.heading,
                  TextAlign::Left);
    }
    c.d.popClip();
}

void flicker(const Ctx& c, bool on, const std::string&, float) {
    const Rect& r = c.r;
    constexpr int kN = 14;
    const float h = r.h / kN;
    for (int i = 0; i < kN; ++i) {
        Color col = (i & 1) ? rgba(228, 214, 186) : rgba(26, 20, 18);
        if (on)
            col = mixc(col, rgba(128, 118, 102), 0.62f);
        c.d.rect({r.x, r.y + static_cast<float>(i) * h, r.w, h + 0.5f}, col);
    }
    label(c, {r.x + 8, r.bottom() - fsize(r) - 8, r.w - 16, fsize(r) + 4}, on ? "softer, no shimmer" : "crisp lines",
          c.th.bad, TextAlign::Right);
}

void sharpPresent(const Ctx& c, bool on, const std::string&, float) {
    const Rect& r = c.r;
    c.d.rect(r, rgba(30, 22, 18));
    c.d.pushClip(r);
    const Vec2 a{r.x + r.w * 0.15f, r.bottom() - r.h * 0.18f}, b{r.right() - r.w * 0.15f, r.y + r.h * 0.18f};
    if (on) {
        c.d.line(a, b, c.th.heading, 3);
    } else {
        for (int i = -3; i <= 3; ++i)
            c.d.line({a.x, a.y + static_cast<float>(i) * 2.2f}, {b.x, b.y + static_cast<float>(i) * 2.2f},
                     withAlpha(c.th.heading, static_cast<uint8_t>(190 - std::abs(i) * 50)), 2.4f);
    }
    c.d.popClip();
    label(c, {r.x + 8, r.bottom() - fsize(r) - 8, r.w - 16, fsize(r) + 4}, on ? "sharp edges" : "soft edges",
          c.th.textMuted, TextAlign::Right);
}

void hudScale(const Ctx& c, bool, const std::string& v, float) {
    const Rect& r = c.r;
    sky(c, r);
    const float s = std::clamp(v == "0" || v.empty() ? 1.0f : static_cast<float>(std::atof(v.c_str())), 0.4f, 1.6f);
    const float d = r.h * 0.2f * s;
    c.d.rect({r.x + 12, r.bottom() - d - 10, d, d}, withAlpha(c.th.hp, 235), d * 0.5f);
    c.d.rect({r.right() - d - 12, r.bottom() - d - 10, d, d}, withAlpha(c.th.mana, 235), d * 0.5f);
    label(c, {r.x + 8, r.y + 6, r.w - 16, fsize(r) + 4},
          v == "0" || v.empty() ? "follows window size"
                                : "HUD at " + std::to_string(static_cast<int>(s * 100 + 0.5f)) + "%",
          c.th.heading, TextAlign::Right);
}

void hudAspect(const Ctx& c, bool on, const std::string&, float) {
    const Rect& r = c.r;
    sky(c, r);
    const float d = r.h * 0.34f;
    c.d.rect({r.cx() - (on ? d : d * 1.7f) * 0.5f, r.cy() - d * 0.5f, on ? d : d * 1.7f, d}, withAlpha(c.th.hp, 235),
             d * 0.5f);
    label(c, {r.x + 8, r.bottom() - fsize(r) - 8, r.w - 16, fsize(r) + 4}, on ? "orbs stay round" : "orbs stretch",
          c.th.heading, TextAlign::Right);
}

void nearClip(const Ctx& c, bool on, const std::string&, float) {
    const Rect& r = c.r;
    sky(c, r);
    c.d.pushClip(r);
    if (on) {
        c.d.line({r.x + r.w * 0.1f, r.bottom() - r.h * 0.1f}, {r.cx(), r.cy()}, rgba(150, 210, 255), 6);
    } else {
        for (int i = -2; i <= 2; ++i)
            c.d.line({r.x + r.w * 0.1f, r.bottom() - r.h * 0.1f},
                     {r.cx() + static_cast<float>(i) * r.w * 0.34f, r.y - 20},
                     withAlpha(rgba(150, 210, 255), static_cast<uint8_t>(210 - std::abs(i) * 50)), 5);
    }
    c.d.popClip();
    label(c, {r.x + 8, r.bottom() - fsize(r) - 8, r.w - 16, fsize(r) + 4},
          on ? "cut cleanly" : "streaks across the screen", on ? c.th.good : c.th.bad, TextAlign::Right);
}

void menuHighlight(const Ctx& c, bool on, const std::string&, float) {
    const Rect& r = c.r;
    c.d.rect(r, rgba(21, 14, 10));
    const Rect band{r.x + r.w * 0.08f, r.cy() - r.h * 0.14f, r.w * 0.84f, r.h * 0.28f};
    if (on) {
        c.d.gradient(band, rgba(150, 48, 30), rgba(96, 26, 16));
    } else {
        constexpr int kN = 14;
        const float cw = band.w / kN, ch = band.h / 4;
        for (int j = 0; j < 4; ++j)
            for (int i = 0; i < kN; ++i)
                c.d.rect({band.x + static_cast<float>(i) * cw, band.y + static_cast<float>(j) * ch, cw, ch},
                         ((i + j) & 1) ? rgba(176, 56, 34) : rgba(70, 18, 12));
    }
    c.d.text(band, "Menu entry", FontRole::Body, fsize(r) * 1.3f, c.th.heading, TextAlign::Center);
}

// ---------------------------------------------------------------- picture

void rayShadows(const Ctx& c, bool on, const std::string&, float) {
    const Rect& r = c.r;
    sky(c, r, 0.6f);
    const float base = r.y + r.h * 0.8f;
    if (on) {
        for (int i = 0; i < 5; ++i)
            c.d.rect({r.x + r.w * 0.36f + static_cast<float>(i) * 3, base - r.h * 0.05f + static_cast<float>(i),
                      r.w * (0.36f - 0.02f * static_cast<float>(i)), r.h * 0.07f},
                     rgba(0, 0, 0, 40), r.h * 0.035f);
    }
    pillar(c, r.x + r.w * 0.3f, base, r.w * 0.1f, r.h * 0.5f);
}

void rayReflections(const Ctx& c, bool on, const std::string&, float) {
    const Rect& r = c.r;
    sky(c, r, 0.58f);
    const float shore = r.y + r.h * 0.58f;
    c.d.rect({r.x, shore, r.w, r.bottom() - shore}, rgba(20, 38, 52));
    pillar(c, r.x + r.w * 0.4f, shore, r.w * 0.1f, r.h * 0.42f);
    if (on) {
        const float h = r.h * 0.3f;
        c.d.gradient({r.x + r.w * 0.4f, shore, r.w * 0.1f, h}, rgba(120, 100, 84, 200), rgba(120, 100, 84, 0));
        c.d.gradient({r.x + r.w * 0.1f, shore, r.w * 0.8f, r.bottom() - shore}, rgba(176, 108, 70, 70),
                     rgba(176, 108, 70, 0));
    }
}

void rayGi(const Ctx& c, bool on, const std::string&, float) {
    const Rect& r = c.r;
    sky(c, r, 0.62f);
    const float base = r.y + r.h * 0.8f;
    if (on) {
        for (int i = 0; i < 6; ++i) {
            const float g = static_cast<float>(i) * r.h * 0.03f;
            c.d.rect({r.x + r.w * 0.18f - g, base - r.h * 0.12f - g * 0.6f, r.w * 0.34f + g * 2, r.h * 0.2f + g * 1.2f},
                     rgba(255, 150, 70, 22), r.h * 0.1f);
        }
    }
    pillar(c, r.x + r.w * 0.3f, base, r.w * 0.1f, r.h * 0.5f);
    c.d.rect({r.x + r.w * 0.62f, base - r.h * 0.25f, r.w * 0.2f, r.h * 0.25f},
             on ? rgba(210, 120, 64) : rgba(96, 52, 30), 3);
}

void modernWater(const Ctx& c, bool on, const std::string&, float) {
    const Rect& r = c.r;
    c.d.rect(r, rgba(18, 44, 62));
    c.d.pushClip(r);
    constexpr int kRows = 8, kSeg = 22;
    for (int j = 0; j < kRows; ++j) {
        const float y = r.y + (static_cast<float>(j) + 0.5f) * r.h / kRows;
        if (!on) {
            c.d.rect({r.x, y - 3, r.w, 6},
                     ((j + static_cast<int>(c.t * 1.5)) & 1) ? rgba(34, 78, 104) : rgba(26, 60, 82));
            continue;
        }
        Vec2 prev{r.x, y};
        for (int i = 1; i <= kSeg; ++i) {
            const float x = r.x + r.w * static_cast<float>(i) / kSeg;
            const float yy =
                y + std::sin(static_cast<float>(i) * 0.7f + static_cast<float>(j) + static_cast<float>(c.t) * 2.2f) *
                        r.h * 0.025f;
            c.d.line(prev, {x, yy}, withAlpha(rgba(170, 220, 240), static_cast<uint8_t>(90 + 20 * (j % 3))), 2.2f);
            prev = {x, yy};
        }
    }
    c.d.popClip();
}

void foliage(const Ctx& c, bool on, const std::string&, float amp) {
    const Rect& r = c.r;
    sky(c, r, 0.55f);
    const float base = r.bottom() - r.h * 0.06f;
    const float sway = c.row.kind == SettingKind::Toggle ? (on ? 1.0f : 0.0f) : std::clamp(amp, 0.0f, 2.0f);
    for (int i = 0; i < 14; ++i) {
        const float x = r.x + (static_cast<float>(i) + 0.5f) * r.w / 14,
                    h = r.h * (0.34f + 0.12f * static_cast<float>((i * 7) % 5) / 4);
        const float dx = std::sin(static_cast<float>(c.t) * 2.0f + static_cast<float>(i) * 0.6f) * r.w * 0.03f * sway;
        c.d.line({x, base}, {x + dx * 0.5f, base - h * 0.5f}, rgba(70, 120, 56), 3);
        c.d.line({x + dx * 0.5f, base - h * 0.5f}, {x + dx, base - h}, rgba(110, 170, 70), 2.5f);
    }
}

// ---------------------------------------------------------------- audio

void speaker(const Ctx& c, const Rect& r, int waves, bool slash, Color col) {
    const float u = std::min(r.w, r.h) * 0.12f;
    const float x = r.cx() - u * 2.5f, y = r.cy();
    c.d.rect({x, y - u, u * 1.4f, u * 2}, col, 2);
    for (int i = 0; i < 6; ++i) {
        const float k = static_cast<float>(i) / 5;
        c.d.rect({x + u * 1.4f + k * u * 1.4f, y - u * (1.0f + k * 1.4f), u * 0.3f, u * (2.0f + k * 2.8f)}, col);
    }
    for (int w = 0; w < waves; ++w) {
        const float rad = u * (1.8f + 1.5f * static_cast<float>(w));
        Vec2 prev{x + u * 3.2f + rad * 0.0f, y - rad * 0.9f};
        for (int i = 1; i <= 6; ++i) {
            const float a = -0.9f + 1.8f * static_cast<float>(i) / 6;
            const Vec2 p{x + u * 2.4f + std::cos(a) * rad, y + std::sin(a) * rad};
            c.d.line(prev, p, col, 2.5f);
            prev = p;
        }
    }
    if (slash)
        c.d.line({r.cx() - u * 3, r.cy() + u * 3.5f}, {r.cx() + u * 3, r.cy() - u * 3.5f}, c.th.bad, 4);
}
void soundOnOff(const Ctx& c, bool on, const std::string&, float) {
    c.d.rect(c.r, rgba(18, 14, 12));
    speaker(c, c.r, on ? 3 : 0, !on, on ? c.th.heading : c.th.textMuted);
}
void masterVolume(const Ctx& c, bool, const std::string&, float pct) {
    const Rect& r = c.r;
    c.d.rect(r, rgba(18, 14, 12));
    const float level = std::clamp(pct / 100.0f, 0.0f, 2.0f);
    constexpr int kBars = 16;
    const float bw = r.w * 0.7f / kBars, x0 = r.cx() - r.w * 0.35f, base = r.bottom() - r.h * 0.22f;
    for (int i = 0; i < kBars; ++i) {
        const float k = static_cast<float>(i) / kBars;
        const float bob = 0.5f + 0.5f * std::sin(static_cast<float>(c.t) * 5 + static_cast<float>(i) * 0.8f);
        const float h = r.h * 0.5f * std::min(1.0f, level) * (0.45f + 0.55f * bob) *
                        (0.5f + 0.5f * (1 - std::fabs(k - 0.5f) * 1.2f));
        const bool clip = level > 1.0f && k > 0.7f;
        c.d.rect({x0 + static_cast<float>(i) * bw + 1, base - h, bw - 2, std::max(2.0f, h)},
                 clip ? c.th.bad : c.th.brass);
    }
    label(c, {r.x + 8, r.y + 6, r.w - 16, fsize(r) + 4},
          level > 1.0f ? "boosted: may clip" : std::to_string(static_cast<int>(pct + 0.5f)) + "%",
          level > 1.0f ? c.th.bad : c.th.heading, TextAlign::Right);
}

// ---------------------------------------------------------------- controls

void invertY(const Ctx& c, bool on, const std::string&, float) {
    const Rect& r = c.r;
    c.d.rect(r, rgba(18, 14, 12));
    const float push = std::sin(static_cast<float>(c.t) * 1.8f); // mouse forward (up) when positive
    const Rect mouse{r.x + r.w * 0.1f, r.cy() - r.h * 0.16f - push * r.h * 0.08f, r.w * 0.16f, r.h * 0.32f};
    c.d.rect(mouse, rgba(190, 176, 150), mouse.w * 0.5f);
    c.d.line({mouse.cx(), mouse.y + 2}, {mouse.cx(), mouse.y + mouse.h * 0.4f}, rgba(60, 50, 40), 2);
    const Rect view{r.x + r.w * 0.36f, r.y + r.h * 0.12f, r.w * 0.58f, r.h * 0.76f};
    c.d.pushClip(view);
    const float horizon = view.cy() + (on ? -1.0f : 1.0f) * push * view.h * 0.22f;
    c.d.rect({view.x, view.y, view.w, std::max(0.0f, horizon - view.y)}, kSkyTop);
    c.d.rect({view.x, horizon, view.w, std::max(0.0f, view.bottom() - horizon)}, kGround);
    c.d.rect({view.x, horizon - 1, view.w, 2}, kSkyLow);
    c.d.popClip();
    c.d.line({view.cx() - 8, view.cy()}, {view.cx() + 8, view.cy()}, c.th.heading, 2);
    c.d.line({view.cx(), view.cy() - 8}, {view.cx(), view.cy() + 8}, c.th.heading, 2);
    label(c, {view.x, view.bottom() - fsize(r) - 2, view.w, fsize(r) + 4},
          on ? "mouse forward: look down" : "mouse forward: look up", c.th.textMuted, TextAlign::Center);
}

void mouseSensitivity(const Ctx& c, bool, const std::string&, float v) {
    const Rect& r = c.r;
    float f = 0.5f;
    if (c.row.max > c.row.min && c.row.min > 0 && c.row.logarithmic)
        f = (std::log(std::max(v, c.row.min)) - std::log(c.row.min)) / (std::log(c.row.max) - std::log(c.row.min));
    else if (c.row.max > c.row.min)
        f = (v - c.row.min) / (c.row.max - c.row.min);
    f = std::clamp(f, 0.0f, 1.0f);
    sky(c, r, 0.7f);
    c.d.pushClip(r);
    // The same hand movement (a fixed sweep) turns the view by more or less.
    const float sweep = std::sin(static_cast<float>(c.t) * 1.6f);
    const float shift = sweep * r.w * (0.08f + 0.9f * f);
    for (int i = -6; i <= 6; ++i) {
        const float x = r.cx() + static_cast<float>(i) * r.w * 0.22f - shift;
        pillar(c, x, r.y + r.h * 0.78f, r.w * 0.05f, r.h * 0.4f);
    }
    c.d.popClip();
    c.d.line({r.cx() - 8, r.cy()}, {r.cx() + 8, r.cy()}, c.th.heading, 2);
    c.d.line({r.cx(), r.cy() - 8}, {r.cx(), r.cy() + 8}, c.th.heading, 2);
    label(c, {r.x + 8, r.y + 6, r.w - 16, fsize(r) + 4}, "same hand movement", c.th.heading, TextAlign::Right);
}

void deadZone(const Ctx& c, bool, const std::string&, float dz) {
    const Rect& r = c.r;
    c.d.rect(r, rgba(18, 14, 12));
    const float rad = std::min(r.w, r.h) * 0.38f;
    const float cx = r.cx(), cy = r.cy();
    disc(c.d, cx, cy, rad, rgba(40, 30, 24));
    const float fz = std::clamp(dz / std::max(0.01f, c.row.max), 0.0f, 1.0f) * 0.5f;
    disc(c.d, cx, cy, rad * fz * 2.0f, withAlpha(c.th.bad, 110));
    const float a = static_cast<float>(c.t) * 1.3f, m = 0.5f + 0.5f * std::sin(static_cast<float>(c.t) * 0.9f);
    const float px = std::cos(a) * m, py = std::sin(a) * m; // stick position, 0..1 of the throw
    const float mag = std::sqrt(px * px + py * py);
    disc(c.d, cx + px * rad, cy + py * rad, rad * 0.1f, mag < fz * 2.0f ? c.th.textMuted : c.th.heading);
    label(c, {r.x + 8, r.bottom() - fsize(r) - 8, r.w - 16, fsize(r) + 4},
          mag < fz * 2.0f ? "ignored: no movement" : "moves the character", mag < fz * 2.0f ? c.th.bad : c.th.good,
          TextAlign::Right);
}

void prompts(const Ctx& c, bool, const std::string& v, float) {
    const Rect& r = c.r;
    c.d.rect(r, rgba(18, 14, 12));
    const float u = r.h * 0.26f;
    if (v == "ps2") {
        const float y = r.cy();
        const Color cols[4] = {rgba(96, 150, 230), rgba(230, 90, 90), rgba(110, 200, 120), rgba(230, 140, 200)};
        for (int i = 0; i < 4; ++i) {
            const float x = r.cx() + (static_cast<float>(i) - 1.5f) * u * 1.5f;
            c.d.rect({x - u * 0.5f, y - u * 0.5f, u, u}, rgba(30, 28, 34), u * 0.5f);
            c.d.outline({x - u * 0.5f, y - u * 0.5f, u, u}, cols[i], 3, u * 0.5f);
            if (i == 0) {
                c.d.line({x - u * 0.2f, y - u * 0.2f}, {x + u * 0.2f, y + u * 0.2f}, cols[i], 3);
                c.d.line({x + u * 0.2f, y - u * 0.2f}, {x - u * 0.2f, y + u * 0.2f}, cols[i], 3);
            } else if (i == 1) {
                c.d.outline({x - u * 0.2f, y - u * 0.2f, u * 0.4f, u * 0.4f}, cols[i], 3, u * 0.2f);
            }
        }
    } else {
        const char* keys[] = {"E", "Space", "Shift"};
        float x = r.cx() - u * 2.4f;
        for (int i = 0; i < 3; ++i) {
            const float w = i == 0 ? u : u * 1.6f;
            c.d.rect({x, r.cy() - u * 0.5f, w, u}, rgba(44, 38, 34), 5);
            c.d.outline({x, r.cy() - u * 0.5f, w, u}, c.th.brass, 2, 5);
            c.d.text({x, r.cy() - u * 0.5f, w, u}, keys[i], FontRole::Body, fsize(r) * 1.1f, c.th.heading,
                     TextAlign::Center);
            x += w + u * 0.35f;
        }
    }
    label(c, {r.x + 8, r.bottom() - fsize(r) - 8, r.w - 16, fsize(r) + 4},
          v == "ps2" ? "PlayStation 2 buttons" : "your keys and mouse", c.th.textMuted, TextAlign::Right);
}

struct Entry {
    const char* key;
    Fn fn;
    const char* offLabel;
    const char* onLabel;
};

const Entry kEntries[] = {
    {"video.vsync", vsync, "Off", "On"},
    {"video.aspect", aspect, nullptr, nullptr},
    {"video.scale", renderScale, nullptr, nullptr},
    {"video.filter", textureFilter, nullptr, nullptr},
    {"video.anisotropic", anisotropic, nullptr, nullptr},
    {"video.hd_textures", hdTextures, "Disc textures", "HD textures"},
    {"video.fov", fov, nullptr, nullptr},
    {"video.world_rate", frameRate, nullptr, nullptr},
    {"video.frame_limit", frameRate, nullptr, nullptr},
    {"video.window_mode", windowMode, nullptr, nullptr},
    {"video.flicker_filter", flicker, "Off", "On"},
    {"video.sharp_present", sharpPresent, "Off", "On"},
    {"video.hud_scale", hudScale, nullptr, nullptr},
    {"video.hud_aspect_correct", hudAspect, "Off", "On"},
    {"video.poly_near_clip", nearClip, "Off", "On"},
    {"video.smooth_menu_highlight", menuHighlight, "Off", "On"},
    {"video.ray_shadows", rayShadows, "Off", "On"},
    {"video.ray_reflections", rayReflections, "Off", "On"},
    {"video.ray_gi", rayGi, "Off", "On"},
    {"video.modern_water", modernWater, "Original", "Modern"},
    {"video.foliage_wind", foliage, "Still", "Swaying"},
    {"video.foliage_wind_strength", foliage, nullptr, nullptr},
    {"audio.enabled", soundOnOff, "Muted", "Sound"},
    {"audio.volume", masterVolume, nullptr, nullptr},
    {"input.mouse.invert_y", invertY, "Off", "On"},
    {"input.mouse.sensitivity_x", mouseSensitivity, nullptr, nullptr},
    {"input.mouse.sensitivity_y", mouseSensitivity, nullptr, nullptr},
    {"input.gamepad.deadzone", deadZone, nullptr, nullptr},
    {"general.ui_prompts", prompts, nullptr, nullptr},
};

const Entry* find(const std::string& key) {
    for (const Entry& e : kEntries)
        if (key == e.key)
            return &e;
    return nullptr;
}

} // namespace

bool hasSettingPreview(const std::string& key) {
    return find(key) != nullptr;
}

std::string settingPreviewCaption(const SettingRow& row, const PreviewState& st) {
    const Entry* e = find(row.key);
    if (!e)
        return {};
    if (row.kind == SettingKind::Toggle)
        return std::string(e->offLabel ? e->offLabel : "Off") + "  |  " + (e->onLabel ? e->onLabel : "On");
    if (row.kind == SettingKind::Choice) {
        for (const SettingChoice& c : row.choices)
            if (c.value == st.value)
                return "Showing: " + c.label;
    }
    return {};
}

bool drawSettingPreview(DrawList& d, const Rect& box, const SettingRow& row, const PreviewState& st, const Theme& th) {
    const Entry* e = find(row.key);
    if (!e || box.empty())
        return false;
    const Ctx whole{d, box, st.time, th, row};
    d.pushClip(box);
    if (row.kind == SettingKind::Toggle) {
        const float gap = 4;
        const Rect left{box.x, box.y, (box.w - gap) * 0.5f, box.h},
            right{box.x + (box.w + gap) * 0.5f, box.y, (box.w - gap) * 0.5f, box.h};
        const Ctx a{d, left, st.time, th, row}, b{d, right, st.time, th, row};
        for (int i = 0; i < 2; ++i) {
            const Ctx& cx = i == 0 ? a : b;
            d.pushClip(cx.r);
            e->fn(cx, i == 1, {}, st.number);
            const bool current = (i == 1) == st.on;
            if (!current)
                d.rect(cx.r, rgba(0, 0, 0, 120)); // the side that is not selected is dimmed
            const float fs = fsize(cx.r);
            d.rect({cx.r.x, cx.r.y, cx.r.w, fs + 10}, rgba(0, 0, 0, 150));
            d.text({cx.r.x + 8, cx.r.y + 3, cx.r.w - 16, fs + 4},
                   i == 0 ? (e->offLabel ? e->offLabel : "Off") : (e->onLabel ? e->onLabel : "On"), FontRole::Small, fs,
                   current ? th.heading : th.textMuted, TextAlign::Left);
            d.popClip();
        }
        d.outline(st.on ? right : left, th.select, 2.5f, 2);
    } else {
        e->fn(whole, false, st.value, st.number);
    }
    d.popClip();
    d.outline(box, th.brassDim, 1.5f, 2);
    return true;
}

} // namespace gameui
