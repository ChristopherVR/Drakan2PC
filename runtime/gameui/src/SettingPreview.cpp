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

// ---------------------------------------------------------------- audio

// ---------------------------------------------------------------- controls

struct Entry {
    const char* key;
    Fn fn;
    const char* offLabel;
    const char* onLabel;
};

const Entry kEntries[] = {
    {"video.aspect", aspect, nullptr, nullptr},
    {"video.scale", renderScale, nullptr, nullptr},
    {"video.filter", textureFilter, nullptr, nullptr},
    {"video.anisotropic", anisotropic, nullptr, nullptr},
    {"video.hd_textures", hdTextures, "Disc textures", "HD textures"},
    {"video.fov", fov, nullptr, nullptr},
    {"video.hud_scale", hudScale, nullptr, nullptr},
    {"video.hud_aspect_correct", hudAspect, "Off", "On"},
    {"video.ray_shadows", rayShadows, "Off", "On"},
    {"video.ray_reflections", rayReflections, "Off", "On"},
    {"video.ray_gi", rayGi, "Off", "On"},
    {"video.modern_water", modernWater, "Original", "Modern"},
};

const Entry* find(const std::string& key) {
    for (const Entry& e : kEntries)
        if (key == e.key)
            return &e;
    return nullptr;
}

} // namespace

namespace {
SettingShotResolver g_shotResolver;
}
void setSettingShotResolver(SettingShotResolver resolver) {
    g_shotResolver = std::move(resolver);
}
std::string settingShotValue(const SettingRow& row, const PreviewState& st) {
    if (row.kind == SettingKind::Toggle)
        return st.on ? "on" : "off";
    if (row.kind == SettingKind::Choice)
        return st.value;
    char b[32];
    std::snprintf(b, sizeof b, "%g", static_cast<double>(st.number));
    return b;
}
uint32_t settingShotId(const SettingRow& row, const PreviewState& st) {
    return g_shotResolver && find(row.key) ? g_shotResolver(row.key, settingShotValue(row, st)) : 0;
}

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
