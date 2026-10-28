// Pure layout and state rules of the Order-of-the-Flame style views of inventory screen 0x15 (M = full-screen map,
// I = Equipment strip along the bottom). The guest hooks live in drakan_inventory_views.inl; see
// docs/reference/mouse-menus.md section 12.
//
// Units are the native GUI space (W x H = 512 x 512 for a 4:3 picture). The renderer maps every post-world inventory
// draw with the page transform x' = x * pageScale (hud_page.h inventoryMap): left-anchored for the normal screen,
// about the screen centre for a view (SceneGuideCamera::CentredPage). A view keeps the native window [0, W] (no page
// offset) and lays its content out centred in it, so it lands centred on a wide screen.
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace drakan_views {

// Modern: the host-drawn Tab screen (drakan_tab_screen.inl, runtime/gameui) over the native screen, whose page is
// hidden; it uses the views' page removal, whole world and centring, and keeps every section.
enum class View : uint8_t { None = 0, Map = 1, Strip = 2, Modern = 3 };

// Inventory sections (screen+0x154): 0 Equipment, 1 Spells, 2 Skills, 3 Map, 4 Journal.
constexpr int kEquipmentSection = 0, kMapSection = 3, kSectionCount = 5;

constexpr int targetSection(View v) {
    return v == View::Map ? kMapSection : kEquipmentSection;
}

// Section stepping for the direct jump: the screen is first set to the section after (dir -1) or before (dir +1) the
// target, so one native step (4309d8 key 0xd3 = previous, 0xd1 = next) lands on the target and only the target's
// enter handler runs. Returns the section index to preset.
constexpr int jumpPreset(int target, int count) {
    return count > 0 ? (target + 1) % count : 0;
}

struct Fit {
    float scale = 1, x = 0, y = 0; // uniform scale and top-left of the drawn image
};

// Largest uniform scale that fits a tw x th image into the box, centred in it.
inline Fit fitCentred(float tw, float th, float left, float top, float right, float bottom) {
    Fit f;
    if (!(tw > 0 && th > 0 && right > left && bottom > top))
        return f;
    f.scale = std::min((right - left) / tw, (bottom - top) / th);
    f.x = left + 0.5f * ((right - left) - tw * f.scale);
    f.y = top + 0.5f * ((bottom - top) - th * f.scale);
    return f;
}

// A host target rectangle in GUI units (where the host wants the native grid or map drawn). The GS front end clips at
// the native window [0, W] x [0, H], and a letterboxed game picture (a 21:9 / 32:9 picture on a 16:9 output) puts the
// host's design rectangles partly outside it: the map lost its left part, the grid its icons.
struct Box {
    float x0 = 0, y0 = 0, x1 = 0, y1 = 0;
    float w() const { return x1 - x0; }
    float h() const { return y1 - y0; }
};
// The part of the box inside [m, W - m] x [m, H - m] (the map is fitted into it afterwards, so its aspect may change).
// Empty (x1 <= x0 or y1 <= y0) when nothing is left.
inline Box clipToWindow(Box b, float W, float H, float m) {
    return {std::max(b.x0, m), std::max(b.y0, m), std::min(b.x1, W - m), std::min(b.y1, H - m)};
}
// The box kept whole inside [m, W - m] x [m, H - m]: shrunk about its centre when it is larger (aspect kept: the grid's
// cells stay square), then moved inside. Unchanged when it already fits.
inline Box keepInWindow(Box b, float W, float H, float m) {
    const float aw = W - 2 * m, ah = H - 2 * m;
    if (!(b.w() > 0 && b.h() > 0 && aw > 0 && ah > 0))
        return b;
    const float k = std::min({1.0f, aw / b.w(), ah / b.h()});
    const float w = b.w() * k, h = b.h() * k;
    const float cx = std::clamp(0.5f * (b.x0 + b.x1), m + 0.5f * w, W - m - 0.5f * w);
    const float cy = std::clamp(0.5f * (b.y0 + b.y1), m + 0.5f * h, H - m - 0.5f * h);
    return {cx - 0.5f * w, cy - 0.5f * h, cx + 0.5f * w, cy + 0.5f * h};
}

// Map view: native map background drawn at origin (ox, oy) with size tw x th becomes fit (scale, x, y). Map-relative
// points (markers, the "you are here" object) scale about the image; the region caption keeps its native distance
// below the image's bottom edge and its x relative to the image's left edge.
struct MapLayout {
    float ox = 0, oy = 0, tw = 0, th = 0;
    Fit fit;
    float iconScale = 1;
    bool valid = false;
    float x(float px) const { return fit.x + (px - ox) * fit.scale; }
    float y(float py) const { return fit.y + (py - oy) * fit.scale; }
    float captionX(float px) const { return fit.x + (px - ox); }
    float captionY(float py) const { return fit.y + th * fit.scale + (py - (oy + th)); }
};

struct MapBox {
    float margin = 12;         // native pixels kept free at the left, right and top
    float captionReserve = 52; // native pixels kept free below the image for the region caption
};

// Marker icons grow with the map but less than it, so markers stay distinct and small icons do not turn blurry.
inline float markerScale(float mapScale) {
    return std::clamp(std::sqrt(std::max(mapScale, 0.0f)), 1.0f, 2.0f);
}

inline MapLayout mapLayout(float ox, float oy, float tw, float th, unsigned width, unsigned height,
                           const MapBox& box = {}) {
    MapLayout m;
    m.ox = ox;
    m.oy = oy;
    m.tw = tw;
    m.th = th;
    if (!(tw > 0 && th > 0) || width < 64 || height < 64)
        return m;
    const float w = float(width), h = float(height);
    m.fit = fitCentred(tw, th, box.margin, box.margin, w - box.margin, h - box.captionReserve);
    // Never shrink below the native size: a map larger than the screen stays as the game draws it.
    if (!(m.fit.scale >= 1.0f)) {
        m.fit = {1.0f, ox, oy};
        return m;
    }
    m.iconScale = markerScale(m.fit.scale);
    m.valid = true;
    return m;
}

// A rectangle of the map (x, y, w, h) drawn at native size whose centre is map-relative: returns the new top-left for
// a new size (nw, nh).
inline void placeCentred(const MapLayout& m, float x, float y, float w, float h, float nw, float nh, float& outX,
                         float& outY) {
    outX = m.x(x + 0.5f * w) - 0.5f * nw;
    outY = m.y(y + 0.5f * h) - 0.5f * nh;
}

// Inverse of the native "you are here" placement (439b28): object x = ((px + 4.5) - W/2) * 50 / kx, with kx the
// renderer's projection product; y = (H/2 - (py - 4.5)) * 50 / ky. Moves the object to the scaled map.
inline float objectToPixelX(float objX, float kx, unsigned width) {
    return objX * kx / 50.0f - 4.5f + float(width >> 1);
}
inline float pixelToObjectX(float px, float kx, unsigned width) {
    return ((px + 4.5f) - float(width >> 1)) * 50.0f / kx;
}
inline float objectToPixelY(float objY, float ky, unsigned height) {
    return float(height >> 1) + 4.5f - objY * ky / 50.0f;
}
inline float pixelToObjectY(float py, float ky, unsigned height) {
    return (float(height >> 1) - (py - 4.5f)) * 50.0f / ky;
}

// Strip view: the Equipment grid (8 x 3 cells) is placed centred along the bottom of the native window inside a bar
// (dark fill, a slot frame per cell, a double brass slot frame around it), the item details in a framed
// panel standing on the bar's left end.
struct Rect {
    float x0 = 0, y0 = 0, x1 = 0, y1 = 0;
    float w() const { return x1 - x0; }
    float h() const { return y1 - y0; }
    bool empty() const { return !(x1 > x0 && y1 > y0); }
};

// The bar's and panel's double slot frame reaches this far outside their fill.
constexpr float kFrameOut = 2;

struct StripBox {
    float bottomMargin = 14; // native pixels below the grid (bar padding + frame stay on screen)
    float barPad = 5;        // bar fill around the grid
    float panelPad = 7;      // panel fill around the details text
    float panelGap = 8;      // panel fill bottom above the bar fill top
};
constexpr float kDetailsLineHeight = 18.0f; // 433850 draws its lines 0x12 apart
constexpr int kStripColumns = 8, kStripRows = 3;

struct StripTarget {
    float gridCentreX = 0, gridCentreY = 0; // where the grid's centre goes
    float gridW = 0, gridH = 0;             // projected grid size
    float textLeft = 0, textTop = 0;        // first details line
};

inline StripTarget stripTarget(float gridW, float gridH, unsigned width, unsigned height, const StripBox& box = {}) {
    StripTarget t;
    const float w = float(width), h = float(height);
    t.gridCentreX = 0.5f * w;
    t.gridCentreY = h - box.bottomMargin - 0.5f * gridH;
    t.gridW = gridW;
    t.gridH = gridH;
    t.textLeft = t.gridCentreX - 0.5f * gridW - box.barPad + box.panelPad;
    t.textTop = t.gridCentreY - 0.5f * gridH - box.barPad - box.panelGap - box.panelPad - 6 * kDetailsLineHeight;
    return t;
}

inline Rect stripGrid(const StripTarget& t) {
    return {t.gridCentreX - 0.5f * t.gridW, t.gridCentreY - 0.5f * t.gridH, t.gridCentreX + 0.5f * t.gridW,
            t.gridCentreY + 0.5f * t.gridH};
}
inline Rect stripCell(const StripTarget& t, int col, int row) {
    const Rect g = stripGrid(t);
    const float cw = g.w() / kStripColumns, ch = g.h() / kStripRows;
    return {g.x0 + col * cw, g.y0 + row * ch, g.x0 + (col + 1) * cw, g.y0 + (row + 1) * ch};
}
inline Rect stripBar(const StripTarget& t, const StripBox& box = {}) {
    const Rect g = stripGrid(t);
    return {g.x0 - box.barPad, g.y0 - box.barPad, g.x1 + box.barPad, g.y1 + box.barPad};
}

// Details text block of `lines` lines (the name, then lines 6 + 0x12 * k below it) whose longest name / stat line has
// nameChars / statChars characters. Width estimated from the inventory font (name about 7.7, stat lines about 6.0
// native pixels per character), clamped to the bar.
struct StripPanel {
    Rect fill;
    float textLeft = 0, textTop = 0;
};
constexpr float kNameCharWidth = 7.7f, kStatCharWidth = 6.0f, kPanelMinWidth = 110;
inline StripPanel stripPanel(const StripTarget& t, int lines, int nameChars, int statChars, const StripBox& box = {}) {
    StripPanel p;
    const Rect bar = stripBar(t, box);
    if (lines <= 0)
        return p;
    const float textW = std::max(nameChars * kNameCharWidth, statChars * kStatCharWidth);
    const float w = std::clamp(textW + 2 * box.panelPad, kPanelMinWidth, bar.w());
    const float textH = 6 + kDetailsLineHeight * float(lines);
    p.fill.x0 = bar.x0;
    p.fill.x1 = bar.x0 + w;
    p.fill.y1 = bar.y0 - box.panelGap;
    p.fill.y0 = p.fill.y1 - textH - 2 * box.panelPad;
    p.textLeft = p.fill.x0 + box.panelPad;
    p.textTop = p.fill.y0 + box.panelPad;
    return p;
}

// Lines of the Equipment details text (433850): name +0x244 at line 0, then +0x2c4 (only when at least 2 characters
// long), +0x284, +0x304, +0x450 (only when +0x2c4 is not drawn), +0x344, +0x384, one line each. Returns the number of
// lines up to the last non-empty one; `lengths` in the order above (the name first).
inline int detailsLines(const int (&lengths)[7]) {
    int line = 0, last = -1;
    const bool second = lengths[1] >= 2;
    for (int i = 0; i < 7; ++i) {
        if ((i == 1 && !second) || (i == 4 && second))
            continue;
        if (lengths[i] > 0)
            last = line;
        ++line;
    }
    return last + 1;
}

// Grid centre in section-plane units from the native fields: cells are at (+0x68 + col * cw, +0x6c - row * ch),
// 8 columns and 3 rows.
constexpr float gridCentrePlaneX(float x0, float cw) {
    return x0 + 3.5f * cw;
}
constexpr float gridCentrePlaneY(float y0, float ch) {
    return y0 - 1.0f * ch;
}

// One Newton step for a plane point p whose projection should be `target`, given the projection at p and its
// Jacobian (d screen / d plane). Returns false when the Jacobian is degenerate.
inline bool solvePlane(float px, float py, float sx, float sy, float dxdu, float dxdv, float dydu, float dydv,
                       float targetX, float targetY, float& outX, float& outY) {
    const float det = dxdu * dydv - dxdv * dydu;
    if (!std::isfinite(det) || std::fabs(det) < 1e-9f)
        return false;
    const float ex = targetX - sx, ey = targetY - sy;
    outX = px + (dydv * ex - dxdv * ey) / det;
    outY = py + (-dydu * ex + dxdu * ey) / det;
    return std::isfinite(outX) && std::isfinite(outY);
}

// What a view key does this frame. Opening only from free gameplay; any view key closes an open view (the screen is
// the inventory then, and its keys come through the menu layer, where these keys have no other meaning).
enum class KeyAction : uint8_t { None, Open, Close };
constexpr KeyAction viewKeyAction(bool keyPressed, bool freeGameplay, View active) {
    if (!keyPressed)
        return KeyAction::None;
    if (active != View::None)
        return KeyAction::Close;
    return freeGameplay ? KeyAction::Open : KeyAction::None;
}

// The inventory screen a view was opened on is still up: it is the active screen, or a message box (1, 2, 3, 0x1b)
// is stacked on it (its parent at box+0x34), e.g. a first-visit help box.
constexpr bool viewScreenUp(uint32_t bound, uint32_t active, int activeId, uint32_t activeParent) {
    if (!bound)
        return false;
    if (active == bound)
        return true;
    const bool box = activeId == 1 || activeId == 2 || activeId == 3 || activeId == 0x1b;
    return box && activeParent == bound;
}

} // namespace drakan_views
